#include "setup_wifi.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <string>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "board.h"
#include "logo_asset.h"
#include "led_status.h"
#include "operating_mode.h"
#include "settings_storage.h"
#include "web_ui.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "setup_wifi";
constexpr char kSetupSsid[] = "OpenWatts-Setup";
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr float kMinimumBatteryOtaVoltage = 3.75F;

bool g_wifi_initialized = false;
httpd_handle_t g_httpd = nullptr;
TaskHandle_t g_dns_task = nullptr;
SetupWifi *g_portal = nullptr;
esp_timer_handle_t g_station_retry_timer = nullptr;
char g_status_json[7600]{};

struct ImuCaptureSample {
    uint32_t elapsed_ms;
    float accel[3];
    float gyro[3];
};

constexpr size_t kImuCaptureCapacity = 1200;
constexpr int64_t kImuCaptureIntervalUs = 50000;
std::array<ImuCaptureSample, kImuCaptureCapacity> g_imu_capture{};
std::atomic<bool> g_imu_capture_active{false};
std::atomic<size_t> g_imu_capture_count{0};
int64_t g_imu_capture_started_us = 0;
int64_t g_imu_capture_last_sample_us = 0;

esp_err_t sendPage(httpd_req_t *req, const char *page) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t logoHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "image/svg+xml; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, webui::kLogoSvg, HTTPD_RESP_USE_STRLEN);
}

void rebootAfterOta(void *) {
    ESP_LOGI(kTag, "OTA complete; restarting into updated firmware");
    esp_restart();
}

void scheduleOtaReboot() {
    esp_timer_create_args_t args{.callback = &rebootAfterOta, .arg = nullptr,
                                 .dispatch_method = ESP_TIMER_TASK, .name = "ota_reboot",
                                 .skip_unhandled_events = false};
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 750000);
    }
}

esp_err_t otaUploadHandler(httpd_req_t *req) {
    const bool usb_present = board::usbPresent();
    const LiveStatus status = g_portal != nullptr ? g_portal->liveStatus() : LiveStatus{};
    const DeviceConfig *config = g_portal != nullptr ? g_portal->mutableConfig() : nullptr;
    const bool safe_battery_ota = config != nullptr && OperatingPolicy::isMaintenance(*config) &&
                                  status.battery_valid && std::isfinite(status.battery_voltage) &&
                                  status.battery_voltage >= kMinimumBatteryOtaVoltage;
    if (!usb_present && !safe_battery_ota) {
        return httpd_resp_send_err(
            req, HTTPD_403_FORBIDDEN,
            "OTA requires USB, or Maintenance Mode with a valid battery at or above 3.75 V");
    }
    if (req->content_len < 32) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image is empty or invalid");
    }

    struct OtaLedGuard {
        bool keep_active = false;
        OtaLedGuard() { ledStatus().setOtaActive(true); }
        ~OtaLedGuard() { if (!keep_active) ledStatus().setOtaActive(false); }
    } ota_led;

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr || req->content_len > static_cast<int>(partition->size)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image exceeds the update partition");
    }

    esp_ota_handle_t update{};
    esp_err_t err = esp_ota_begin(partition, req->content_len, &update);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start OTA update");
    }

    uint8_t buffer[1024];
    int received = 0;
    bool image_header_checked = false;
    while (received < req->content_len) {
        const int wanted = std::min<int>(sizeof(buffer), req->content_len - received);
        const int got = httpd_req_recv(req, reinterpret_cast<char *>(buffer), wanted);
        if (got <= 0) {
            esp_ota_abort(update);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware upload interrupted");
        }
        if (!image_header_checked) {
            image_header_checked = true;
            if (buffer[0] != kEspImageMagic) {
                esp_ota_abort(update);
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not an ESP firmware binary");
            }
        }
        err = esp_ota_write(update, buffer, static_cast<size_t>(got));
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(update);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware write failed");
        }
        received += got;
    }

    err = esp_ota_end(update);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA image validation failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image validation failed");
    }
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA boot-partition selection failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not activate firmware image");
    }

    ESP_LOGI(kTag, "OTA wrote %d bytes to %s", received, partition->label);
    ota_led.keep_active = true;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Update installed. Restarting now.\"}");
    scheduleOtaReboot();
    return ESP_OK;
}

esp_err_t otaPageHandler(httpd_req_t *req) {
    return sendPage(req, webui::kOtaPage);
}

void wifiEventLog(void *, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            const auto *event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
            ESP_LOGI(kTag, "setup client joined %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            const auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
            ESP_LOGW(kTag, "setup client left %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
            ESP_LOGW(kTag, "saved Wi-Fi connection ended (reason=%u)", static_cast<unsigned>(event->reason));
            if (g_station_retry_timer != nullptr) {
                esp_timer_stop(g_station_retry_timer);
                esp_timer_start_once(g_station_retry_timer, 3LL * 1000LL * 1000LL);
            }
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(kTag, "saved Wi-Fi associated; waiting for DHCP");
            if (g_station_retry_timer != nullptr) esp_timer_stop(g_station_retry_timer);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        const auto *event = static_cast<ip_event_ap_staipassigned_t *>(event_data);
        ESP_LOGI(kTag, "setup client received DHCP address " IPSTR, IP2STR(&event->ip));
    }
}

void retrySavedStation(void *) {
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) ESP_LOGW(kTag, "saved Wi-Fi retry did not start: %s", esp_err_to_name(err));
}

std::string urlDecode(const char *input) {
    std::string out;
    if (input == nullptr) {
        return out;
    }
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] == '+') {
            out.push_back(' ');
        } else if (input[i] == '%' && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            char hex[] = {input[i + 1], input[i + 2], '\0'};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string formValue(const std::string &body, const char *key) {
    const std::string prefix = std::string(key) + "=";
    size_t pos = body.find(prefix);
    if (pos == std::string::npos) {
        return {};
    }
    pos += prefix.size();
    const size_t end = body.find('&', pos);
    return urlDecode(body.substr(pos, end == std::string::npos ? std::string::npos : end - pos).c_str());
}

bool parseUnsigned(const std::string &text, uint32_t minimum, uint32_t maximum, uint32_t &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool parseFloat(const std::string &text, float minimum, float maximum, float &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed) ||
        parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool validBleName(const std::string &name) {
    if (name.empty() || name.size() >= sizeof(DeviceConfig{}.ble_device_name)) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == ' ' || ch == '-' || ch == '_';
    });
}

esp_err_t badSetting(httpd_req_t *req, const char *message) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
}

esp_err_t diagnosticsHandler(httpd_req_t *req) {
    return sendPage(req, webui::kDiagnosticsPage);
}

esp_err_t settingsHandler(httpd_req_t *req) {
    return sendPage(req, webui::kSettingsPage);
}

esp_err_t ensureWifiInitialized() {
    if (g_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    esp_timer_create_args_t retry_args{.callback = &retrySavedStation, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                       .name = "wifi_retry", .skip_unhandled_events = false};
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &g_station_retry_timer), kTag, "create station retry timer");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifiEventLog, nullptr), kTag,
                        "register AP join event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &wifiEventLog, nullptr), kTag,
                        "register AP leave event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifiEventLog, nullptr), kTag,
                        "register STA connected event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifiEventLog, nullptr), kTag,
                        "register STA disconnected event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifiEventLog, nullptr), kTag,
                        "register DHCP event");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return err;
    }
    g_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t rootHandler(httpd_req_t *req) {
    return sendPage(req, webui::kStatusPage);
}

