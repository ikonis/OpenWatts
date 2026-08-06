#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>

#include "ble_cycling_power.h"
#include "battery_policy.h"
#include "board.h"
#include "cadence_estimator.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"
#include "imu_lsm6ds3.h"
#include "mqtt_notifier.h"
#include "nvs_flash.h"
#include "operating_mode.h"
#include "power_estimator.h"
#include "power_manager.h"
#include "report_policy.h"
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
openwatts::BatteryPolicy g_battery_policy(g_config);
openwatts::MqttNotifier g_mqtt;
bool g_mqtt_discovery_pending = true;
char g_mqtt_payload[512]{};
openwatts::ReportHistory g_report_history{};
openwatts::ReportReason g_pending_report_reason = openwatts::ReportReason::None;
bool g_timer_decision_pending = false;
bool g_battery_display_initialized = false;
float g_battery_display_voltage = 0.0F;
float g_battery_percent_reference_voltage = 0.0F;
uint8_t g_battery_display_percent = 0;
float g_provisional_angle = 0.0F;
float g_provisional_total_degrees = 0.0F;
uint32_t g_provisional_revolutions = 0;
int64_t g_last_provisional_imu_us = 0;

const char *wakeReason() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_GPIO: return "motion_or_usb";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "power_on";
        default: return "other";
    }
}
const char *resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW: return "software";
        case ESP_RST_WDT: case ESP_RST_TASK_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "other";
    }
}

uint8_t estimateBatteryPercent(float voltage) {
    struct Point { float voltage; uint8_t percent; };
    // This is the same conservative 1S LiPo display curve validated on
    // ikoniWatts.  4.16 V is the observed full-charge endpoint, not 4.20 V.
    static constexpr Point curve[] = {
        {3.20F, 0}, {3.50F, 5}, {3.65F, 15}, {3.75F, 35},
        {3.85F, 55}, {3.95F, 75}, {4.05F, 90}, {4.16F, 100},
    };
    if (voltage <= curve[0].voltage) return 0;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
        if (voltage <= curve[i].voltage) {
            const float fraction = (voltage - curve[i - 1].voltage) /
                                   (curve[i].voltage - curve[i - 1].voltage);
            return static_cast<uint8_t>(std::lround(
                curve[i - 1].percent + fraction * (curve[i].percent - curve[i - 1].percent)));
        }
    }
    return 100;
}

uint8_t stableBatteryPercent(float voltage) {
    // Keep the measured voltage authoritative, but stabilize the human-facing
    // percentage exactly as ikoniWatts does.  This prevents tiny ADC or
    // charge-surface changes from making the dashboard visibly jump.
    constexpr float kAlpha = 0.12F;
    constexpr float kVoltageHysteresis = 0.006F;
    if (!g_battery_display_initialized) {
        g_battery_display_initialized = true;
        g_battery_display_voltage = voltage;
        g_battery_percent_reference_voltage = voltage;
        g_battery_display_percent = estimateBatteryPercent(voltage);
        return g_battery_display_percent;
    }
    g_battery_display_voltage += (voltage - g_battery_display_voltage) * kAlpha;
    const uint8_t candidate = estimateBatteryPercent(g_battery_display_voltage);
    const float change_since_display = g_battery_display_voltage - g_battery_percent_reference_voltage;
    if ((candidate > g_battery_display_percent && change_since_display >= kVoltageHysteresis) ||
        (candidate < g_battery_display_percent && change_since_display <= -kVoltageHysteresis)) {
        g_battery_display_percent = candidate;
        g_battery_percent_reference_voltage = g_battery_display_voltage;
    }
    return g_battery_display_percent;
}

openwatts::BatteryReading readBattery() {
    openwatts::BatteryReading result{};
    const uint32_t adc_mv = openwatts::board::readBatteryMillivolts();
    if (adc_mv == 0) return result;
    result.voltage = (static_cast<float>(adc_mv) * g_config.battery_voltage_scale +
                      static_cast<float>(g_config.battery_voltage_offset_mv)) / 1000.0F;
    result.valid = result.voltage >= 3.0F && result.voltage <= 4.30F;
    result.estimated_percent = result.valid ? stableBatteryPercent(result.voltage) : 0;
    return result;
}

