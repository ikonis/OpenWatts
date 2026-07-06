#pragma once

#include <cstdint>

#include "cadence_estimator.h"
#include "config.h"
#include "esp_err.h"

namespace openwatts {

class BleCyclingPowerService;
class SetupWifi;

class PowerManager {
public:
    void updateConfig(const DeviceConfig &config);
    bool shouldSleepForInactivity(const CadenceState &cadence, int64_t now_us) const;
    void enterSleep(BleCyclingPowerService &ble, SetupWifi &setup_wifi) const;

private:
    esp_err_t configureWakeSources() const;

    DeviceConfig config_{};
};

}  // namespace openwatts
