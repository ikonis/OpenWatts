#pragma once

#include <cstdint>
#include <cstring>

namespace openwatts {

struct DeviceConfig {
    static constexpr uint32_t kMagic = 0x4F575454;  // OWTT
    static constexpr uint32_t kVersion = 2;

    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t sample_interval_ms = 50;
    uint32_t publish_interval_ms = 1000;
    uint32_t inactivity_timeout_ms = 60000;
    bool deep_sleep_enabled = true;
    bool wake_on_timer_enabled = true;
    uint32_t timer_wake_seconds = 5;
    bool wake_on_imu_enabled = false;
    bool wake_on_usb_enabled = false;
    bool wake_on_button_enabled = false;
    bool wifi_setup_on_usb = true;
    bool force_setup_portal = false;
    bool run_self_test_on_boot = true;
    bool self_test_done = false;
    float hx711_smoothing = 0.20F;
    int32_t zero_offset_counts = 0;
    float counts_per_nm = 10000.0F;  // TODO: calibrate with known torque fixture.
    int32_t torque_sign = 1;
    float imu_revolution_threshold_dps = 120.0F;  // TODO: replace with validated crank cadence algorithm.
    char wifi_ssid[33]{};
    char wifi_password[65]{};

    bool hasWifiCredentials() const {
        return wifi_ssid[0] != '\0';
    }
};

}  // namespace openwatts