bool startBatteryReport(const openwatts::BatteryReading &battery, openwatts::BatteryState state,
                        openwatts::ReportReason reason, bool usb_present, int64_t now_us) {
    std::snprintf(g_mqtt_payload, sizeof(g_mqtt_payload),
                  "{\"device\":\"OpenWatts\",\"firmware_version\":\"%s\","
                  "\"battery_voltage\":%.2f,\"estimated_percent\":%u,"
                  "\"battery_status\":\"%s\",\"battery_valid\":%s,"
                  "\"usb_present\":%s,\"runtime\":\"%s\",\"device_health\":\"Healthy\","
                  "\"report_reason\":\"%s\"}",
                  OPENWATTS_FIRMWARE_VERSION, battery.voltage,
                  static_cast<unsigned>(battery.estimated_percent),
                  openwatts::BatteryPolicy::name(state), battery.valid ? "true" : "false",
                  usb_present ? "true" : "false",
                  openwatts::OperatingPolicy::isMaintenance(g_config) ? "Maintenance" :
                      (usb_present ? "Normal / USB" : "Battery Report"),
                  openwatts::reportReasonName(reason));
    const esp_err_t mqtt_err = g_mqtt.begin(g_config.mqtt_host, g_config.mqtt_port, g_config.mqtt_topic,
                                            g_mqtt_payload, g_mqtt_discovery_pending);
    g_report_history.last_attempt_seconds = static_cast<uint64_t>(now_us / 1000000LL);
    if (mqtt_err != ESP_OK) {
        ESP_LOGW(kTag, "MQTT start failed: %s", esp_err_to_name(mqtt_err));
        g_report_history.retry_pending = true;
        return false;
    }
    return true;
}

