#include "settings_storage.h"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "settings";
constexpr char kNamespace[] = "openwatts";
constexpr char kConfigKey[] = "config";

DeviceConfig sanitized(DeviceConfig config) {
    if (config.magic != DeviceConfig::kMagic || config.version != DeviceConfig::kVersion) {
        return DeviceConfig{};
    }
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
    if (config.sample_interval_ms < 10) {
        config.sample_interval_ms = 10;
    }
    if (config.publish_interval_ms < 100) {
        config.publish_interval_ms = 100;
    }
    if (config.inactivity_timeout_ms < 5000) {
        config.inactivity_timeout_ms = 5000;
    }
    if (config.timer_wake_seconds == 0) {
        config.timer_wake_seconds = 5;
    }
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

    size_t size = sizeof(config);
    err = nvs_get_blob(handle, kConfigKey, &config, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(config)) {
        ESP_LOGW(kTag, "config read failed or version changed; using defaults");
        return DeviceConfig{};
    }
    return sanitized(config);
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
