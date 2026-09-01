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
#include "led_status.h"
#include "mqtt_notifier.h"
#include "nvs_flash.h"
#include "operating_mode.h"
#include "power_estimator.h"
#include "power_manager.h"
#include "report_policy.h"
#include "ride_log.h"
#include "ride_zero.h"
#include "setup_wifi.h"
#include "settings_storage.h"
#include "self_test.h"

namespace {
constexpr char kTag[] = "openwatts";
constexpr int64_t kSleepReportTimeoutUs = 15LL * 1000000LL;

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
openwatts::RideLog g_ride_log;
openwatts::RideZeroController g_ride_zero;
bool g_mqtt_discovery_pending = true;
char g_mqtt_payload[1280]{};
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
int64_t g_mqtt_started_us = 0;
// Exponential backoff after repeated MQTT failures (e.g. an unreachable
// broker). Retrying a route-level failure every evaluation interval never
// helps it succeed and was observed to slowly exhaust sockets/PCBs shared
// with the web server. Backoff is in-RAM only; it resets on every boot and
// on the first success, and never disables the feature.
uint32_t g_mqtt_consecutive_failures = 0;
int64_t g_mqtt_next_attempt_us = 0;
constexpr int64_t kMqttBackoffBaseUs = 30LL * 1000000LL;
constexpr int64_t kMqttBackoffMaxUs = 30LL * 60LL * 1000000LL;

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
    // Conservative 1S LiPo display curve. Voltage remains authoritative;
    // 4.16 V is the observed full-charge endpoint on assembled OpenWatts.
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
    // Keep measured voltage authoritative while preventing ADC noise and
    // charge-surface changes from making the display visibly jump.
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

void recordMqttFailure(int64_t now_us) {
    if (g_mqtt_consecutive_failures < 16) ++g_mqtt_consecutive_failures;
    int64_t backoff_us = kMqttBackoffBaseUs << (g_mqtt_consecutive_failures - 1);
    if (backoff_us > kMqttBackoffMaxUs || backoff_us <= 0) backoff_us = kMqttBackoffMaxUs;
    g_mqtt_next_attempt_us = now_us + backoff_us;
    ESP_LOGW(kTag, "MQTT backing off %lld s after %u consecutive failures",
             static_cast<long long>(backoff_us / 1000000LL), static_cast<unsigned>(g_mqtt_consecutive_failures));
}

bool startBatteryReport(const openwatts::BatteryReading &battery, openwatts::BatteryState state,
                        openwatts::ReportReason reason, bool usb_present, int64_t now_us) {
    const openwatts::LastRideSummary &ride = g_ride_log.lastRide();
    std::snprintf(g_mqtt_payload, sizeof(g_mqtt_payload),
                  "{\"device\":\"OpenWatts\",\"firmware_version\":\"%s\","
                  "\"battery_voltage\":%.2f,\"estimated_percent\":%u,"
                  "\"battery_status\":\"%s\",\"battery_valid\":%s,"
                  "\"usb_present\":%s,\"runtime\":\"%s\",\"device_health\":\"Healthy\","
                  "\"report_reason\":\"%s\","
                  "\"last_ride_valid\":%s,\"last_ride_sequence\":%u,"
                  "\"last_ride_moving_seconds\":%u,\"last_ride_elapsed_seconds\":%u,"
                  "\"last_ride_revolutions\":%u,\"last_ride_average_power\":%.1f,"
                  "\"last_ride_peak_power\":%d,\"last_ride_average_cadence\":%.1f,"
                  "\"last_ride_peak_cadence\":%.1f,\"last_ride_work_kj\":%.2f,"
                  "\"last_ride_estimated_distance_m\":%.2f,\"last_ride_average_speed_mps\":%.3f,"
                  "\"last_ride_peak_speed_mps\":%.3f,\"last_ride_road_model_version\":%u,"
                  "\"last_ride_rider_mass_kg\":%.2f,"
                  "\"last_ride_end_reason\":\"%s\"}",
                  OPENWATTS_FIRMWARE_VERSION, battery.voltage,
                  static_cast<unsigned>(battery.estimated_percent),
                  openwatts::BatteryPolicy::name(state), battery.valid ? "true" : "false",
                  usb_present ? "true" : "false",
                  openwatts::OperatingPolicy::isMaintenance(g_config) ? "Maintenance" :
                      (usb_present ? "Normal / USB" : "Battery Report"),
                  openwatts::reportReasonName(reason), ride.valid ? "true" : "false",
                  static_cast<unsigned>(ride.sequence), static_cast<unsigned>(ride.moving_seconds),
                  static_cast<unsigned>(ride.elapsed_seconds), static_cast<unsigned>(ride.crank_revolutions),
                  ride.average_power_watts, static_cast<int>(ride.maximum_power_watts),
                  ride.average_cadence_rpm, ride.maximum_cadence_rpm, ride.work_kj,
                  ride.estimated_distance_meters, ride.average_estimated_speed_mps,
                  ride.maximum_estimated_speed_mps, static_cast<unsigned>(ride.road_model_version),
                  ride.rider_mass_kg, ride.end_reason);
    const esp_err_t mqtt_err = g_mqtt.begin(g_config.mqtt_host, g_config.mqtt_port, g_config.mqtt_topic,
                                            g_mqtt_payload, g_mqtt_discovery_pending);
    g_report_history.last_attempt_seconds = static_cast<uint64_t>(now_us / 1000000LL);
    if (mqtt_err != ESP_OK) {
        ESP_LOGW(kTag, "MQTT start failed: %s", esp_err_to_name(mqtt_err));
        g_report_history.retry_pending = true;
        recordMqttFailure(now_us);
        return false;
    }
    g_mqtt_started_us = now_us;
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
        if (g_ride_log.lastRide().mqtt_publish_pending) {
            g_ride_log.markMqttPublished();
            const esp_err_t ride_err = g_settings.saveLastRide(g_ride_log.lastRide());
            if (ride_err != ESP_OK) {
                g_ride_log.markMqttPending();
                ESP_LOGW(kTag, "last ride acknowledgement save failed: %s", esp_err_to_name(ride_err));
            }
        }
        g_mqtt_consecutive_failures = 0;
        g_mqtt_next_attempt_us = 0;
    } else {
        g_report_history.retry_pending = true;
        recordMqttFailure(now_us);
    }
    g_mqtt.stop();
    g_mqtt_started_us = 0;
}

