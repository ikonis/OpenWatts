#include "power_manager.h"

#include <algorithm>
#include <cinttypes>

#include "ble_cycling_power.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "hx711.h"
#include "imu_lsm6ds3.h"
#include "led_status.h"
#include "setup_wifi.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "power";
}

void PowerManager::updateConfig(const DeviceConfig &config) {
    config_ = config;
    // OpenWatts uses light sleep: GPIO10 (IMU INT) cannot deep-sleep wake an
    // ESP32-C3.  A timer check is required for battery reporting while idle.
    config_.wake_on_timer_enabled = true;
    config_.wake_on_usb_enabled = true;
    config_.timer_wake_seconds = std::max<uint32_t>(60, config.timer_wake_seconds);
}

bool PowerManager::shouldSleepForInactivity(const CadenceState &cadence, int64_t now_us) const {
    if (!config_.light_sleep_enabled || cadence.moving) {
        return false;
    }
    if (last_wake_us_ != 0 &&
        (now_us - last_wake_us_) < static_cast<int64_t>(config_.inactivity_timeout_ms) * 1000LL) {
        return false;
    }
    if (cadence.last_revolution_us == 0) {
        return now_us >= static_cast<int64_t>(config_.inactivity_timeout_ms) * 1000LL;
    }
    const int64_t idle_us = now_us - cadence.last_revolution_us;
    return idle_us >= static_cast<int64_t>(config_.inactivity_timeout_ms) * 1000LL;
}

esp_err_t PowerManager::enterSleep(BleCyclingPowerService &ble, SetupWifi &setup_wifi, Hx711 &hx711,
                                   Lsm6ds3 &imu, WakeResult *wake_result) const {
    if (!config_.light_sleep_enabled) {
        return ESP_OK;
    }
    ESP_LOGI(kTag, "entering IMU-armed light sleep");
    ble.stop();
    setup_wifi.stop();
    ESP_RETURN_ON_ERROR(hx711.prepareForSleep(), kTag, "HX711 power-down");
    ESP_RETURN_ON_ERROR(imu.configureWakeMode(config_.imu_wake_threshold, config_.imu_wake_duration),
                        kTag, "IMU wake mode");
    ESP_RETURN_ON_ERROR(configureLightSleepWakeSources(), kTag, "light-sleep wake sources");

    ledStatus().setSleeping(true);
    const esp_err_t sleep_err = esp_light_sleep_start();
    ledStatus().setSleeping(false);
    gpio_wakeup_disable(board::kImuInt);
    gpio_wakeup_disable(board::kUsbPresent);
    if (sleep_err != ESP_OK) {
        return sleep_err;
    }

    uint8_t wake_source = 0;
    ESP_RETURN_ON_ERROR(imu.clearWakeSource(&wake_source), kTag, "clear IMU wake");
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    last_wake_us_ = esp_timer_get_time();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        // Do not reinitialize riding services. The caller performs a battery
        // decision and either reports or returns straight to light sleep.
        if (wake_result != nullptr) *wake_result = WakeResult::Timer;
        ESP_LOGI(kTag, "light-sleep timer wake");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(imu.configureActiveMode(), kTag, "IMU active mode");
    ESP_RETURN_ON_ERROR(hx711.resumeFromSleep(), kTag, "HX711 resume");
    ble.resumeAdvertising();
    if (wake_result != nullptr) *wake_result = WakeResult::MotionOrUsb;
    ESP_LOGI(kTag, "light-sleep wake: cause=%d imu_source=0x%02x imu_int=%d usb=%d",
             static_cast<int>(esp_sleep_get_wakeup_cause()), wake_source,
             gpio_get_level(board::kImuInt), board::usbPresent() ? 1 : 0);
    return ESP_OK;
}

esp_err_t PowerManager::configureLightSleepWakeSources() const {
    ESP_RETURN_ON_ERROR(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL), kTag, "clear wake sources");
    if (config_.wake_on_imu_enabled) {
        ESP_RETURN_ON_ERROR(gpio_wakeup_enable(board::kImuInt, GPIO_INTR_HIGH_LEVEL), kTag, "IMU GPIO wake");
    }
    // USB insertion is a wake source only when we entered sleep without USB.
    // Enabling a high-level GPIO wake while USB is already present would make
    // an intentional bench sleep return immediately.
    if (config_.wake_on_usb_enabled && !board::usbPresent()) {
        ESP_RETURN_ON_ERROR(gpio_wakeup_enable(board::kUsbPresent, GPIO_INTR_HIGH_LEVEL), kTag, "USB GPIO wake");
    }
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), kTag, "GPIO light-sleep wake");
    if (config_.wake_on_timer_enabled) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(
                                static_cast<uint64_t>(config_.timer_wake_seconds) * 1000000ULL),
                            kTag, "timer wake");
    }
    return ESP_OK;
}

}  // namespace openwatts
