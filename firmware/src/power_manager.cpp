#include "power_manager.h"

#include "ble_cycling_power.h"
#include "board.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "setup_wifi.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "power";
}

void PowerManager::updateConfig(const DeviceConfig &config) {
    config_ = config;
}

bool PowerManager::shouldSleepForInactivity(const CadenceState &cadence, int64_t now_us) const {
    if (!config_.deep_sleep_enabled || cadence.moving) {
        return false;
    }
    if (cadence.last_revolution_us == 0) {
        return now_us >= static_cast<int64_t>(config_.inactivity_timeout_ms) * 1000LL;
    }
    const int64_t idle_us = now_us - cadence.last_revolution_us;
    return idle_us >= static_cast<int64_t>(config_.inactivity_timeout_ms) * 1000LL;
}

void PowerManager::enterSleep(BleCyclingPowerService &ble, SetupWifi &setup_wifi) const {
    ESP_LOGI(kTag, "entering deep sleep");
    board::setGreenLed(false);
    ble.stop();
    setup_wifi.stop();
    configureWakeSources();
    esp_deep_sleep_start();
}

esp_err_t PowerManager::configureWakeSources() const {
    esp_err_t err = ESP_OK;
    if (config_.wake_on_timer_enabled) {
        err = esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(config_.timer_wake_seconds) * 1000000ULL);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (config_.wake_on_button_enabled) {
        ESP_LOGW(kTag, "button wake requested but not enabled yet; TODO map BOOT to supported wake source");
    }
    if (config_.wake_on_usb_enabled) {
        ESP_LOGW(kTag, "USB wake requested but not enabled yet; TODO validate GPIO8 wake from deep sleep");
    }
    if (config_.wake_on_imu_enabled) {
        ESP_LOGW(kTag, "IMU wake requested but not enabled yet; TODO configure LSM6DS3 INT and ESP wake");
    }
    return ESP_OK;
}

}  // namespace openwatts