esp_err_t ridePageHandler(httpd_req_t *req) {
    return sendPage(req, webui::kRidePage);
}

std::string requestBody(httpd_req_t *req);

esp_err_t trainerTestPageHandler(httpd_req_t *req) {
    return sendPage(req, webui::kTrainerTestPage);
}

esp_err_t trainerTestLeaseHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }
    const std::string body = requestBody(req);
    const std::string active = formValue(body, "active");
    if (active != "0" && active != "1") {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "active must be 0 or 1");
    }
    if (active == "1") {
        const LiveStatus status = g_portal->liveStatus();
        const bool power_permitted = status.usb_present ||
            OperatingPolicy::isMaintenance(*g_portal->mutableConfig());
        if (!power_permitted || !status.strain_calibration_valid || status.ride_active || status.ride_candidate) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "Trainer Test requires USB or Maintenance, calibration, and no active ride");
        }
        g_portal->setTrainerTestActive(true);
    } else {
        g_portal->setTrainerTestActive(false);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, active == "1"
        ? "{\"ok\":true,\"active\":true,\"message\":\"Trainer Test active; normal ride recording paused\"}"
        : "{\"ok\":true,\"active\":false,\"message\":\"Trainer Test ended\"}");
}

esp_err_t statusHandler(httpd_req_t *req) {
    if (g_portal == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    const LiveStatus s = g_portal->liveStatus();
    const DeviceConfig &c = *g_portal->mutableConfig();
    const CalibrationSnapshot cal = g_portal->calibrationSnapshot();
    const char *signal = !s.hx711_ready ? "signal_unavailable" :
                         std::abs(s.raw_counts) >= 8388000 ? "saturated_output" :
                         s.hx711_noise > 6000 ? "signal_unstable" :
                         c.strain_calibration_valid ? "calibrated" : "signal_present_not_calibrated";
    std::snprintf(g_status_json, sizeof(g_status_json),
                  "{\"uptime\":%u,\"battery_voltage\":%.3f,\"battery_percent\":%u,\"battery_valid\":%s,"
                  "\"usb\":%s,\"charging\":%s,\"operating_mode\":\"%s\",\"ble\":%s,\"imu\":%s,\"imu_interrupt\":%s,\"imu_whoami\":%u,"
                  "\"accel\":[%.3f,%.3f,%.3f],\"gyro\":[%.2f,%.2f,%.2f],\"hx\":%s,\"calibrated\":%s,\"raw\":%ld,"
                  "\"filtered\":%.2f,\"torque\":%.3f,\"cadence\":%.2f,\"power\":%d,\"revolutions\":%u,\"failures\":%u,"
                  "\"cadence_diagnostics\":{\"corrected_gyro_z_dps\":%.2f,\"forward_velocity_dps\":%.2f,"
                  "\"integrated_angle_degrees\":%.2f,\"reverse_angle_degrees\":%.2f,\"last_candidate_rpm\":%.2f,"
                  "\"imu_invalid_reads\":%u,\"rejected_revolutions\":%u,\"integration_gap_count\":%u,"
                  "\"last_sample_interval_ms\":%.2f,\"dropout_count\":%u,"
                  "\"last_revolution_age_ms\":%u,\"last_dropout_reason\":\"%s\",\"last_dropout_age_ms\":%u},"
                  "\"ble_cps_diagnostics\":{\"last_notify_result\":%d,\"notify_success_count\":%u,"
                  "\"notify_failure_count\":%u,\"last_power_watts\":%d,\"last_cadence_rpm\":%.2f,"
                  "\"last_crank_revolutions\":%u,\"last_crank_event_time\":%u,\"last_notify_age_ms\":%u,"
                  "\"measurement_subscribed\":%s,\"transmit_event_count\":%u,\"transmit_error_count\":%u,"
                  "\"last_transmit_status\":%d,\"disconnect_count\":%u,\"last_disconnect_reason\":%d,"
                  "\"connection_interval_ms\":%.2f,\"connection_rssi_dbm\":%d},"
                  "\"hx711_noise\":%.2f,\"hx711_sample_rate_hz\":%.2f,\"strain_signal\":\"%s\","
                  "\"maintenance_tools\":%s,"
                  "\"wake_reason\":\"%s\",\"reset_reason\":\"%s\","
                  "\"motion_detected\":%s,\"provisional_angle_degrees\":%.2f,"
                  "\"provisional_angular_velocity_dps\":%.2f,\"provisional_cadence_rpm\":%.2f,"
                  "\"provisional_revolutions\":%u,\"provisional_confidence\":%.3f,"
                  "\"provisional_reason\":\"%s\","
                  "\"ride_candidate\":%s,\"ride_active\":%s,"
                  "\"current_ride_moving_seconds\":%u,\"current_ride_distance_meters\":%.2f,"
                  "\"current_ride_speed_mps\":%.3f,\"last_ride\":{\"valid\":%s,\"sequence\":%u,"
                  "\"moving_seconds\":%u,\"elapsed_seconds\":%u,\"revolutions\":%u,"
                  "\"average_power\":%.1f,\"maximum_power\":%d,\"average_cadence\":%.1f,"
                  "\"maximum_cadence\":%.1f,\"work_kj\":%.2f,\"end_reason\":\"%s\","
                  "\"estimated_distance_meters\":%.2f,\"average_estimated_speed_mps\":%.3f,"
                  "\"maximum_estimated_speed_mps\":%.3f,\"road_model_version\":%u,\"rider_mass_kg\":%.2f},"
                  "\"calibration\":{\"step\":\"%s\",\"result\":\"%s\",\"error\":\"%s\",\"active\":%s,\"valid\":%s,"
                  "\"mass_kg\":%.4f,\"lever_arm_mm\":%.2f,\"reference_torque_nm\":%.4f,\"raw_delta\":%.2f,"
                  "\"counts_per_nm\":%.4f,\"nm_per_count\":%.9f,\"verification_torque_nm\":%.4f,\"verification_error_percent\":%.2f,"
                  "\"zero_samples\":%u,\"loaded_samples\":%u,\"zero_noise\":%.2f,\"loaded_noise\":%.2f},"
                   "\"config\":{\"wifi_ssid\":\"%s\",\"operating_mode\":\"%s\","
                   "\"mqtt_enabled\":%s,\"mqtt_host\":\"%s\",\"mqtt_port\":%u,\"mqtt_topic\":\"%s\","
                   "\"light_sleep_enabled\":%s,\"inactivity_timeout_ms\":%u,\"ble_device_name\":\"%s\","
                   "\"ride_diagnostics_enabled\":%s,\"debug_logging_enabled\":%s,"
                   "\"power_filter_alpha\":%.3f,\"maximum_valid_power_watts\":%u,"
                   "\"auto_ride_zero_enabled\":%s,\"ride_detection_enabled\":%s,"
                   "\"minimum_ride_duration_seconds\":%u,\"cadence_timeout_seconds\":%u,"
                   "\"ride_zero_stationary_timeout_seconds\":%u,"
                   "\"ride_zero_baseline_stddev_counts\":%.2f,\"ride_zero_baseline_range_counts\":%.2f,"
                   "\"unit_system\":\"%s\",\"rider_mass_kg\":%.2f,"
                   "\"imu_wake_threshold\":%u,\"imu_revolution_threshold_dps\":%.1f,"
                   "\"minimum_cadence_rpm\":%u,\"maximum_cadence_rpm\":%u,"
                   "\"ble_advertising_power_dbm\":%d,"
                   "\"zero_offset\":%ld,\"calibration_zero\":%ld,"
                   "\"counts_per_nm\":%.3f,\"torque_sign\":%ld,\"calibration_mass_kg\":%.4f,"
                   "\"calibration_lever_arm_mm\":%.2f}}",
                  static_cast<unsigned>(s.uptime_seconds), static_cast<double>(s.battery_voltage),
                  static_cast<unsigned>(s.battery_percent), s.battery_valid ? "true" : "false",
                  s.usb_present ? "true" : "false", s.charging ? "true" : "false",
                  OperatingPolicy::value(c.operating_mode), s.ble_connected ? "true" : "false",
                  s.imu_ready ? "true" : "false", s.imu_interrupt_active ? "true" : "false",
                  static_cast<unsigned>(s.imu_who_am_i), static_cast<double>(s.imu_accel_g[0]),
                  static_cast<double>(s.imu_accel_g[1]), static_cast<double>(s.imu_accel_g[2]),
                  static_cast<double>(s.imu_gyro_dps[0]), static_cast<double>(s.imu_gyro_dps[1]),
                  static_cast<double>(s.imu_gyro_dps[2]), s.hx711_ready ? "true" : "false",
                  s.strain_calibration_valid ? "true" : "false", static_cast<long>(s.raw_counts),
                  static_cast<double>(s.filtered_counts), static_cast<double>(s.torque_nm), static_cast<double>(s.cadence_rpm),
                  static_cast<int>(s.power_watts), static_cast<unsigned>(s.revolutions), static_cast<unsigned>(s.hx711_failures),
                  static_cast<double>(s.cadence_corrected_gyro_z_dps),
                  static_cast<double>(s.cadence_forward_velocity_dps),
                  static_cast<double>(s.cadence_integrated_angle_degrees),
                  static_cast<double>(s.cadence_reverse_angle_degrees),
                  static_cast<double>(s.cadence_last_candidate_rpm),
                  static_cast<unsigned>(s.cadence_imu_invalid_reads),
                  static_cast<unsigned>(s.cadence_rejected_revolutions),
                  static_cast<unsigned>(s.cadence_integration_gap_count),
                  static_cast<double>(s.cadence_last_sample_interval_ms),
                  static_cast<unsigned>(s.cadence_dropout_count),
                  static_cast<unsigned>(s.cadence_last_revolution_age_ms), s.cadence_last_dropout_reason,
                  static_cast<unsigned>(s.cadence_last_dropout_age_ms),
                  s.ble_last_notify_result, static_cast<unsigned>(s.ble_notify_success_count),
                  static_cast<unsigned>(s.ble_notify_failure_count), static_cast<int>(s.ble_last_power_watts),
                  static_cast<double>(s.ble_last_cadence_rpm),
                  static_cast<unsigned>(s.ble_last_crank_revolutions),
                  static_cast<unsigned>(s.ble_last_crank_event_time),
                  static_cast<unsigned>(s.ble_last_notify_age_ms),
                  s.ble_measurement_subscribed ? "true" : "false",
                  static_cast<unsigned>(s.ble_transmit_event_count),
                  static_cast<unsigned>(s.ble_transmit_error_count), s.ble_last_transmit_status,
                  static_cast<unsigned>(s.ble_disconnect_count), s.ble_last_disconnect_reason,
                  static_cast<double>(s.ble_connection_interval_units) * 1.25,
                  static_cast<int>(s.ble_connection_rssi_dbm),
                  static_cast<double>(s.hx711_noise), static_cast<double>(s.hx711_sample_rate_hz), signal,
                  OperatingPolicy::permitsMaintenanceTools(c) ? "true" : "false", s.wake_reason, s.reset_reason,
                  s.motion_detected ? "true" : "false", static_cast<double>(s.provisional_angle_degrees),
                  static_cast<double>(s.provisional_angular_velocity_dps),
                  static_cast<double>(s.provisional_cadence_rpm), static_cast<unsigned>(s.provisional_revolutions),
                  static_cast<double>(s.provisional_confidence), s.provisional_reason,
                  s.ride_candidate ? "true" : "false", s.ride_active ? "true" : "false",
                  static_cast<unsigned>(s.current_ride_moving_seconds),
                  static_cast<double>(s.current_ride_distance_meters),
                  static_cast<double>(s.current_ride_speed_mps), s.last_ride.valid ? "true" : "false",
                  static_cast<unsigned>(s.last_ride.sequence), static_cast<unsigned>(s.last_ride.moving_seconds),
                  static_cast<unsigned>(s.last_ride.elapsed_seconds), static_cast<unsigned>(s.last_ride.crank_revolutions),
                  static_cast<double>(s.last_ride.average_power_watts), static_cast<int>(s.last_ride.maximum_power_watts),
                  static_cast<double>(s.last_ride.average_cadence_rpm), static_cast<double>(s.last_ride.maximum_cadence_rpm),
                  static_cast<double>(s.last_ride.work_kj), s.last_ride.end_reason,
                  static_cast<double>(s.last_ride.estimated_distance_meters),
                  static_cast<double>(s.last_ride.average_estimated_speed_mps),
                  static_cast<double>(s.last_ride.maximum_estimated_speed_mps),
                  static_cast<unsigned>(s.last_ride.road_model_version),
                  static_cast<double>(s.last_ride.rider_mass_kg),
                  CalibrationManager::stepName(cal.step), cal.result, cal.error, cal.active ? "true" : "false",
                  cal.valid ? "true" : "false", cal.mass_kg, cal.lever_arm_mm, cal.reference_torque_nm,
                  cal.raw_delta, cal.counts_per_nm, cal.nm_per_count, cal.verification_torque_nm,
                  cal.verification_error_percent, static_cast<unsigned>(cal.zero.count),
                  static_cast<unsigned>(cal.loaded.count), cal.zero.standardDeviation(), cal.loaded.standardDeviation(),
                   c.wifi_ssid, OperatingPolicy::value(c.operating_mode),
                   c.mqtt_battery_notifications_enabled ? "true" : "false", c.mqtt_host,
                   static_cast<unsigned>(c.mqtt_port), c.mqtt_topic,
                   c.light_sleep_enabled ? "true" : "false", static_cast<unsigned>(c.inactivity_timeout_ms),
                   c.ble_device_name, c.ride_diagnostics_enabled ? "true" : "false",
                   c.debug_logging_enabled ? "true" : "false", static_cast<double>(c.power_filter_alpha),
                   static_cast<unsigned>(c.maximum_valid_power_watts),
                   c.auto_ride_zero_enabled ? "true" : "false", c.ride_detection_enabled ? "true" : "false",
                   static_cast<unsigned>(c.minimum_ride_duration_seconds),
                   static_cast<unsigned>(c.cadence_timeout_seconds),
                   static_cast<unsigned>(c.ride_zero_stationary_timeout_seconds),
                   static_cast<double>(c.ride_zero_baseline_stddev_counts),
                   static_cast<double>(c.ride_zero_baseline_range_counts),
                   c.imperial_units ? "imperial" : "metric", static_cast<double>(c.rider_mass_kg),
                   static_cast<unsigned>(c.imu_wake_threshold),
                   static_cast<double>(c.imu_revolution_threshold_dps),
                   static_cast<unsigned>(c.minimum_cadence_rpm), static_cast<unsigned>(c.maximum_cadence_rpm),
                   static_cast<int>(c.ble_advertising_power_dbm),
                   static_cast<long>(c.runtime_zero_offset_counts),
                   static_cast<long>(c.calibration_zero_reference_counts), static_cast<double>(c.counts_per_nm),
                   static_cast<long>(c.torque_sign), static_cast<double>(c.calibration_mass_kg),
                   static_cast<double>(c.calibration_lever_arm_mm));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, g_status_json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t calibrationHandler(httpd_req_t *req) {
    return sendPage(req, webui::kCalibrationPage);
}

std::string requestBody(httpd_req_t *req) {
    std::string body(static_cast<size_t>(req->content_len), '\0');
    int received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (got <= 0) return {};
        received += got;
    }
    return body;
}

esp_err_t actionResponse(httpd_req_t *req, esp_err_t err, const char *success) {
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict");
        std::snprintf(g_status_json, sizeof(g_status_json),
                      "{\"ok\":false,\"message\":\"%s\"}", esp_err_to_name(err));
        return httpd_resp_sendstr(req, g_status_json);
    }
    std::snprintf(g_status_json, sizeof(g_status_json), "{\"ok\":true,\"message\":\"%s\"}", success);
    return httpd_resp_sendstr(req, g_status_json);
}

