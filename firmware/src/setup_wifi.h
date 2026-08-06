#pragma once

#include <atomic>

#include "config.h"
#include "esp_err.h"

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
    uint32_t hx711_failures = 0;
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
    void requestBenchLightSleep();
    bool consumeBenchLightSleepRequest();

private:
    esp_err_t startHttpServer();
    esp_err_t startDnsRedirect();

    bool active_ = false;
    DeviceConfig *config_ = nullptr;
    SettingsStorage *storage_ = nullptr;
    LiveStatus live_status_{};
    std::atomic<bool> bench_light_sleep_requested_{false};
};

}  // namespace openwatts
