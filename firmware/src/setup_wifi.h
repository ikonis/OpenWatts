#pragma once

#include <atomic>

#include "calibration.h"
#include "config.h"
#include "esp_err.h"
#include "ride_log.h"

namespace openwatts {

class SettingsStorage;

// A compact, copyable snapshot owned by the application loop.  The web server
// only reads this snapshot; it never touches sensor drivers directly.
struct LiveStatus {
    uint32_t uptime_seconds = 0;
    float battery_voltage = 0.0F;
    uint8_t battery_percent = 0;
    bool battery_valid = false;
    bool usb_present = false;
    bool charging = false;
    bool ble_connected = false;
    bool imu_ready = false;
    bool imu_interrupt_active = false;
    uint8_t imu_who_am_i = 0;
    float imu_accel_g[3]{};
    float imu_gyro_dps[3]{};
    bool hx711_ready = false;
    bool strain_calibration_valid = false;
    int32_t raw_counts = 0;
    float filtered_counts = 0.0F;
    float torque_nm = 0.0F;
    float cadence_rpm = 0.0F;
    int16_t power_watts = 0;
    uint32_t revolutions = 0;
    float cadence_corrected_gyro_z_dps = 0.0F;
    float cadence_forward_velocity_dps = 0.0F;
    float cadence_integrated_angle_degrees = 0.0F;
    float cadence_reverse_angle_degrees = 0.0F;
    float cadence_last_candidate_rpm = 0.0F;
    uint32_t cadence_imu_invalid_reads = 0;
    uint32_t cadence_rejected_revolutions = 0;
    uint32_t cadence_integration_gap_count = 0;
    float cadence_last_sample_interval_ms = 0.0F;
    uint32_t cadence_dropout_count = 0;
    uint32_t cadence_last_revolution_age_ms = 0;
    const char *cadence_last_dropout_reason = "none";
    uint32_t cadence_last_dropout_age_ms = 0;
    int ble_last_notify_result = 0;
    uint32_t ble_notify_success_count = 0;
    uint32_t ble_notify_failure_count = 0;
    int16_t ble_last_power_watts = 0;
    float ble_last_cadence_rpm = 0.0F;
    uint16_t ble_last_crank_revolutions = 0;
    uint16_t ble_last_crank_event_time = 0;
    uint32_t ble_last_notify_age_ms = 0;
    bool ble_measurement_subscribed = false;
    uint32_t ble_transmit_event_count = 0;
    uint32_t ble_transmit_error_count = 0;
    int ble_last_transmit_status = 0;
    uint32_t ble_disconnect_count = 0;
    int ble_last_disconnect_reason = 0;
    uint16_t ble_connection_interval_units = 0;
    int8_t ble_connection_rssi_dbm = -127;
    uint32_t hx711_failures = 0;
    float hx711_noise = 0.0F;
    float hx711_sample_rate_hz = 0.0F;
    const char *wake_reason = "unknown";
    const char *reset_reason = "unknown";
    float provisional_angle_degrees = 0.0F;
    float provisional_angular_velocity_dps = 0.0F;
    float provisional_cadence_rpm = 0.0F;
    uint32_t provisional_revolutions = 0;
    float provisional_confidence = 0.0F;
    const char *provisional_reason = "tuning_disabled";
    bool motion_detected = false;
    LastRideSummary last_ride{};
    bool ride_active = false;
};

class SetupWifi {
public:
    esp_err_t begin(DeviceConfig &config, SettingsStorage &storage, bool usb_present, bool setup_requested,
                    bool allow_battery_reporting = false);
    bool active() const;
    void stop();
    DeviceConfig *mutableConfig();
    SettingsStorage *storage();
    void updateLiveStatus(const LiveStatus &status);
    LiveStatus liveStatus() const;
    bool consumeConfigChanged();
    void notifyConfigChanged();
    void observeCalibration(bool attempted, bool success, int32_t raw, bool hx_ready, int64_t now_us);
    CalibrationSnapshot calibrationSnapshot() const;
    esp_err_t calibrationStart(double mass_kg, double lever_mm, bool reverse);
    esp_err_t calibrationCaptureLoaded();
    esp_err_t calibrationSave();
    esp_err_t calibrationVerify();
    esp_err_t calibrationTare();
    esp_err_t calibrationReverse();
    esp_err_t calibrationReset();
    void calibrationDiscard();
    void resetImuTracker();
    bool consumeImuTrackerReset();
    void setBridgeSignalConfirmed(bool confirmed);
    bool bridgeSignalConfirmed() const;

private:
    esp_err_t startHttpServer();
    esp_err_t startDnsRedirect();

    bool active_ = false;
    DeviceConfig *config_ = nullptr;
    SettingsStorage *storage_ = nullptr;
    LiveStatus live_status_{};
    std::atomic<bool> config_changed_{false};
    std::atomic<bool> imu_tracker_reset_requested_{false};
    std::atomic<bool> bridge_signal_confirmed_{false};
    CalibrationManager calibration_{};
};

}  // namespace openwatts