esp_err_t calibrationStartHandler(httpd_req_t *req) {
    const std::string body = requestBody(req);
    if (!g_portal->bridgeSignalConfirmed()) return actionResponse(req, ESP_ERR_INVALID_STATE, "");
    float mass = 0, lever = 0;
    if (!parseFloat(formValue(body, "mass_kg"), .01F, 200.F, mass) ||
        !parseFloat(formValue(body, "lever_mm"), 10.F, 1000.F, lever)) return actionResponse(req, ESP_ERR_INVALID_ARG, "");
    return actionResponse(req, g_portal->calibrationStart(mass, lever, formValue(body, "reverse") == "1"), "Unloaded capture started");
}
esp_err_t calibrationLoadHandler(httpd_req_t *req) { return actionResponse(req, g_portal->calibrationCaptureLoaded(), "Loaded capture started"); }
esp_err_t calibrationSaveHandler(httpd_req_t *req) { return actionResponse(req, g_portal->calibrationSave(), "Calibration saved"); }
esp_err_t calibrationVerifyHandler(httpd_req_t *req) { return actionResponse(req, g_portal->calibrationVerify(), "Verification capture started"); }
esp_err_t calibrationTareHandler(httpd_req_t *req) { return actionResponse(req, g_portal->calibrationTare(), "Runtime zero updated"); }
esp_err_t calibrationReverseHandler(httpd_req_t *req) { return actionResponse(req, g_portal->calibrationReverse(), "Torque direction reversed"); }
esp_err_t calibrationDiscardHandler(httpd_req_t *req) { g_portal->calibrationDiscard(); return actionResponse(req, ESP_OK, "Calibration session discarded"); }
esp_err_t calibrationResetHandler(httpd_req_t *req) {
    const std::string body = requestBody(req);
    if (formValue(body, "confirm") != "RESET") return actionResponse(req, ESP_ERR_INVALID_ARG, "");
    return actionResponse(req, g_portal->calibrationReset(), "Saved calibration reset");
}
esp_err_t imuResetHandler(httpd_req_t *req) { g_portal->resetImuTracker(); return actionResponse(req, ESP_OK, "Provisional tracker reset"); }

