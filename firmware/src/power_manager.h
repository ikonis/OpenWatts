#pragma once

#include <cstdint>

#include "cadence_estimator.h"
#include "config.h"
#include "esp_err.h"

namespace openwatts {

class BleCyclingPowerService;
class SetupWifi;
class Hx711;
class Lsm6ds3;

class PowerManager {
public:
    void updateConfig(const DeviceConfig &config);
    bool shouldSleepForInactivity(const CadenceState &cadence, int64_t now_us) const;
    esp_err_t enterSleep(BleCyclingPowerService &ble, SetupWifi &setup_wifi, Hx711 &hx711, Lsm6ds3 &imu) const;

private:
    esp_err_t configureLightSleepWakeSources() const;

    DeviceConfig config_{};
    mutable int64_t last_wake_us_ = 0;
};

}  // namespace openwatts
