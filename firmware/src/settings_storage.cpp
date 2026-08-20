#include "settings_storage.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "settings";
constexpr char kNamespace[] = "openwatts";
constexpr char kConfigKey[] = "config";
constexpr char kLastRideKey[] = "last_ride";

struct LastRideSummaryV2 {
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t moving_seconds;
    uint32_t elapsed_seconds;
    uint32_t crank_revolutions;
    float average_power_watts;
    int16_t maximum_power_watts;
    float average_cadence_rpm;
    float maximum_cadence_rpm;
    float work_kj;
    bool valid;
    char end_reason[24];
    float estimated_distance_meters;
    float average_estimated_speed_mps;
    float maximum_estimated_speed_mps;
    uint16_t road_model_version;
    float rider_mass_kg;
};

struct LastRideSummaryV1 {
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t moving_seconds;
    uint32_t elapsed_seconds;
    uint32_t crank_revolutions;
    float average_power_watts;
    int16_t maximum_power_watts;
    float average_cadence_rpm;
    float maximum_cadence_rpm;
    float work_kj;
    bool valid;
    char end_reason[24];
};

DeviceConfig sanitized(DeviceConfig config) {
    if (config.magic != DeviceConfig::kMagic || config.version > DeviceConfig::kVersion) {
        return DeviceConfig{};
    }
    const uint32_t stored_version = config.version;
    if (stored_version < 10) {
        config.operating_mode = config.legacy_wifi_keep_alive_without_usb
            ? OperatingMode::Maintenance : OperatingMode::Normal;
    }
    if (stored_version < 12) {
        config.auto_ride_zero_enabled = true;
        config.ride_zero_baseline_stddev_counts = 0.0F;
        config.ride_zero_baseline_range_counts = 0.0F;
    }
    if (stored_version < 13) {
        config.imperial_units = true;
        config.rider_mass_kg = 82.0F;
    }
    if (config.operating_mode != OperatingMode::Normal && config.operating_mode != OperatingMode::Maintenance) {
        config.operating_mode = OperatingMode::Normal;
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
    config.battery_voltage_scale = std::clamp(config.battery_voltage_scale, 0.1F, 10.0F);
    config.battery_qualification_count = std::clamp<uint8_t>(config.battery_qualification_count, 1, 8);
    config.battery_check_interval_seconds = std::clamp<uint32_t>(config.battery_check_interval_seconds, 60, 86400);
    config.battery_heartbeat_interval_seconds =
        std::clamp<uint32_t>(config.battery_heartbeat_interval_seconds, 300, 604800);
    config.battery_report_voltage_delta = std::clamp(config.battery_report_voltage_delta, 0.005F, 0.25F);
    config.battery_report_retry_interval_seconds =
        std::clamp<uint32_t>(config.battery_report_retry_interval_seconds, 60, 86400);
    config.maximum_valid_power_watts = std::clamp<uint16_t>(config.maximum_valid_power_watts, 500, 5000);
    config.power_filter_alpha = std::clamp(config.power_filter_alpha, 0.05F, 1.0F);
    config.minimum_ride_duration_seconds =
        std::clamp<uint16_t>(config.minimum_ride_duration_seconds, 30, 3600);
    config.cadence_timeout_seconds = std::clamp<uint16_t>(config.cadence_timeout_seconds, 1, 60);
    config.ble_advertising_power_dbm = std::clamp<int8_t>(config.ble_advertising_power_dbm, -20, 9);
    config.minimum_cadence_rpm = std::clamp<uint8_t>(config.minimum_cadence_rpm, 1, 120);
    config.maximum_cadence_rpm = std::clamp<uint8_t>(config.maximum_cadence_rpm, config.minimum_cadence_rpm, 250);
    config.rotation_confidence_threshold_percent =
        std::clamp<uint8_t>(config.rotation_confidence_threshold_percent, 1, 100);
    config.imu_stationary_timeout_ms = std::clamp<uint16_t>(config.imu_stationary_timeout_ms, 250, 30000);
    config.ride_zero_stationary_timeout_seconds =
        std::clamp<uint16_t>(config.ride_zero_stationary_timeout_seconds, 10, 600);
    if (!std::isfinite(config.ride_zero_baseline_stddev_counts) || config.ride_zero_baseline_stddev_counts < 0.0F)
        config.ride_zero_baseline_stddev_counts = 0.0F;
    if (!std::isfinite(config.ride_zero_baseline_range_counts) || config.ride_zero_baseline_range_counts < 0.0F)
        config.ride_zero_baseline_range_counts = 0.0F;
    if (!std::isfinite(config.rider_mass_kg) || config.rider_mass_kg < 35.0F || config.rider_mass_kg > 250.0F)
        config.rider_mass_kg = 82.0F;
    if (!std::isfinite(config.counts_per_nm) || config.counts_per_nm <= 0.0F) {
        config.strain_calibration_valid = false;
        config.counts_per_nm = 10000.0F;
    }
    if (config.strain_calibration_valid && config.runtime_zero_offset_counts == 0 && config.zero_offset_counts != 0) {
        config.calibration_zero_reference_counts = config.zero_offset_counts;
        config.runtime_zero_offset_counts = config.zero_offset_counts;
    }
    config.zero_offset_counts = config.runtime_zero_offset_counts;
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

LastRideSummary SettingsStorage::loadLastRide() {
    LastRideSummary summary{};
    if (!initialized_) return summary;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return summary;
    size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, kLastRideKey, nullptr, &size);
    if (err != ESP_OK) { nvs_close(handle); return summary; }
    if (size == sizeof(summary)) {
        err = nvs_get_blob(handle, kLastRideKey, &summary, &size);
        nvs_close(handle);
        if (err != ESP_OK || !summary.valid) return LastRideSummary{};
        if (summary.schema_version == LastRideSummary::kSchemaVersion) return summary;
        // The appended v3 bool can occupy v2 tail padding, making both blobs
        // the same size on this target. The preceding fields have identical
        // offsets, so migrate by version before rejecting the record.
        if (summary.schema_version == 2) {
            summary.schema_version = LastRideSummary::kSchemaVersion;
            summary.mqtt_publish_pending = false;
            return summary;
        }
        return LastRideSummary{};
    }
    if (size == sizeof(LastRideSummaryV2)) {
        LastRideSummaryV2 legacy{};
        err = nvs_get_blob(handle, kLastRideKey, &legacy, &size);
        nvs_close(handle);
        if (err != ESP_OK || legacy.schema_version != 2 || !legacy.valid) return LastRideSummary{};
        summary.sequence = legacy.sequence;
        summary.moving_seconds = legacy.moving_seconds;
        summary.elapsed_seconds = legacy.elapsed_seconds;
        summary.crank_revolutions = legacy.crank_revolutions;
        summary.average_power_watts = legacy.average_power_watts;
        summary.maximum_power_watts = legacy.maximum_power_watts;
        summary.average_cadence_rpm = legacy.average_cadence_rpm;
        summary.maximum_cadence_rpm = legacy.maximum_cadence_rpm;
        summary.work_kj = legacy.work_kj;
        summary.valid = true;
        std::strncpy(summary.end_reason, legacy.end_reason, sizeof(summary.end_reason) - 1);
        summary.estimated_distance_meters = legacy.estimated_distance_meters;
        summary.average_estimated_speed_mps = legacy.average_estimated_speed_mps;
        summary.maximum_estimated_speed_mps = legacy.maximum_estimated_speed_mps;
        summary.road_model_version = legacy.road_model_version;
        summary.rider_mass_kg = legacy.rider_mass_kg;
        // A v2 record predates durable acknowledgement state. It was already
        // eligible for publication in prior firmware, so do not invent a
        // duplicate report during migration.
        summary.mqtt_publish_pending = false;
        return summary;
    }
    if (size == sizeof(LastRideSummaryV1)) {
        LastRideSummaryV1 legacy{};
        err = nvs_get_blob(handle, kLastRideKey, &legacy, &size);
        nvs_close(handle);
        if (err != ESP_OK || legacy.schema_version != 1 || !legacy.valid) return LastRideSummary{};
        summary.sequence = legacy.sequence;
        summary.moving_seconds = legacy.moving_seconds;
        summary.elapsed_seconds = legacy.elapsed_seconds;
        summary.crank_revolutions = legacy.crank_revolutions;
        summary.average_power_watts = legacy.average_power_watts;
        summary.maximum_power_watts = legacy.maximum_power_watts;
        summary.average_cadence_rpm = legacy.average_cadence_rpm;
        summary.maximum_cadence_rpm = legacy.maximum_cadence_rpm;
        summary.work_kj = legacy.work_kj;
        summary.valid = true;
        std::strncpy(summary.end_reason, legacy.end_reason, sizeof(summary.end_reason) - 1);
        // Model version zero explicitly means this pre-model ride has no
        // reproducible road estimate. Never invent one after settings change.
        summary.road_model_version = 0;
        return summary;
    }
    nvs_close(handle);
    ESP_LOGW(kTag, "last ride blob has unsupported size %u", static_cast<unsigned>(size));
    return LastRideSummary{};
}

esp_err_t SettingsStorage::saveLastRide(const LastRideSummary &summary) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, kLastRideKey, &summary, sizeof(summary));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return err;
}

}  // namespace openwatts