esp_err_t imuCaptureStartHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr ||
        !OperatingPolicy::isMaintenance(*g_portal->mutableConfig())) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Maintenance Mode is required");
    }
    g_imu_capture_active.store(false);
    g_imu_capture_count.store(0);
    g_imu_capture_started_us = esp_timer_get_time();
    g_imu_capture_last_sample_us = 0;
    g_imu_capture_active.store(true);
    return actionResponse(req, ESP_OK, "IMU capture started; maximum duration is 60 seconds");
}

esp_err_t imuCaptureStopHandler(httpd_req_t *req) {
    g_imu_capture_active.store(false);
    return actionResponse(req, ESP_OK, "IMU capture stopped");
}

esp_err_t imuCaptureClearHandler(httpd_req_t *req) {
    g_imu_capture_active.store(false);
    g_imu_capture_count.store(0);
    return actionResponse(req, ESP_OK, "IMU capture cleared");
}

esp_err_t imuCaptureStatusHandler(httpd_req_t *req) {
    char body[160];
    const size_t count = g_imu_capture_count.load();
    const uint32_t duration_ms = count > 0 ? g_imu_capture[count - 1].elapsed_ms : 0;
    std::snprintf(body, sizeof(body),
                  "{\"active\":%s,\"samples\":%u,\"duration_ms\":%u,\"capacity\":%u}",
                  g_imu_capture_active.load() ? "true" : "false", static_cast<unsigned>(count),
                  static_cast<unsigned>(duration_ms), static_cast<unsigned>(kImuCaptureCapacity));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

esp_err_t imuCaptureDownloadHandler(httpd_req_t *req) {
    if (g_imu_capture_active.load()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Stop capture before downloading");
    }
    if (g_imu_capture_count.load() == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No IMU samples have been captured");
    }
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=openwatts-imu-capture.csv");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "elapsed_ms,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps\n", HTTPD_RESP_USE_STRLEN), kTag, "send CSV header");
    char row[192];
    const size_t count = g_imu_capture_count.load();
    for (size_t i = 0; i < count; ++i) {
        const ImuCaptureSample &s = g_imu_capture[i];
        const int length = std::snprintf(row, sizeof(row), "%u,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f\n",
                                         static_cast<unsigned>(s.elapsed_ms), static_cast<double>(s.accel[0]),
                                         static_cast<double>(s.accel[1]), static_cast<double>(s.accel[2]),
                                         static_cast<double>(s.gyro[0]), static_cast<double>(s.gyro[1]),
                                         static_cast<double>(s.gyro[2]));
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, row, length), kTag, "send CSV row");
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}
esp_err_t bridgeConfirmHandler(httpd_req_t *req) {
    g_portal->setBridgeSignalConfirmed(formValue(requestBody(req), "confirmed") == "1");
    return actionResponse(req, ESP_OK, g_portal->bridgeSignalConfirmed() ? "Physical signal confirmed" : "Physical confirmation cleared");
}