void attemptRideZero(openwatts::RideZeroTrigger trigger, const openwatts::PowerSample &sample,
                     bool imu_stationary, int64_t now_us) {
    const openwatts::RideZeroAttempt attempt = g_ride_zero.attempt(
        trigger, g_config, sample, g_ride_log.active(), imu_stationary,
        g_setup_wifi.calibrationSnapshot().active, now_us);
    if (g_config.ride_diagnostics_enabled) {
        ESP_LOGI(kTag, "Ride Zero: trigger=%s result=%s mean=%.1f stddev=%.1f range=%.1f samples=%u locked=%d",
                 openwatts::RideZeroController::triggerName(trigger),
                 openwatts::RideZeroController::resultName(attempt.result), attempt.average,
                 attempt.standard_deviation, attempt.range,
                 static_cast<unsigned>(attempt.samples), g_ride_zero.locked() ? 1 : 0);
    }
    if (attempt.result != openwatts::RideZeroResult::Accepted) return;
    openwatts::DeviceConfig candidate = g_config;
    candidate.runtime_zero_offset_counts = attempt.zero_offset;
    candidate.zero_offset_counts = attempt.zero_offset;
    // A newly accepted zero makes any sliding-zero correction already
    // accumulated against the old zero stale; otherwise it keeps getting
    // added back in and leaves a residual right after a fresh zero.
    g_power.resetSlidingZero();
    constexpr float kBaselineLearningAlpha = 0.10F;
    candidate.ride_zero_baseline_stddev_counts = candidate.ride_zero_baseline_stddev_counts > 0.0F
        ? candidate.ride_zero_baseline_stddev_counts * (1.0F - kBaselineLearningAlpha) +
              attempt.standard_deviation * kBaselineLearningAlpha
        : attempt.standard_deviation;
    candidate.ride_zero_baseline_range_counts = candidate.ride_zero_baseline_range_counts > 0.0F
        ? candidate.ride_zero_baseline_range_counts * (1.0F - kBaselineLearningAlpha) +
              attempt.range * kBaselineLearningAlpha
        : attempt.range;
    // Stationary tracking can run for hours in Maintenance Mode. Apply those
    // trusted corrections in RAM without consuming NVS erase/write cycles.
    // Persist only at lifecycle boundaries that establish the next ride's
    // reference or precede shutdown.
    const bool persist = trigger != openwatts::RideZeroTrigger::Stationary &&
                         trigger != openwatts::RideZeroTrigger::BleConnected;
    const esp_err_t err = persist ? g_settings.save(candidate) : ESP_OK;
    if (err == ESP_OK) {
        g_config = candidate;
        g_power.updateConfig(g_config);
        g_power.reset();
    } else {
        ESP_LOGW(kTag, "Ride Zero%s failed: %s", persist ? " save" : " update", esp_err_to_name(err));
    }
}
}  // namespace

