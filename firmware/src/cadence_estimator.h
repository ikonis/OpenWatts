#pragma once

#include <cstdint>

#include "config.h"
#include "imu_lsm6ds3.h"

namespace openwatts {

struct CadenceState {
    float rpm = 0.0F;
    float forward_delta_radians = 0.0F;
    uint32_t revolutions = 0;
    int64_t last_revolution_us = 0;
    int64_t revolution_duration_us = 0;
    bool revolution_completed = false;
    bool moving = false;
};

class CadenceEstimator {
public:
    void updateConfig(const DeviceConfig &config);
    CadenceState update(const ImuSample &sample, int64_t now_us);
    CadenceState latest() const;
    void reset();

private:
    DeviceConfig config_{};
    CadenceState latest_{};
    float gyro_z_bias_dps_ = 0.0F;
    float forward_angle_degrees_ = 0.0F;
    float reverse_angle_degrees_ = 0.0F;
    int64_t last_sample_us_ = 0;
    int64_t motion_started_us_ = 0;
    int64_t stationary_since_us_ = 0;
    int64_t previous_revolution_us_ = 0;
};

}  // namespace openwatts