esp_err_t operatingModeHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr || g_portal->storage() == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }

    const std::string requested_mode = formValue(requestBody(req), "mode");
    if (requested_mode != "normal" && requested_mode != "maintenance") {
        return badSetting(req, "Select Normal or Maintenance operating mode");
    }

    DeviceConfig candidate = *g_portal->mutableConfig();
    candidate.operating_mode = requested_mode == "maintenance"
                                   ? OperatingMode::Maintenance
                                   : OperatingMode::Normal;
    const esp_err_t err = g_portal->storage()->save(candidate);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "application/json");
    const char *message = candidate.operating_mode == OperatingMode::Maintenance
                              ? "Maintenance Mode active"
                              : "Normal Mode active";
    char response_body[96]{};
    std::snprintf(response_body, sizeof(response_body),
                  "{\"ok\":true,\"operating_mode\":\"%s\",\"message\":\"%s\"}",
                  OperatingPolicy::value(candidate.operating_mode), message);
    const esp_err_t response = httpd_resp_sendstr(req, response_body);
    // Apply only after acknowledging the request. On battery, Normal Mode is
    // then free to close the web server without truncating this response.
    *g_portal->mutableConfig() = candidate;
    g_portal->notifyConfigChanged();
    return response;
}

esp_err_t rideLoggingHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr || g_portal->storage() == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }
    std::string body;
    body.resize(static_cast<size_t>(req->content_len));
    int received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (got <= 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        received += got;
    }
    const std::string enabled = formValue(body, "enabled");
    if (enabled != "0" && enabled != "1") return badSetting(req, "Ride logging enabled must be 0 or 1");
    DeviceConfig candidate = *g_portal->mutableConfig();
    candidate.ride_detection_enabled = enabled == "1";
    const esp_err_t err = g_portal->storage()->save(candidate);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    *g_portal->mutableConfig() = candidate;
    g_portal->notifyConfigChanged();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, enabled == "1"
        ? "{\"ok\":true,\"ride_logging_enabled\":true,\"message\":\"Last ride recording enabled\"}"
        : "{\"ok\":true,\"ride_logging_enabled\":false,\"message\":\"Last ride recording disabled\"}");
}

esp_err_t saveHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr || g_portal->storage() == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }

    std::string body;
    body.resize(static_cast<size_t>(req->content_len));
    int received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (got <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        }
        received += got;
    }

    const DeviceConfig &current = *g_portal->mutableConfig();
    DeviceConfig candidate = current;
    const std::string ssid = formValue(body, "ssid");
    const std::string password = formValue(body, "password");
    const std::string timeout = formValue(body, "timeout");
    const std::string mqtt_host = formValue(body, "mqtt_host");
    const std::string mqtt_port = formValue(body, "mqtt_port");
    const std::string mqtt_topic = formValue(body, "mqtt_topic");
    const std::string ble_name = formValue(body, "ble_name");
    const std::string power_alpha = formValue(body, "power_filter_alpha");
    const std::string maximum_power = formValue(body, "maximum_valid_power_watts");
    const std::string minimum_ride = formValue(body, "minimum_ride_duration_seconds");
    const std::string cadence_timeout = formValue(body, "cadence_timeout_seconds");
    const std::string stationary_timeout = formValue(body, "ride_zero_stationary_timeout_seconds");
    const std::string imu_wake_threshold = formValue(body, "imu_wake_threshold");
    const std::string revolution_threshold = formValue(body, "imu_revolution_threshold_dps");
    const std::string minimum_cadence = formValue(body, "minimum_cadence_rpm");
    const std::string maximum_cadence = formValue(body, "maximum_cadence_rpm");
    const std::string advertising_power = formValue(body, "ble_advertising_power_dbm");
    const std::string unit_system = formValue(body, "unit_system");
    const std::string rider_mass = formValue(body, "rider_mass_kg");

    if (ssid.size() > 32) return badSetting(req, "Wi-Fi network name is too long");
    if (password.size() > 64) return badSetting(req, "Wi-Fi password is too long");
    if (mqtt_host.size() > 63) return badSetting(req, "MQTT broker address is too long");
    if (mqtt_topic.size() > 95) return badSetting(req, "MQTT topic is too long");

    uint32_t parsed_timeout = 0;
    uint32_t parsed_port = 0;
    if (!parseUnsigned(timeout, 5000, 3600000, parsed_timeout)) {
        return badSetting(req, "Sleep timeout must be between 5 seconds and 60 minutes");
    }
    if (!parseUnsigned(mqtt_port, 1, 65535, parsed_port)) {
        return badSetting(req, "MQTT port must be between 1 and 65535");
    }
    const bool mqtt_enabled = body.find("mqtt_enabled=on") != std::string::npos;
    if (mqtt_enabled && (mqtt_host.empty() || mqtt_topic.empty())) {
        return badSetting(req, "MQTT broker and topic are required when battery reporting is enabled");
    }
    if (!ble_name.empty() && !validBleName(ble_name)) {
        return badSetting(req, "Bluetooth name must use 1-18 letters, numbers, spaces, hyphens, or underscores");
    }
    float parsed_alpha = candidate.power_filter_alpha;
    if (!power_alpha.empty() && !parseFloat(power_alpha, 0.05F, 1.0F, parsed_alpha)) {
        return badSetting(req, "Power smoothing must be between 0.05 and 1.00");
    }
    uint32_t parsed_maximum = candidate.maximum_valid_power_watts;
    if (!maximum_power.empty() && !parseUnsigned(maximum_power, 500, 5000, parsed_maximum)) {
        return badSetting(req, "Maximum believable power must be between 500 and 5000 watts");
    }
    uint32_t parsed_minimum_ride = candidate.minimum_ride_duration_seconds;
    uint32_t parsed_cadence_timeout = candidate.cadence_timeout_seconds;
    uint32_t parsed_stationary_timeout = candidate.ride_zero_stationary_timeout_seconds;
    uint32_t parsed_wake_threshold = candidate.imu_wake_threshold;
    uint32_t parsed_minimum_cadence = candidate.minimum_cadence_rpm;
    uint32_t parsed_maximum_cadence = candidate.maximum_cadence_rpm;
    float parsed_revolution_threshold = candidate.imu_revolution_threshold_dps;
    float parsed_rider_mass = candidate.rider_mass_kg;
    if (!parseUnsigned(minimum_ride, 30, 3600, parsed_minimum_ride))
        return badSetting(req, "Minimum ride duration must be between 30 and 3600 seconds");
    if (!parseUnsigned(cadence_timeout, 1, 60, parsed_cadence_timeout))
        return badSetting(req, "Cadence timeout must be between 1 and 60 seconds");
    if (!parseUnsigned(stationary_timeout, 10, 600, parsed_stationary_timeout))
        return badSetting(req, "Ride Zero stationary delay must be between 10 and 600 seconds");
    if (!parseUnsigned(imu_wake_threshold, 1, 63, parsed_wake_threshold))
        return badSetting(req, "Motion sensitivity must be between 1 and 63");
    if (!parseFloat(revolution_threshold, 20.0F, 2000.0F, parsed_revolution_threshold))
        return badSetting(req, "Rotation threshold must be between 20 and 2000 degrees per second");
    if (!parseUnsigned(minimum_cadence, 1, 120, parsed_minimum_cadence) ||
        !parseUnsigned(maximum_cadence, parsed_minimum_cadence, 250, parsed_maximum_cadence))
        return badSetting(req, "Cadence range must be between 1 and 250 RPM");
    if (unit_system != "imperial" && unit_system != "metric")
        return badSetting(req, "Units must be Imperial or Metric");
    if (!parseFloat(rider_mass, 35.0F, 250.0F, parsed_rider_mass))
        return badSetting(req, "Rider mass must be between 35 and 250 kg");
    char *power_end = nullptr;
    const long parsed_advertising_power = std::strtol(advertising_power.c_str(), &power_end, 10);
    if (advertising_power.empty() || power_end == advertising_power.c_str() || *power_end != '\0' ||
        parsed_advertising_power < -20 || parsed_advertising_power > 9)
        return badSetting(req, "Bluetooth advertising power must be between -20 and 9 dBm");

    std::strncpy(candidate.wifi_ssid, ssid.c_str(), sizeof(candidate.wifi_ssid) - 1);
    // A blank password field means "leave the existing secret alone" so a
    // settings-only save cannot silently disconnect the device from its AP.
    if (!password.empty()) {
        std::strncpy(candidate.wifi_password, password.c_str(), sizeof(candidate.wifi_password) - 1);
    }
    candidate.wifi_ssid[sizeof(candidate.wifi_ssid) - 1] = '\0';
    candidate.wifi_password[sizeof(candidate.wifi_password) - 1] = '\0';
    candidate.light_sleep_enabled = body.find("sleep=on") != std::string::npos;
    const std::string operating_mode = formValue(body, "operating_mode");
    if (operating_mode != "normal" && operating_mode != "maintenance") {
        return badSetting(req, "Select Normal or Maintenance operating mode");
    }
    candidate.operating_mode = operating_mode == "maintenance" ? OperatingMode::Maintenance : OperatingMode::Normal;
    candidate.mqtt_battery_notifications_enabled = mqtt_enabled;
    if (!mqtt_host.empty()) std::strncpy(candidate.mqtt_host, mqtt_host.c_str(), sizeof(candidate.mqtt_host) - 1);
    if (!mqtt_topic.empty()) std::strncpy(candidate.mqtt_topic, mqtt_topic.c_str(), sizeof(candidate.mqtt_topic) - 1);
    candidate.mqtt_port = static_cast<uint16_t>(parsed_port);
    candidate.mqtt_host[sizeof(candidate.mqtt_host) - 1] = '\0';
    candidate.mqtt_topic[sizeof(candidate.mqtt_topic) - 1] = '\0';
    candidate.inactivity_timeout_ms = parsed_timeout;
    if (!ble_name.empty()) {
        std::strncpy(candidate.ble_device_name, ble_name.c_str(), sizeof(candidate.ble_device_name) - 1);
        candidate.ble_device_name[sizeof(candidate.ble_device_name) - 1] = '\0';
    }
    candidate.ride_diagnostics_enabled = body.find("ride_diagnostics=on") != std::string::npos;
    candidate.debug_logging_enabled = body.find("debug_logging=on") != std::string::npos;
    candidate.auto_ride_zero_enabled = body.find("auto_ride_zero=on") != std::string::npos;
    candidate.ride_detection_enabled = body.find("ride_detection=on") != std::string::npos;
    candidate.power_filter_alpha = parsed_alpha;
    candidate.maximum_valid_power_watts = static_cast<uint16_t>(parsed_maximum);
    candidate.minimum_ride_duration_seconds = static_cast<uint16_t>(parsed_minimum_ride);
    candidate.cadence_timeout_seconds = static_cast<uint16_t>(parsed_cadence_timeout);
    candidate.ride_zero_stationary_timeout_seconds = static_cast<uint16_t>(parsed_stationary_timeout);
    candidate.imu_wake_threshold = static_cast<uint8_t>(parsed_wake_threshold);
    candidate.imu_revolution_threshold_dps = parsed_revolution_threshold;
    candidate.minimum_cadence_rpm = static_cast<uint8_t>(parsed_minimum_cadence);
    candidate.maximum_cadence_rpm = static_cast<uint8_t>(parsed_maximum_cadence);
    candidate.ble_advertising_power_dbm = static_cast<int8_t>(parsed_advertising_power);
    candidate.imperial_units = unit_system == "imperial";
    candidate.rider_mass_kg = parsed_rider_mass;
    candidate.force_setup_portal = false;

    esp_err_t err = g_portal->storage()->save(candidate);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    httpd_resp_set_type(req, "application/json");
    const esp_err_t response = httpd_resp_sendstr(
        req, "{\"ok\":true,\"reboot_required\":true,\"message\":\"Settings saved\"}");
    *g_portal->mutableConfig() = candidate;
    g_portal->notifyConfigChanged();
    return response;
}

