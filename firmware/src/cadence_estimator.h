#pragma once

#include <cstdint>

#include "config.h"
#include "imu_lsm6ds3.h"

namespace openwatts {

struct CadenceState {
    float rpm = 0.0F;
    uint32_t revolutions = 0;
    int64_t last_revolution_us = 0;
    bool moving = false;
};

class CadenceEstimator {
public:
    void updateConfig(const DeviceConfig &config);
    CadenceState update(const ImuSample &sample, int64_t now_us);
    CadenceState latest() const;

private:
    DeviceConfig config_{};
    CadenceState latest_{};
    bool armed_ = true;
    int64_t previous_revolution_us_ = 0;
};

}  // namespace openwatts