void finishBatteryReport(const openwatts::BatteryReading &battery, openwatts::BatteryState state,
                         int64_t now_us) {
    if (!g_mqtt.running() || !g_mqtt.complete()) return;
    if (g_mqtt.succeeded()) {
        g_mqtt_discovery_pending = false;
        g_report_history.has_success = true;
        g_report_history.last_voltage = battery.voltage;
        g_report_history.last_state = state;
        g_report_history.last_success_seconds = static_cast<uint64_t>(now_us / 1000000LL);
        g_report_history.retry_pending = false;
    } else {
        g_report_history.retry_pending = true;
    }
    g_mqtt.stop();
}
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
    ESP_LOGI(kTag, "Operating Mode: %s", openwatts::OperatingPolicy::name(g_config.operating_mode));

    ESP_ERROR_CHECK(openwatts::board::initPins());
    ESP_ERROR_CHECK(openwatts::board::initI2c());
    ESP_ERROR_CHECK(openwatts::board::initBatteryAdc());

    g_cadence.updateConfig(g_config);
    g_power.updateConfig(g_config);
    g_power_manager.updateConfig(g_config);
    g_battery_policy.updateConfig(g_config);

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

    int64_t last_battery_policy_check_us = -5LL * 1000LL * 1000LL;

    err = g_ble.begin(g_config.ble_device_name);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "BLE init failed: %s", esp_err_to_name(err));
    }
    if (run_self_test) {
        g_ble.setDiagnostics(openwatts::SelfTest::summary(self_test_result));
    }

    int64_t last_publish_us = 0;
    int64_t last_status_battery_us = -1000000;
    openwatts::BatteryReading status_battery{};
    bool previous_usb_present = usb_present;
    while (true) {
        openwatts::board::setGreenLed(true);
        if (g_timer_decision_pending) {
            const int64_t timer_now_us = esp_timer_get_time();
            const bool timer_usb_present = openwatts::board::usbPresent();
            status_battery = readBattery();
            const openwatts::BatteryState timer_state = g_battery_policy.qualify(status_battery);
            const openwatts::ReportReason timer_reason = openwatts::decideBatteryReport(
                g_config, status_battery, timer_state, g_report_history,
                static_cast<uint64_t>(timer_now_us / 1000000LL));

            finishBatteryReport(status_battery, timer_state, timer_now_us);
            if (!g_mqtt.running() && timer_reason != openwatts::ReportReason::None &&
                g_config.hasWifiCredentials() && g_config.mqtt_battery_notifications_enabled) {
                if (!g_setup_wifi.active()) {
                    const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, timer_usb_present, false, true);
                    if (wifi_err != ESP_OK) {
                        ESP_LOGW(kTag, "timer report Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                        g_report_history.retry_pending = true;
                    } else {
                        startBatteryReport(status_battery, timer_state, timer_reason, timer_usb_present, timer_now_us);
                    }
                } else {
                    startBatteryReport(status_battery, timer_state, timer_reason, timer_usb_present, timer_now_us);
                }
            }

            if (!g_mqtt.running()) {
                if (!openwatts::OperatingPolicy::permitsWifi(g_config, timer_usb_present)) g_setup_wifi.stop();
                openwatts::PowerManager::WakeResult wake_result{};
                const esp_err_t sleep_err = g_power_manager.enterSleep(
                    g_ble, g_setup_wifi, g_hx711, g_imu, &wake_result);
                if (sleep_err != ESP_OK) {
                    ESP_LOGW(kTag, "timer decision sleep failed: %s", esp_err_to_name(sleep_err));
                }
                g_timer_decision_pending = wake_result == openwatts::PowerManager::WakeResult::Timer;
            }
            openwatts::board::setGreenLed(false);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int32_t raw_counts = 0;
        const bool hx_ok = g_hx711.read(raw_counts, 25);
        g_hx711.observe(hx_ok, raw_counts, g_config.hx711_smoothing);

        openwatts::ImuSample imu_sample{};
        g_imu.read(imu_sample);

        const int64_t now_us = esp_timer_get_time();
        const bool current_usb_present = openwatts::board::usbPresent();
        g_setup_wifi.observeCalibration(true, hx_ok, raw_counts, g_hx711.ready(), now_us);
        if (g_setup_wifi.consumeImuTrackerReset()) {
            g_cadence.reset();
            g_provisional_angle = 0.0F;
            g_provisional_total_degrees = 0.0F;
            g_provisional_revolutions = 0;
            g_last_provisional_imu_us = now_us;
        }
        if (g_setup_wifi.consumeBenchLightSleepRequest()) {
            ESP_LOGI(kTag, "bench request: entering IMU-armed light sleep");
            const esp_err_t sleep_err = g_power_manager.enterSleep(g_ble, g_setup_wifi, g_hx711, g_imu);
            if (sleep_err != ESP_OK) {
                ESP_LOGW(kTag, "bench light sleep failed: %s", esp_err_to_name(sleep_err));
            } else if (openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present)) {
                const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, current_usb_present, false);
                if (wifi_err != ESP_OK) ESP_LOGW(kTag, "Wi-Fi restore after bench wake failed: %s", esp_err_to_name(wifi_err));
            }
            g_cadence.reset();
            g_power.reset();
            continue;
        }
        if (current_usb_present != previous_usb_present) {
            previous_usb_present = current_usb_present;
            ESP_LOGI(kTag, "USB %s", current_usb_present ? "inserted" : "removed");
            g_pending_report_reason = current_usb_present ? openwatts::ReportReason::UsbConnected
                                                          : openwatts::ReportReason::UsbDisconnected;
            if (current_usb_present) {
                const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, true, false);
                if (wifi_err != ESP_OK) {
                    ESP_LOGW(kTag, "Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                }
            } else if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present)) {
                g_setup_wifi.stop();
            } else {
                ESP_LOGI(kTag, "USB removed; Maintenance Mode keeps Wi-Fi and WebUI active");
            }
        }
        // A Settings save can change Operating Mode without a USB edge.
        // Honor Normal Mode promptly after the response has been delivered.
        if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present) &&
            g_setup_wifi.active() && !g_mqtt.running()) {
            g_setup_wifi.stop();
        }
        const openwatts::CadenceState cadence = g_cadence.update(imu_sample, now_us);
        float provisional_velocity = 0.0F;
        float provisional_cadence = 0.0F;
        float provisional_confidence = 0.0F;
        const char *provisional_reason = "maintenance_required";
        bool motion_detected = false;
        if (openwatts::OperatingPolicy::permitsMaintenanceTools(g_config) && imu_sample.valid) {
            const float gx = std::fabs(imu_sample.gyro_dps[0]);
            const float gy = std::fabs(imu_sample.gyro_dps[1]);
            const float gz = std::fabs(imu_sample.gyro_dps[2]);
            provisional_velocity = gx >= gy && gx >= gz ? imu_sample.gyro_dps[0] : gy >= gz ? imu_sample.gyro_dps[1] : imu_sample.gyro_dps[2];
            motion_detected = std::fabs(provisional_velocity) >= 5.0F;
            const float dt = g_last_provisional_imu_us > 0 ? static_cast<float>(now_us - g_last_provisional_imu_us) / 1000000.0F : 0.0F;
            g_last_provisional_imu_us = now_us;
            if (dt > 0 && dt < 0.25F) {
                const float step = provisional_velocity * dt;
                g_provisional_angle = std::fmod(g_provisional_angle + step + 360.0F, 360.0F);
                g_provisional_total_degrees += std::fabs(step);
                g_provisional_revolutions = static_cast<uint32_t>(g_provisional_total_degrees / 360.0F);
            }
            provisional_cadence = std::fabs(provisional_velocity) / 6.0F;
            provisional_confidence = motion_detected ? std::min(0.75F, std::fabs(provisional_velocity) / 360.0F) : 0.0F;
            provisional_reason = motion_detected ? "axis_not_validated" : "stationary";
        } else if (openwatts::OperatingPolicy::permitsMaintenanceTools(g_config)) {
            provisional_reason = "imu_unavailable";
        }
        // Web calibration may update scale/direction/zero without reboot.
        g_power.updateConfig(g_config);
        const openwatts::PowerSample sample = g_power.update(raw_counts, g_hx711.filtered(), g_hx711.noiseEstimate(),
                                                             g_hx711.ready(), cadence);
        if (now_us - last_status_battery_us >= 1000LL * 1000LL) {
            last_status_battery_us = now_us;
            status_battery = readBattery();
        }
        g_setup_wifi.updateLiveStatus({
            .uptime_seconds = static_cast<uint32_t>(now_us / 1000000LL), .battery_voltage = status_battery.voltage,
            .battery_percent = status_battery.estimated_percent, .battery_valid = status_battery.valid,
            .usb_present = current_usb_present, .charging = openwatts::board::chargeStatActive(),
            .ble_connected = g_ble.connected(), .imu_ready = imu_sample.valid,
            .imu_interrupt_active = openwatts::board::imuInterruptActive(), .imu_who_am_i = g_imu.whoAmI(),
            .imu_accel_g = {imu_sample.accel_g[0], imu_sample.accel_g[1], imu_sample.accel_g[2]},
            .imu_gyro_dps = {imu_sample.gyro_dps[0], imu_sample.gyro_dps[1], imu_sample.gyro_dps[2]},
            .hx711_ready = sample.hx711_ready,
            .strain_calibration_valid = g_config.strain_calibration_valid,
            .raw_counts = sample.raw_counts, .filtered_counts = sample.filtered_counts, .torque_nm = sample.torque_nm,
            .cadence_rpm = sample.cadence_rpm, .power_watts = sample.power_watts,
            .revolutions = cadence.revolutions, .hx711_failures = g_hx711.readFailures(),
            .hx711_noise = g_hx711.noiseEstimate(),
            .hx711_sample_rate_hz = g_config.sample_interval_ms ? 1000.0F / g_config.sample_interval_ms : 0.0F,
            .wake_reason = wakeReason(), .reset_reason = resetReason(),
            .provisional_angle_degrees = g_provisional_angle,
            .provisional_angular_velocity_dps = provisional_velocity,
            .provisional_cadence_rpm = provisional_cadence,
            .provisional_revolutions = g_provisional_revolutions,
            .provisional_confidence = provisional_confidence,
            .provisional_reason = provisional_reason, .motion_detected = motion_detected,
        });

        // Reporting is independent from battery sampling.  It can wake the
        // radio only when a valid report is due, never for a percentage-only
        // dashboard change.
        if (g_mqtt.running() && g_mqtt.complete()) {
            finishBatteryReport(status_battery, g_battery_policy.state(), now_us);
            if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present)) {
                g_setup_wifi.stop();
            }
        }
        if (now_us - last_battery_policy_check_us >= static_cast<int64_t>(
                openwatts::OperatingPolicy::mqttEvaluationIntervalSeconds(g_config, current_usb_present)) * 1000000LL) {
            last_battery_policy_check_us = now_us;
            const openwatts::BatteryState state = g_battery_policy.qualify(status_battery);
            openwatts::ReportReason reason = g_pending_report_reason;
            if (reason == openwatts::ReportReason::None) {
                reason = openwatts::decideBatteryReport(g_config, status_battery, state, g_report_history,
                                                        static_cast<uint64_t>(now_us / 1000000LL));
            }
            if (reason == openwatts::ReportReason::None &&
                openwatts::OperatingPolicy::isMaintenance(g_config)) {
                reason = openwatts::ReportReason::Manual;
            }
            if (!g_mqtt.running() && reason != openwatts::ReportReason::None &&
                g_config.hasWifiCredentials() && g_config.mqtt_battery_notifications_enabled) {
                if (!g_setup_wifi.active()) {
                    const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, current_usb_present, false, true);
                    if (wifi_err != ESP_OK) {
                        ESP_LOGW(kTag, "Wi-Fi report start failed: %s", esp_err_to_name(wifi_err));
                        g_report_history.retry_pending = true;
                        g_report_history.last_attempt_seconds = static_cast<uint64_t>(now_us / 1000000LL);
                        continue;
                    }
                }
            if (startBatteryReport(status_battery, state, reason, current_usb_present, now_us)) {
                g_pending_report_reason = openwatts::ReportReason::None;
            }
            }
        }

        if ((now_us - last_publish_us) >= static_cast<int64_t>(g_config.publish_interval_ms) * 1000LL) {
            last_publish_us = now_us;
            g_ble.notify(sample);
            if (g_config.ride_diagnostics_enabled) {
                ESP_LOGI(kTag,
                         "rpm=%.1f power=%dW raw=%" PRId32 " hx=%d imu=%d rev=%" PRIu32 " ble=%d wifi_setup=%d",
                         sample.cadence_rpm, sample.power_watts, sample.raw_counts, sample.hx711_ready ? 1 : 0,
                         imu_sample.valid ? 1 : 0, cadence.revolutions, g_ble.connected() ? 1 : 0,
                         g_setup_wifi.active() ? 1 : 0);
            }
        }

        // An active BLE consumer is riding activity even if cadence has paused.
        // Begin the inactivity path only after the app disconnects.
        // Maintenance deliberately keeps the runtime awake; entering light
        // sleep would immediately tear down the web server and radio.
        if (openwatts::OperatingPolicy::permitsInactivitySleep(g_config) && !current_usb_present && !g_ble.connected() &&
            g_power_manager.shouldSleepForInactivity(cadence, now_us)) {
            openwatts::PowerManager::WakeResult wake_result{};
            const esp_err_t sleep_err = g_power_manager.enterSleep(g_ble, g_setup_wifi, g_hx711, g_imu, &wake_result);
            if (sleep_err != ESP_OK) {
                ESP_LOGW(kTag, "sleep/wake failed: %s", esp_err_to_name(sleep_err));
            } else {
                // A wake interrupt is motion, not a completed crank revolution.
                // Require the cadence provider and power filter to reacquire.
                g_cadence.reset();
                g_power.reset();
                g_timer_decision_pending = wake_result == openwatts::PowerManager::WakeResult::Timer;
            }
        }
        openwatts::board::setGreenLed(false);
        vTaskDelay(pdMS_TO_TICKS(g_config.sample_interval_ms));
    }
}
