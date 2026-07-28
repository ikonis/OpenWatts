#include <cinttypes>

#include "ble_cycling_power.h"
#include "board.h"
#include "cadence_estimator.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"
#include "imu_lsm6ds3.h"
#include "nvs_flash.h"
#include "power_estimator.h"
#include "power_manager.h"
#include "setup_wifi.h"
#include "settings_storage.h"
#include "self_test.h"

namespace {
constexpr char kTag[] = "openwatts";

openwatts::DeviceConfig g_config;
openwatts::Hx711 g_hx711(openwatts::board::kHx711Dout, openwatts::board::kHx711Sck);
openwatts::Lsm6ds3 g_imu;
openwatts::CadenceEstimator g_cadence;
openwatts::PowerEstimator g_power;
openwatts::PowerManager g_power_manager;
openwatts::BleCyclingPowerService g_ble;
openwatts::SetupWifi g_setup_wifi;
openwatts::SettingsStorage g_settings;
openwatts::SelfTest g_self_test;
}  // namespace

extern "C" void app_main() {
    ESP_LOGI(kTag, "OpenWatts firmware %s", OPENWATTS_FIRMWARE_VERSION);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(g_settings.begin());
    g_config = g_settings.load();

    ESP_ERROR_CHECK(openwatts::board::initPins());
    ESP_ERROR_CHECK(openwatts::board::initI2c());
    ESP_ERROR_CHECK(openwatts::board::initBatteryAdc());

    g_cadence.updateConfig(g_config);
    g_power.updateConfig(g_config);
    g_power_manager.updateConfig(g_config);

    ESP_ERROR_CHECK(g_hx711.begin());

    err = g_imu.begin();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "IMU init failed; continuing for HX711/BLE bring-up: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(kTag, "LSM6DS3 WHO_AM_I=0x%02x", g_imu.whoAmI());
    }

    const bool usb_present = openwatts::board::usbPresent();
    const bool setup_requested = openwatts::board::bootButtonPressed();
    const bool self_test_requested = setup_requested;
    ESP_LOGI(kTag, "USB_PRESENT=%d CHG_STAT(active-low)=%d", usb_present ? 1 : 0,
             openwatts::board::chargeStatActive() ? 1 : 0);

    openwatts::SelfTestResult self_test_result{};
    const bool run_self_test = !g_config.self_test_done || g_config.run_self_test_on_boot || self_test_requested;
    if (run_self_test) {
        self_test_result = g_self_test.run(g_imu, g_hx711);
        ESP_LOGI(kTag, "self-test: %s", openwatts::SelfTest::summary(self_test_result));
        if (!self_test_requested) {
            ESP_ERROR_CHECK(g_settings.markSelfTestDone(g_config));
        }
    }

    ESP_ERROR_CHECK(g_setup_wifi.begin(g_config, g_settings, usb_present, setup_requested));

    err = g_ble.begin(g_config.ble_device_name);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "BLE init failed: %s", esp_err_to_name(err));
    }
    if (run_self_test) {
        g_ble.setDiagnostics(openwatts::SelfTest::summary(self_test_result));
    }

    int64_t last_publish_us = 0;
    bool previous_usb_present = usb_present;
    while (true) {
        openwatts::board::setGreenLed(true);

        int32_t raw_counts = 0;
        const bool hx_ok = g_hx711.read(raw_counts, 25);
        g_hx711.observe(hx_ok, raw_counts, g_config.hx711_smoothing);

        openwatts::ImuSample imu_sample{};
        g_imu.read(imu_sample);

        const int64_t now_us = esp_timer_get_time();
        const bool current_usb_present = openwatts::board::usbPresent();
        if (current_usb_present != previous_usb_present) {
            previous_usb_present = current_usb_present;
            ESP_LOGI(kTag, "USB %s", current_usb_present ? "inserted" : "removed");
            if (current_usb_present) {
                const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, true, false);
                if (wifi_err != ESP_OK) {
                    ESP_LOGW(kTag, "Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                }
            } else {
                g_setup_wifi.stop();
            }
        }
        const openwatts::CadenceState cadence = g_cadence.update(imu_sample, now_us);
        const openwatts::PowerSample sample = g_power.update(raw_counts, g_hx711.filtered(), g_hx711.noiseEstimate(),
                                                             g_hx711.ready(), cadence);

        if ((now_us - last_publish_us) >= static_cast<int64_t>(g_config.publish_interval_ms) * 1000LL) {
            last_publish_us = now_us;
            g_ble.notify(sample);
            ESP_LOGI(kTag,
                     "rpm=%.1f power=%dW raw=%" PRId32 " hx=%d imu=%d rev=%" PRIu32 " ble=%d wifi_setup=%d",
                     sample.cadence_rpm, sample.power_watts, sample.raw_counts, sample.hx711_ready ? 1 : 0,
                     imu_sample.valid ? 1 : 0, cadence.revolutions, g_ble.connected() ? 1 : 0,
                     g_setup_wifi.active() ? 1 : 0);
        }

        // An active BLE consumer is riding activity even if cadence has paused.
        // Begin the inactivity path only after the app disconnects.
        if (!current_usb_present && !g_ble.connected() &&
            g_power_manager.shouldSleepForInactivity(cadence, now_us)) {
            const esp_err_t sleep_err = g_power_manager.enterSleep(g_ble, g_setup_wifi, g_hx711, g_imu);
            if (sleep_err != ESP_OK) {
                ESP_LOGW(kTag, "sleep/wake failed: %s", esp_err_to_name(sleep_err));
            } else {
                // A wake interrupt is motion, not a completed crank revolution.
                // Require the cadence provider and power filter to reacquire.
                g_cadence.reset();
                g_power.reset();
            }
        }
        openwatts::board::setGreenLed(false);
        vTaskDelay(pdMS_TO_TICKS(g_config.sample_interval_ms));
    }
}