void dnsTask(void *arg) {
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t rx[256]{};
    while (true) {
        sockaddr_in source{};
        socklen_t source_len = sizeof(source);
        const int len = recvfrom(sock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&source), &source_len);
        if (len < 12) {
            continue;
        }

        uint8_t tx[320]{};
        std::memcpy(tx, rx, len);
        tx[2] = 0x81;
        tx[3] = 0x80;
        tx[6] = 0x00;
        tx[7] = 0x01;
        int offset = len;
        tx[offset++] = 0xC0;
        tx[offset++] = 0x0C;
        tx[offset++] = 0x00;
        tx[offset++] = 0x01;
        tx[offset++] = 0x00;
        tx[offset++] = 0x01;
        tx[offset++] = 0x00;
        tx[offset++] = 0x00;
        tx[offset++] = 0x00;
        tx[offset++] = 0x3C;
        tx[offset++] = 0x00;
        tx[offset++] = 0x04;
        tx[offset++] = 192;
        tx[offset++] = 168;
        tx[offset++] = 4;
        tx[offset++] = 1;
        sendto(sock, tx, offset, 0, reinterpret_cast<sockaddr *>(&source), source_len);
    }
}
}

esp_err_t SetupWifi::begin(DeviceConfig &config, SettingsStorage &storage, bool usb_present, bool setup_requested,
                           bool allow_battery_reporting) {
    active_ = false;
    config_ = &config;
    storage_ = &storage;

    if (!OperatingPolicy::permitsWifi(config, usb_present, allow_battery_reporting)) {
        ESP_LOGI(kTag, "USB absent; Wi-Fi remains off");
        return ESP_OK;
    }

    if (!config.wifi_setup_on_usb && !setup_requested && !config.force_setup_portal) {
        ESP_LOGI(kTag, "USB present but setup Wi-Fi disabled by config");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensureWifiInitialized(), kTag, "wifi init");

    // A saved network is the normal USB maintenance path. Keep the radio in
    // plain station mode there: APSTA is only recovery/setup and adds needless
    // coexistence/channel changes while connecting to the router.
    const bool start_setup_ap = !config.hasWifiCredentials() || setup_requested || config.force_setup_portal;
    if (start_setup_ap) {
        wifi_config_t ap_cfg{};
        std::strncpy(reinterpret_cast<char *>(ap_cfg.ap.ssid), kSetupSsid, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = std::strlen(kSetupSsid);
        ap_cfg.ap.channel = 6;
        ap_cfg.ap.max_connection = 2;
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(config.hasWifiCredentials() ? WIFI_MODE_APSTA : WIFI_MODE_AP),
                            kTag, "wifi mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), kTag, "wifi ap config");
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "wifi station mode");
    }
    if (config.hasWifiCredentials()) {
        wifi_config_t sta_cfg{};
        std::strncpy(reinterpret_cast<char *>(sta_cfg.sta.ssid), config.wifi_ssid, sizeof(sta_cfg.sta.ssid));
        std::strncpy(reinterpret_cast<char *>(sta_cfg.sta.password), config.wifi_password, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg), kTag, "wifi sta config");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");
    if (config.hasWifiCredentials()) {
        const esp_err_t connect_err = esp_wifi_connect();
        if (connect_err != ESP_OK) {
            ESP_LOGW(kTag, "station connect did not start: %s", esp_err_to_name(connect_err));
        }
    }
    ESP_RETURN_ON_ERROR(startHttpServer(), kTag, "http server");
    if (start_setup_ap) {
        ESP_RETURN_ON_ERROR(startDnsRedirect(), kTag, "dns redirect");
        ESP_LOGW(kTag, "setup portal active: SSID=%s, URL=http://192.168.4.1/", kSetupSsid);
    } else {
        ESP_LOGI(kTag, "%s Mode: joining saved Wi-Fi as a station",
                 OperatingPolicy::name(config.operating_mode));
    }

    active_ = true;
    return ESP_OK;
}