extern "C" void app_main() {
    ESP_LOGI(kTag, "OpenWatts firmware %s", OPENWATTS_FIRMWARE_VERSION);
    g_setup_wifi.setPowerSource(&g_power);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(g_settings.begin());
    g_config = g_settings.load();
    g_ride_log.begin(g_settings.loadLastRide());
    esp_log_level_set(kTag, g_config.debug_logging_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
    ESP_LOGI(kTag, "Operating Mode: %s", openwatts::OperatingPolicy::name(g_config.operating_mode));

    ESP_ERROR_CHECK(openwatts::board::initPins());
    ESP_ERROR_CHECK(openwatts::ledStatus().begin());
    ESP_ERROR_CHECK(openwatts::board::initI2c());
    ESP_ERROR_CHECK(openwatts::board::initBatteryAdc());

    g_cadence.updateConfig(g_config);
    g_power.updateConfig(g_config);
    g_power_manager.updateConfig(g_config);
    g_battery_policy.updateConfig(g_config);

    ESP_ERROR_CHECK(g_hx711.begin());

    err = g_imu.begin();
    const bool imu_start_failed = err != ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "IMU init failed; cadence unavailable: %s", esp_err_to_name(err));
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

    err = g_ble.begin(g_config.ble_device_name, g_config.ble_advertising_power_dbm);
    const bool fatal_startup_error = imu_start_failed || err != ESP_OK;
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
    bool previous_ble_connected = false;
    int64_t usb_removed_zero_due_us = 0;
    // A reboot while USB remains connected has no GPIO edge. Schedule the
    // same stable-zero opportunity explicitly after enough fresh HX711 data.
    int64_t usb_inserted_zero_due_us = usb_present ? esp_timer_get_time() + 4000000LL : 0;
    int64_t ble_zero_due_us = 0;
    // Sliding-zero baseline learning is deliberately deferred past the first
    // stretch of pedaling: SmartSpin2K's own spin-down/handshake makes early
    // revolutions unrepresentative of steady-state riding.
    int64_t sliding_zero_warmup_due_us = 0;
    int64_t last_imu_motion_us = esp_timer_get_time();
    int64_t stationary_since_us = 0;
    bool sleep_report_attempted = false;
    // Normal Mode may bring up the optional dashboard once the existing ride
    // logger qualifies a ride.  This RAM-only latch survives cadence pauses
    // and remains set until the ordinary post-ride reporting/sleep boundary.
    bool ride_wifi_latched = false;
    bool ride_wifi_start_attempted = false;
    bool usb_ride_finalize_pending = false;
    while (true) {
        if (g_timer_decision_pending) {
            openwatts::ledStatus().setSleeping(true);
            const int64_t timer_now_us = esp_timer_get_time();
            const bool timer_usb_present = openwatts::board::usbPresent();
            status_battery = readBattery();
            const openwatts::BatteryState timer_state = g_battery_policy.qualify(status_battery);
            const openwatts::ReportReason timer_reason = openwatts::decideBatteryReport(
                g_config, status_battery, timer_state, g_report_history,
                static_cast<uint64_t>(timer_now_us / 1000000LL));

            finishBatteryReport(status_battery, timer_state, timer_now_us);
            if (!g_mqtt.running() && timer_reason != openwatts::ReportReason::None &&
                g_config.hasWifiCredentials() && g_config.mqtt_battery_notifications_enabled &&
                timer_now_us >= g_mqtt_next_attempt_us) {
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
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        openwatts::ledStatus().setSleeping(false);

        if (g_setup_wifi.consumeConfigChanged()) {
            g_cadence.updateConfig(g_config);
            g_power.updateConfig(g_config);
            g_power_manager.updateConfig(g_config);
            g_battery_policy.updateConfig(g_config);
            esp_log_level_set("hx711", g_config.debug_logging_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
            esp_log_level_set("imu", g_config.debug_logging_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
            esp_log_level_set("power", g_config.debug_logging_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
            esp_log_level_set(kTag, g_config.debug_logging_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
            ESP_LOGI(kTag, "runtime settings applied");
        }

        int32_t raw_counts = g_hx711.lastRawCounts();
        const openwatts::Hx711PollResult hx_result = g_hx711.poll(raw_counts);
        const bool hx_attempted = hx_result != openwatts::Hx711PollResult::Waiting;
        const bool hx_ok = hx_result == openwatts::Hx711PollResult::Sample;
        if (hx_attempted) {
            g_hx711.observe(hx_ok, raw_counts, g_config.hx711_smoothing);
        }

        openwatts::ImuSample imu_sample{};
        g_imu.read(imu_sample);

        const int64_t now_us = esp_timer_get_time();
        if (imu_sample.valid) {
            const float maximum_gyro = std::max(std::fabs(imu_sample.gyro_dps[0]),
                std::max(std::fabs(imu_sample.gyro_dps[1]), std::fabs(imu_sample.gyro_dps[2])));
            if (maximum_gyro > 3.0F) last_imu_motion_us = now_us;
        }
        const bool imu_stationary = imu_sample.valid && now_us - last_imu_motion_us >= 2000000LL;
        const bool current_usb_present = openwatts::board::usbPresent();
        openwatts::ledStatus().setAutomaticState(
            current_usb_present, openwatts::OperatingPolicy::permitsMaintenanceTools(g_config),
            g_ble.connected(), !g_config.strain_calibration_valid, fatal_startup_error);
        g_setup_wifi.observeCalibration(hx_attempted, hx_ok, raw_counts, g_hx711.ready(), now_us);
        if (g_setup_wifi.consumeImuTrackerReset()) {
            g_cadence.reset();
            g_provisional_angle = 0.0F;
            g_provisional_total_degrees = 0.0F;
            g_provisional_revolutions = 0;
            g_last_provisional_imu_us = now_us;
        }
        if (current_usb_present != previous_usb_present) {
            previous_usb_present = current_usb_present;
            ESP_LOGI(kTag, "USB %s", current_usb_present ? "inserted" : "removed");
            g_pending_report_reason = current_usb_present ? openwatts::ReportReason::UsbConnected
                                                          : openwatts::ReportReason::UsbDisconnected;
            if (current_usb_present) {
                g_ride_zero.resetLifecycle();
                sliding_zero_warmup_due_us = 0;
                usb_ride_finalize_pending = g_ride_log.active();
                ride_wifi_latched = false;
                ride_wifi_start_attempted = false;
                usb_removed_zero_due_us = 0;
                usb_inserted_zero_due_us = now_us + 2000000LL;
                // Consume USB_CONNECTED promptly so a just-finalized ride is
                // included in the first available USB reporting cycle.
                last_battery_policy_check_us = -5LL * 1000LL * 1000LL;
                const esp_err_t wifi_err = g_setup_wifi.active() ? ESP_OK :
                    g_setup_wifi.begin(g_config, g_settings, true, false);
                if (wifi_err != ESP_OK) {
                    ESP_LOGW(kTag, "Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                }
            } else {
                usb_removed_zero_due_us = now_us + 2000000LL;
                // Keep the already-running dashboard available during the
                // possible-ride window. If no ride begins, the ordinary
                // Normal Mode inactivity path shuts Wi-Fi down before sleep.
                ride_wifi_latched = true;
                ride_wifi_start_attempted = g_setup_wifi.active();
                if (!g_setup_wifi.active() && g_config.hasWifiCredentials()) {
                    ride_wifi_start_attempted = true;
                    const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, false, false, true);
                    if (wifi_err != ESP_OK) {
                        ESP_LOGW(kTag, "post-USB dashboard Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                    }
                }
                ESP_LOGI(kTag, "USB removed; dashboard remains available until ride completion or sleep");
            }
        }
        // A Settings save can change Operating Mode without a USB edge.
        // Honor Normal Mode promptly after the response has been delivered.
        if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present) &&
            !ride_wifi_latched &&
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
        g_ride_zero.observe(g_hx711.filtered(), hx_ok, now_us);
        if (usb_removed_zero_due_us != 0 && now_us >= usb_removed_zero_due_us) {
            attemptRideZero(openwatts::RideZeroTrigger::UsbRemoved, sample, imu_stationary, now_us);
            usb_removed_zero_due_us = 0;
        }
        if (usb_inserted_zero_due_us != 0 && now_us >= usb_inserted_zero_due_us) {
            attemptRideZero(openwatts::RideZeroTrigger::UsbInserted, sample, imu_stationary, now_us);
            usb_inserted_zero_due_us = 0;
        }
        const bool ble_connected = g_ble.connected();
        if (ble_connected != previous_ble_connected) {
            // Connecting an app commonly coincides with clipping in or moving
            // the crank. Observe a quiet window before judging Ride Zero.
            ble_zero_due_us = now_us + 3000000LL;
            if (!ble_connected) {
                g_ride_zero.resetLifecycle();
                sliding_zero_warmup_due_us = 0;
            }
            previous_ble_connected = ble_connected;
        }
        if (ble_zero_due_us != 0 && now_us >= ble_zero_due_us) {
            attemptRideZero(ble_connected ? openwatts::RideZeroTrigger::BleConnected
                                          : openwatts::RideZeroTrigger::BleDisconnected,
                            sample, imu_stationary, now_us);
            ble_zero_due_us = 0;
        }
        if (sliding_zero_warmup_due_us != 0 && now_us >= sliding_zero_warmup_due_us) {
            g_power.resetSlidingZero();
            sliding_zero_warmup_due_us = 0;
        }
        if (g_config.strain_calibration_valid && cadence.moving) {
            if (!g_ride_zero.locked()) {
                // Defer baseline learning past SmartSpin2K's own spin-down/
                // handshake so it isn't trained on non-steady-state pedaling.
                sliding_zero_warmup_due_us =
                    now_us + static_cast<int64_t>(g_config.sliding_zero_warmup_seconds) * 1000000LL;
            }
            g_ride_zero.lock();
            stationary_since_us = 0;
        } else if (!cadence.moving) {
            if (stationary_since_us == 0) stationary_since_us = now_us;
            const bool continuous_zero_allowed = current_usb_present ||
                openwatts::OperatingPolicy::isMaintenance(g_config);
            if (continuous_zero_allowed && now_us - stationary_since_us >=
                    static_cast<int64_t>(g_config.ride_zero_stationary_timeout_seconds) * 1000000LL) {
                attemptRideZero(openwatts::RideZeroTrigger::Stationary, sample, imu_stationary, now_us);
                // Continue checking only at the configured interval. This
                // follows slow unloaded thermal drift without chasing zero
                // every sample; cadence immediately locks the lifecycle.
                stationary_since_us = now_us;
            }
        }
        if (g_config.ride_detection_enabled && g_config.strain_calibration_valid) {
            bool ride_completed = false;
            if (usb_ride_finalize_pending && !cadence.moving) {
                ride_completed = g_ride_log.finishForUsbConnection(now_us);
                usb_ride_finalize_pending = false;
                if (ride_completed) ESP_LOGI(kTag, "qualified ride finalized by USB connection");
            }
            if (!ride_completed) {
                ride_completed = g_ride_log.update(sample, now_us, g_config.minimum_ride_duration_seconds,
                                                   g_config.rider_mass_kg);
            }
            if (ride_completed) {
                g_ride_zero.resetLifecycle();
                sliding_zero_warmup_due_us = 0;
            }
            if (g_ride_log.completedPendingSave()) {
                const esp_err_t ride_err = g_settings.saveLastRide(g_ride_log.lastRide());
                if (ride_err == ESP_OK) {
                    g_ride_log.markSaved();
                    // The durable flag is consumed only when ordinary Normal
                    // Mode sleep is due. Saving a ride must not start Wi-Fi or
                    // prevent the inactivity timer from reaching that point.
                    sleep_report_attempted = false;
                }
                else ESP_LOGW(kTag, "last ride save failed: %s", esp_err_to_name(ride_err));
            }
        }
        if (g_ride_log.candidate() && !ride_wifi_latched) {
            ride_wifi_latched = true;
            if (!g_setup_wifi.active() && !ride_wifi_start_attempted && g_config.hasWifiCredentials()) {
                ride_wifi_start_attempted = true;
                const esp_err_t wifi_err = g_setup_wifi.begin(g_config, g_settings, current_usb_present, false, true);
                if (wifi_err != ESP_OK) {
                    // The dashboard is optional.  Never retry in the sampling
                    // loop or let a network failure disturb this ride.
                    ESP_LOGW(kTag, "ride dashboard Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
                } else {
                    ESP_LOGI(kTag, "ride candidate detected; ride dashboard available");
                }
            }
        }
        if (now_us - last_status_battery_us >= 1000LL * 1000LL) {
            last_status_battery_us = now_us;
            status_battery = readBattery();
        }
        float current_ride_speed_mps = 0.0F;
        if (sample.valid && sample.cadence_rpm > 0.0F) {
            const float modeled_speed = openwatts::RoadModel::speedMetersPerSecond(
                sample.power_watts, g_config.rider_mass_kg);
            if (std::isfinite(modeled_speed) && modeled_speed > 0.0F) {
                current_ride_speed_mps = modeled_speed;
            }
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
            .revolutions = cadence.revolutions,
            .cadence_corrected_gyro_z_dps = cadence.corrected_gyro_z_dps,
            .cadence_forward_velocity_dps = cadence.forward_velocity_dps,
            .cadence_integrated_angle_degrees = cadence.integrated_forward_angle_degrees,
            .cadence_reverse_angle_degrees = cadence.integrated_reverse_angle_degrees,
            .cadence_last_candidate_rpm = cadence.last_candidate_rpm,
            .cadence_imu_invalid_reads = cadence.imu_invalid_reads,
            .cadence_rejected_revolutions = cadence.rejected_revolution_periods,
            .cadence_integration_gap_count = cadence.integration_gap_count,
            .cadence_last_sample_interval_ms = cadence.last_sample_interval_ms,
            .cadence_dropout_count = cadence.dropout_count,
            .cadence_last_revolution_age_ms = cadence.last_revolution_us > 0
                ? static_cast<uint32_t>((now_us - cadence.last_revolution_us) / 1000LL) : 0,
            .cadence_last_dropout_reason = openwatts::cadenceDropoutReasonName(cadence.last_dropout_reason),
            .cadence_last_dropout_age_ms = cadence.last_dropout_us > 0
                ? static_cast<uint32_t>((now_us - cadence.last_dropout_us) / 1000LL) : 0,
            .ble_last_notify_result = g_ble.lastNotifyResult(),
            .ble_notify_success_count = g_ble.notifySuccessCount(),
            .ble_notify_failure_count = g_ble.notifyFailureCount(),
            .ble_last_power_watts = g_ble.lastNotifiedPowerWatts(),
            .ble_last_cadence_rpm = g_ble.lastNotifiedCadenceRpm(),
            .ble_last_crank_revolutions = g_ble.lastNotifiedCrankRevolutions(),
            .ble_last_crank_event_time = g_ble.lastNotifiedCrankEventTime(),
            .ble_last_notify_age_ms = g_ble.lastNotifyUs() > 0
                ? static_cast<uint32_t>((now_us - g_ble.lastNotifyUs()) / 1000LL) : 0,
            .ble_measurement_subscribed = g_ble.measurementSubscribed(),
            .ble_transmit_event_count = g_ble.transmitEventCount(),
            .ble_transmit_error_count = g_ble.transmitErrorCount(),
            .ble_last_transmit_status = g_ble.lastTransmitStatus(),
            .ble_disconnect_count = g_ble.disconnectCount(),
            .ble_last_disconnect_reason = g_ble.lastDisconnectReason(),
            .ble_connection_interval_units = g_ble.connectionIntervalUnits(),
            .ble_connection_rssi_dbm = g_ble.connectionRssiDbm(),
            .hx711_failures = g_hx711.readFailures(),
            .hx711_noise = g_hx711.noiseEstimate(),
            .hx711_sample_rate_hz = g_hx711.sampleRateHz(),
            .wake_reason = wakeReason(), .reset_reason = resetReason(),
            .provisional_angle_degrees = g_provisional_angle,
            .provisional_angular_velocity_dps = provisional_velocity,
            .provisional_cadence_rpm = provisional_cadence,
            .provisional_revolutions = g_provisional_revolutions,
            .provisional_confidence = provisional_confidence,
            .provisional_reason = provisional_reason, .motion_detected = motion_detected,
            .last_ride = g_ride_log.lastRide(), .ride_candidate = g_ride_log.candidate(),
            .ride_active = g_ride_log.active(),
            .current_ride_moving_seconds = g_ride_log.currentMovingSeconds(),
            .current_ride_distance_meters = g_ride_log.currentDistanceMeters(),
            .current_ride_speed_mps = current_ride_speed_mps,
        });

        // Reporting is independent from battery sampling.  It can wake the
        // radio only when a valid report is due, never for a percentage-only
        // dashboard change.
        if (g_mqtt.running() && g_mqtt.complete()) {
            finishBatteryReport(status_battery, g_battery_policy.state(), now_us);
            if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present) && !ride_wifi_latched) {
                g_setup_wifi.stop();
            }
        } else if (g_mqtt.running() && g_mqtt_started_us != 0 &&
                   now_us - g_mqtt_started_us >= kSleepReportTimeoutUs) {
            // A client that never reaches a terminal event must not be left
            // running indefinitely -- it can retry internally and exhaust
            // sockets shared with the web server. This mirrors the pre-sleep
            // deadline below but applies at all times, not only when about
            // to sleep.
            ESP_LOGW(kTag, "MQTT attempt exceeded watchdog; forcing stop");
            g_mqtt.stop();
            g_mqtt_started_us = 0;
            g_report_history.retry_pending = true;
            recordMqttFailure(now_us);
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
                g_config.hasWifiCredentials() && g_config.mqtt_battery_notifications_enabled &&
                now_us >= g_mqtt_next_attempt_us) {
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
            ESP_LOGD(kTag, "sample raw=%" PRId32 " filtered=%.1f noise=%.1f rpm=%.1f torque=%.2f power=%d valid=%d",
                     sample.raw_counts, sample.filtered_counts, sample.noise_estimate, sample.cadence_rpm,
                     sample.torque_nm, sample.power_watts, sample.valid ? 1 : 0);
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
        // A qualified ride owns the runtime until its stationary completion
        // window closes. Once persisted, the pending summary is given one
        // bounded publish attempt when this ordinary sleep path becomes due.
        const bool ride_finalize_pending = g_ride_log.active();
        const bool sleep_due = openwatts::OperatingPolicy::permitsInactivitySleep(g_config) &&
            !current_usb_present && !g_ble.connected() && !ride_finalize_pending &&
            g_power_manager.shouldSleepForInactivity(cadence, now_us);
        if (sleep_due) {
            const bool ride_report_pending = g_ride_log.lastRide().valid &&
                g_ride_log.lastRide().mqtt_publish_pending;
            if (g_mqtt.running()) {
                if (g_mqtt_started_us != 0 && now_us - g_mqtt_started_us < kSleepReportTimeoutUs) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                ESP_LOGW(kTag, "pre-sleep MQTT deadline expired; retaining pending ride");
                g_mqtt.stop();
                g_mqtt_started_us = 0;
                g_report_history.retry_pending = true;
            } else if (ride_report_pending && !sleep_report_attempted &&
                       g_config.hasWifiCredentials() && g_config.mqtt_battery_notifications_enabled) {
                sleep_report_attempted = true;
                const openwatts::BatteryState state = g_battery_policy.qualify(status_battery);
                const esp_err_t wifi_err = g_setup_wifi.active() ? ESP_OK :
                    g_setup_wifi.begin(g_config, g_settings, false, false, true);
                if (wifi_err == ESP_OK &&
                    startBatteryReport(status_battery, state, openwatts::ReportReason::Manual, false, now_us)) {
                    ESP_LOGI(kTag, "bounded pre-sleep Last Ride report started");
                    continue;
                }
                ESP_LOGW(kTag, "pre-sleep Last Ride report could not start; retaining pending ride");
                g_report_history.retry_pending = true;
            }
            ride_wifi_latched = false;
            ride_wifi_start_attempted = false;
            if (!openwatts::OperatingPolicy::permitsWifi(g_config, current_usb_present)) g_setup_wifi.stop();
            attemptRideZero(openwatts::RideZeroTrigger::BeforeSleep, sample, imu_stationary, now_us);
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
        vTaskDelay(pdMS_TO_TICKS(g_config.sample_interval_ms));
    }
}
