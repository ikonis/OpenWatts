#pragma once

#include <cstdint>
#include <cstring>

#include "operating_mode.h"

namespace openwatts {

struct DeviceConfig {
    static constexpr uint32_t kMagic = 0x4F575454;  // OWTT
    static constexpr uint32_t kVersion = 10;

    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t sample_interval_ms = 50;
    uint32_t publish_interval_ms = 1000;
    uint32_t inactivity_timeout_ms = 60000;
    // GPIO10 is not an ESP32-C3 RTC GPIO, so the fitted LSM6DS3 wakes the
    // product from light sleep. Deep sleep remains available only for timed
    // battery-protection shutdowns.
    bool light_sleep_enabled = true;
    bool deep_sleep_enabled = false;
    bool wake_on_timer_enabled = true;
    uint32_t timer_wake_seconds = 300;
    bool wake_on_imu_enabled = true;
    bool wake_on_usb_enabled = true;
    bool wake_on_button_enabled = false;
    bool wifi_setup_on_usb = true;
    // Retained at its historical offset for v9-and-older NVS migration only.
    bool legacy_wifi_keep_alive_without_usb = false;
    bool force_setup_portal = false;
    bool run_self_test_on_boot = true;
    bool self_test_done = false;
    float hx711_smoothing = 0.20F;
    int32_t zero_offset_counts = 0;
    float counts_per_nm = 10000.0F;  // TODO: calibrate with known torque fixture.
    int32_t torque_sign = 1;
    float imu_revolution_threshold_dps = 120.0F;  // TODO: replace with validated crank cadence algorithm.
    uint8_t imu_wake_threshold = 4;
    uint8_t imu_wake_duration = 1;
    char ble_device_name[19] = "OpenWatts";
    bool mqtt_battery_notifications_enabled = true;
    char mqtt_host[64] = "192.168.1.28";
    uint16_t mqtt_port = 1883;
    char mqtt_topic[96] = "openwatts/battery";
    uint8_t mqtt_charge_soon_percent = 20;
    uint8_t mqtt_charge_now_percent = 10;
    uint8_t mqtt_critical_percent = 5;
    char wifi_ssid[33]{};
    char wifi_password[65]{};

    // Append persisted fields only.  This preserves every prior field offset
    // when a smaller NVS blob is loaded during firmware migration.
    bool strain_calibration_valid = false;

    // Shared power-policy settings. Voltage is authoritative; percentage is
    // informational until the assembled board divider is validated.
    float battery_voltage_scale = 2.0F;
    int32_t battery_voltage_offset_mv = 0;
    float battery_charge_soon_voltage = 3.65F;
    float battery_charge_now_voltage = 3.50F;
    float battery_critical_voltage = 3.35F;
    float battery_protection_voltage = 3.20F;
    float battery_hysteresis_voltage = 0.05F;
    uint8_t battery_qualification_count = 2;
    uint32_t battery_check_interval_seconds = 300;
    uint32_t battery_heartbeat_interval_seconds = 86400;
    float battery_report_voltage_delta = 0.02F;
    float usb_voltage_publish_delta = 0.01F;
    uint32_t battery_report_retry_interval_seconds = 900;
    uint16_t maximum_valid_power_watts = 2000;
    float power_filter_alpha = 0.35F;
    bool ride_diagnostics_enabled = false;
    // The threshold-crossing cadence provider is bring-up-only. Rotation-aware
    // power stays disabled until physical IMU axis/angle validation succeeds.
    bool rotation_aware_power_enabled = false;

    // Configuration plumbing for product controls whose algorithms are not
    // enabled yet. Append-only storage preserves all v6 field offsets.
    bool debug_logging_enabled = false;
    bool auto_ride_zero_enabled = false;
    bool ride_detection_enabled = false;
    uint16_t minimum_ride_duration_seconds = 180;
    uint16_t cadence_timeout_seconds = 5;
    int8_t ble_advertising_power_dbm = 0;
    bool ble_auto_advertise_enabled = true;

    // Permanent bench calibration and mutable in-service zero are deliberately
    // separate. Automatic/runtime tare must never alter the permanent scale.
    int32_t calibration_zero_reference_counts = 0;
    int32_t runtime_zero_offset_counts = 0;
    float calibration_mass_kg = 0.0F;
    float calibration_lever_arm_mm = 0.0F;

    // Installation-time IMU tuning scaffolding. Production rotation-aware
    // power remains disabled until crank-mounted validation is complete.
    uint16_t imu_accel_odr_hz = 104;
    uint8_t imu_accel_range_g = 2;
    uint16_t imu_gyro_odr_hz = 104;
    uint16_t imu_gyro_range_dps = 500;
    uint16_t imu_stationary_timeout_ms = 2000;
    uint8_t rotation_confidence_threshold_percent = 80;
    uint8_t minimum_cadence_rpm = 10;
    uint8_t maximum_cadence_rpm = 220;
    int8_t rotation_direction_convention = 1;
    float crank_angle_reference_degrees = 0.0F;
    // Former IMU tuning timeout. Retained only to preserve the append-only
    // binary layout while loading older NVS blobs.
    uint16_t reserved_legacy_imu_tuning_timeout_seconds = 900;

    OperatingMode operating_mode = OperatingMode::Normal;

    bool hasWifiCredentials() const {
        return wifi_ssid[0] != '\0';
    }
};

}  // namespace openwatts