bool SetupWifi::active() const {
    return active_;
}

void SetupWifi::stop() {
    if (active_) {
        if (g_httpd != nullptr) {
            httpd_stop(g_httpd);
            g_httpd = nullptr;
        }
        if (g_dns_task != nullptr) {
            vTaskDelete(g_dns_task);
            g_dns_task = nullptr;
        }
        esp_wifi_stop();
        ESP_LOGI(kTag, "Wi-Fi stopped");
    }
    active_ = false;
}

DeviceConfig *SetupWifi::mutableConfig() {
    return config_;
}

SettingsStorage *SetupWifi::storage() {
    return storage_;
}

void SetupWifi::updateLiveStatus(const LiveStatus &status) {
    live_status_ = status;
    if (g_imu_capture_active.load()) {
        const int64_t now_us = esp_timer_get_time();
        size_t count = g_imu_capture_count.load();
        if (count < kImuCaptureCapacity &&
            (g_imu_capture_last_sample_us == 0 || now_us - g_imu_capture_last_sample_us >= kImuCaptureIntervalUs)) {
            ImuCaptureSample &sample = g_imu_capture[count];
            sample.elapsed_ms = static_cast<uint32_t>((now_us - g_imu_capture_started_us) / 1000);
            for (size_t axis = 0; axis < 3; ++axis) {
                sample.accel[axis] = status.imu_accel_g[axis];
                sample.gyro[axis] = status.imu_gyro_dps[axis];
            }
            g_imu_capture_last_sample_us = now_us;
            g_imu_capture_count.store(count + 1);
            if (count + 1 >= kImuCaptureCapacity) g_imu_capture_active.store(false);
        }
    }
}

LiveStatus SetupWifi::liveStatus() const {
    return live_status_;
}

bool SetupWifi::consumeConfigChanged() { return config_changed_.exchange(false); }
void SetupWifi::notifyConfigChanged() { config_changed_.store(true); }

void SetupWifi::observeCalibration(bool attempted, bool success, int32_t raw, bool hx_ready, int64_t now_us) {
    calibration_.observe(attempted, success, raw, hx_ready, now_us,
                         config_ && OperatingPolicy::permitsMaintenanceTools(*config_));
}
CalibrationSnapshot SetupWifi::calibrationSnapshot() const { return calibration_.snapshot(); }
esp_err_t SetupWifi::calibrationStart(double mass, double lever, bool reverse) { return calibration_.start(mass, lever, reverse, esp_timer_get_time(), config_ && OperatingPolicy::permitsMaintenanceTools(*config_), live_status_.hx711_ready); }
esp_err_t SetupWifi::calibrationCaptureLoaded() { return calibration_.captureLoaded(esp_timer_get_time(), config_ && OperatingPolicy::permitsMaintenanceTools(*config_), live_status_.hx711_ready); }
esp_err_t SetupWifi::calibrationSave() {
    if (!config_ || !storage_ || !OperatingPolicy::permitsMaintenanceTools(*config_)) return ESP_ERR_INVALID_STATE;
    DeviceConfig candidate = *config_;
    ESP_RETURN_ON_ERROR(calibration_.apply(candidate), kTag, "apply calibration");
    ESP_RETURN_ON_ERROR(storage_->save(candidate), kTag, "save calibration");
    *config_ = candidate; return ESP_OK;
}
esp_err_t SetupWifi::calibrationVerify() { return calibration_.verify(esp_timer_get_time(), config_ && OperatingPolicy::permitsMaintenanceTools(*config_), live_status_.hx711_ready); }
esp_err_t SetupWifi::calibrationTare() {
    // Manual Tare is an in-service adjustment, not Bench Calibration. If the
    // WebUI is reachable, allow it in either operating mode; sensor and saved
    // calibration validation remains inside CalibrationManager::manualTare().
    if (!config_ || !storage_) return ESP_ERR_INVALID_STATE;
    DeviceConfig candidate = *config_;
    ESP_RETURN_ON_ERROR(calibration_.manualTare(candidate, live_status_.hx711_ready, live_status_.filtered_counts, live_status_.hx711_noise), kTag, "tare");
    ESP_RETURN_ON_ERROR(storage_->save(candidate), kTag, "save tare"); *config_ = candidate; return ESP_OK;
}
esp_err_t SetupWifi::calibrationReverse() {
    if (!config_ || !storage_ || !OperatingPolicy::permitsMaintenanceTools(*config_)) return ESP_ERR_INVALID_STATE;
    DeviceConfig candidate = *config_;
    ESP_RETURN_ON_ERROR(calibration_.reverseDirection(candidate), kTag, "reverse");
    ESP_RETURN_ON_ERROR(storage_->save(candidate), kTag, "save reverse"); *config_ = candidate; return ESP_OK;
}
esp_err_t SetupWifi::calibrationReset() {
    if (!config_ || !storage_ || !OperatingPolicy::permitsMaintenanceTools(*config_)) return ESP_ERR_INVALID_STATE;
    DeviceConfig candidate = *config_; calibration_.resetCalibration(candidate);
    ESP_RETURN_ON_ERROR(storage_->save(candidate), kTag, "reset calibration"); *config_ = candidate; return ESP_OK;
}
void SetupWifi::calibrationDiscard() { calibration_.discard(); }
void SetupWifi::resetImuTracker() { imu_tracker_reset_requested_.store(true); }
bool SetupWifi::consumeImuTrackerReset() { return imu_tracker_reset_requested_.exchange(false); }
void SetupWifi::setBridgeSignalConfirmed(bool confirmed) { bridge_signal_confirmed_.store(confirmed && config_ && OperatingPolicy::permitsMaintenanceTools(*config_)); }
bool SetupWifi::bridgeSignalConfirmed() const { return bridge_signal_confirmed_.load() && config_ && OperatingPolicy::permitsMaintenanceTools(*config_); }
void SetupWifi::setTrainerTestActive(bool active) {
    constexpr int64_t kTrainerTestLeaseUs = 10LL * 60LL * 1000000LL;
    trainer_test_until_us_.store(active ? esp_timer_get_time() + kTrainerTestLeaseUs : 0);
}
bool SetupWifi::trainerTestActive() const {
    return trainer_test_until_us_.load() > esp_timer_get_time();
}

