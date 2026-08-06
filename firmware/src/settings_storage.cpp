#include "settings_storage.h"

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "settings";
constexpr char kNamespace[] = "openwatts";
constexpr char kConfigKey[] = "config";

DeviceConfig sanitized(DeviceConfig config) {
    if (config.magic != DeviceConfig::kMagic || config.version > DeviceConfig::kVersion) {
        return DeviceConfig{};
    }
    config.version = DeviceConfig::kVersion;
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
    config.ble_device_name[sizeof(config.ble_device_name) - 1] = '\0';
    config.mqtt_host[sizeof(config.mqtt_host) - 1] = '\0';
    config.mqtt_topic[sizeof(config.mqtt_topic) - 1] = '\0';
    if (config.ble_device_name[0] == '\0') {
        std::strncpy(config.ble_device_name, "OpenWatts", sizeof(config.ble_device_name) - 1);
    }
    if (config.sample_interval_ms < 10) {
        config.sample_interval_ms = 10;
    }
    if (config.publish_interval_ms < 100) {
        config.publish_interval_ms = 100;
    }
    if (config.inactivity_timeout_ms < 5000) {
        config.inactivity_timeout_ms = 5000;
    }
    if (config.timer_wake_seconds < 60) {
        config.timer_wake_seconds = 60;
    }
    config.imu_wake_threshold = std::min<uint8_t>(config.imu_wake_threshold, 0x3F);
    config.imu_wake_duration = std::min<uint8_t>(config.imu_wake_duration, 3);
    config.mqtt_port = config.mqtt_port == 0 ? 1883 : config.mqtt_port;
    config.mqtt_charge_soon_percent = std::min<uint8_t>(config.mqtt_charge_soon_percent, 100);
    config.mqtt_charge_now_percent =
        std::min(config.mqtt_charge_now_percent, config.mqtt_charge_soon_percent);
    config.mqtt_critical_percent =
        std::min(config.mqtt_critical_percent, config.mqtt_charge_now_percent);
    config.battery_voltage_scale = std::clamp(config.battery_voltage_scale, 0.1F, 10.0F);
    config.battery_qualification_count = std::clamp<uint8_t>(config.battery_qualification_count, 1, 8);
    config.battery_check_interval_seconds = std::clamp<uint32_t>(config.battery_check_interval_seconds, 60, 86400);
    config.battery_heartbeat_interval_seconds =
        std::clamp<uint32_t>(config.battery_heartbeat_interval_seconds, 300, 604800);
    config.battery_report_voltage_delta = std::clamp(config.battery_report_voltage_delta, 0.005F, 0.25F);
    config.usb_voltage_publish_delta = std::clamp(config.usb_voltage_publish_delta, 0.005F, 0.25F);
    config.battery_report_retry_interval_seconds =
        std::clamp<uint32_t>(config.battery_report_retry_interval_seconds, 60, 86400);
    config.maximum_valid_power_watts = std::clamp<uint16_t>(config.maximum_valid_power_watts, 500, 5000);
    config.power_filter_alpha = std::clamp(config.power_filter_alpha, 0.05F, 1.0F);
    config.minimum_ride_duration_seconds =
        std::clamp<uint16_t>(config.minimum_ride_duration_seconds, 30, 3600);
    config.cadence_timeout_seconds = std::clamp<uint16_t>(config.cadence_timeout_seconds, 1, 60);
    config.ble_advertising_power_dbm = std::clamp<int8_t>(config.ble_advertising_power_dbm, -20, 9);
    return config;
}
}  // namespace

esp_err_t SettingsStorage::begin() {
    initialized_ = true;
    return ESP_OK;
}

DeviceConfig SettingsStorage::load() {
    DeviceConfig config{};
    if (!initialized_) {
        return config;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "no stored config; using defaults");
        return config;
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return config;
    }

    size_t size = 0;
    err = nvs_get_blob(handle, kConfigKey, nullptr, &size);
    if (err != ESP_OK || size < sizeof(uint32_t) * 2U || size > sizeof(config)) {
        nvs_close(handle);
        ESP_LOGW(kTag, "config blob has unsupported size %u; using defaults", static_cast<unsigned>(size));
        return DeviceConfig{};
    }
    DeviceConfig migrated{};
    err = nvs_get_blob(handle, kConfigKey, &migrated, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "config read failed: %s", esp_err_to_name(err));
        return DeviceConfig{};
    }
    const uint32_t stored_version = migrated.version;
    config = sanitized(migrated);
    if (config.magic != DeviceConfig::kMagic) return DeviceConfig{};
    if (stored_version != DeviceConfig::kVersion) {
        ESP_LOGI(kTag, "migrated config v%u to v%u without replacing stored credentials/calibration",
                 static_cast<unsigned>(stored_version), static_cast<unsigned>(DeviceConfig::kVersion));
    }
    return config;
}

esp_err_t SettingsStorage::save(const DeviceConfig &config) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    DeviceConfig clean = sanitized(config);
    err = nvs_set_blob(handle, kConfigKey, &clean, sizeof(clean));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t SettingsStorage::markSelfTestDone(DeviceConfig &config) {
    config.self_test_done = true;
    config.run_self_test_on_boot = false;
    return save(config);
}

}  // namespace openwatts