esp_err_t SetupWifi::startHttpServer() {
    if (g_httpd != nullptr) {
        return ESP_OK;
    }
    g_portal = this;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    // Keep headroom for product WebUI pages and their small control APIs.
    // This is a server allocation limit, not a runtime policy setting.
    cfg.max_uri_handlers = 36;
    ESP_RETURN_ON_ERROR(httpd_start(&g_httpd, &cfg), kTag, "httpd_start");

    httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = nullptr};
    httpd_uri_t logo{.uri = "/assets/openwatts-logo.svg", .method = HTTP_GET, .handler = logoHandler, .user_ctx = nullptr};
    httpd_uri_t save{.uri = "/save", .method = HTTP_POST, .handler = saveHandler, .user_ctx = nullptr};
    httpd_uri_t operating_mode{.uri = "/api/operating-mode", .method = HTTP_POST, .handler = operatingModeHandler, .user_ctx = nullptr};
    httpd_uri_t ride_logging{.uri = "/api/ride-logging", .method = HTTP_POST, .handler = rideLoggingHandler, .user_ctx = nullptr};
    httpd_uri_t status{.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = nullptr};
    httpd_uri_t ride_page{.uri = "/ride", .method = HTTP_GET, .handler = ridePageHandler, .user_ctx = nullptr};
    httpd_uri_t trainer_test{.uri = "/trainer-test", .method = HTTP_GET, .handler = trainerTestPageHandler, .user_ctx = nullptr};
    httpd_uri_t trainer_test_lease{.uri = "/api/trainer-test", .method = HTTP_POST, .handler = trainerTestLeaseHandler, .user_ctx = nullptr};
    httpd_uri_t settings{.uri = "/settings", .method = HTTP_GET, .handler = settingsHandler, .user_ctx = nullptr};
    httpd_uri_t diagnostics{.uri = "/diagnostics", .method = HTTP_GET, .handler = diagnosticsHandler, .user_ctx = nullptr};
    httpd_uri_t calibration{.uri = "/calibration", .method = HTTP_GET, .handler = calibrationHandler, .user_ctx = nullptr};
    httpd_uri_t ota_page{.uri = "/ota", .method = HTTP_GET, .handler = otaPageHandler, .user_ctx = nullptr};
    httpd_uri_t ota_upload{.uri = "/ota/upload", .method = HTTP_POST, .handler = otaUploadHandler, .user_ctx = nullptr};
    httpd_uri_t cal_start{.uri = "/api/calibration/start", .method = HTTP_POST, .handler = calibrationStartHandler, .user_ctx = nullptr};
    httpd_uri_t cal_load{.uri = "/api/calibration/load", .method = HTTP_POST, .handler = calibrationLoadHandler, .user_ctx = nullptr};
    httpd_uri_t cal_save{.uri = "/api/calibration/save", .method = HTTP_POST, .handler = calibrationSaveHandler, .user_ctx = nullptr};
    httpd_uri_t cal_verify{.uri = "/api/calibration/verify", .method = HTTP_POST, .handler = calibrationVerifyHandler, .user_ctx = nullptr};
    httpd_uri_t cal_tare{.uri = "/api/calibration/tare", .method = HTTP_POST, .handler = calibrationTareHandler, .user_ctx = nullptr};
    httpd_uri_t cal_reverse{.uri = "/api/calibration/reverse", .method = HTTP_POST, .handler = calibrationReverseHandler, .user_ctx = nullptr};
    httpd_uri_t cal_discard{.uri = "/api/calibration/discard", .method = HTTP_POST, .handler = calibrationDiscardHandler, .user_ctx = nullptr};
    httpd_uri_t cal_reset{.uri = "/api/calibration/reset", .method = HTTP_POST, .handler = calibrationResetHandler, .user_ctx = nullptr};
    httpd_uri_t imu_reset{.uri = "/api/imu-reset", .method = HTTP_POST, .handler = imuResetHandler, .user_ctx = nullptr};
    httpd_uri_t imu_capture_start{.uri = "/api/imu-capture/start", .method = HTTP_POST, .handler = imuCaptureStartHandler, .user_ctx = nullptr};
    httpd_uri_t imu_capture_stop{.uri = "/api/imu-capture/stop", .method = HTTP_POST, .handler = imuCaptureStopHandler, .user_ctx = nullptr};
    httpd_uri_t imu_capture_clear{.uri = "/api/imu-capture/clear", .method = HTTP_POST, .handler = imuCaptureClearHandler, .user_ctx = nullptr};
    httpd_uri_t imu_capture_status{.uri = "/api/imu-capture/status", .method = HTTP_GET, .handler = imuCaptureStatusHandler, .user_ctx = nullptr};
    httpd_uri_t imu_capture_download{.uri = "/api/imu-capture/download", .method = HTTP_GET, .handler = imuCaptureDownloadHandler, .user_ctx = nullptr};
    httpd_uri_t bridge_confirm{.uri = "/api/calibration/confirm-signal", .method = HTTP_POST, .handler = bridgeConfirmHandler, .user_ctx = nullptr};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &root), kTag, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &logo), kTag, "logo handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &save), kTag, "save handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &operating_mode), kTag, "operating mode handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ride_logging), kTag, "ride logging handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &status), kTag, "status handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ride_page), kTag, "ride page handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &trainer_test), kTag, "trainer test handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &trainer_test_lease), kTag, "trainer test lease handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &settings), kTag, "settings handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &diagnostics), kTag, "diagnostics handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &calibration), kTag, "calibration handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ota_page), kTag, "ota page handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ota_upload), kTag, "ota upload handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_start), kTag, "cal start");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_load), kTag, "cal load");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_save), kTag, "cal save");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_verify), kTag, "cal verify");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_tare), kTag, "cal tare");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_reverse), kTag, "cal reverse");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_discard), kTag, "cal discard");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &cal_reset), kTag, "cal reset");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_reset), kTag, "imu reset");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_capture_start), kTag, "IMU capture start");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_capture_stop), kTag, "IMU capture stop");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_capture_clear), kTag, "IMU capture clear");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_capture_status), kTag, "IMU capture status");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &imu_capture_download), kTag, "IMU capture download");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &bridge_confirm), kTag, "bridge confirm");
    return ESP_OK;
}

esp_err_t SetupWifi::startDnsRedirect() {
    if (g_dns_task != nullptr) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(dnsTask, "dns_redirect", 3072, nullptr, 4, &g_dns_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace openwatts
