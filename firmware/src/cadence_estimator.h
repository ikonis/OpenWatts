#pragma once

#include <cstdint>

#include "config.h"
#include "imu_lsm6ds3.h"

namespace openwatts {

enum class CadenceDropoutReason : uint8_t {
    None = 0,
    RevolutionTimeout,
    ImuInvalidTimeout,
    ReverseReset,
};

const char *cadenceDropoutReasonName(CadenceDropoutReason reason);

struct CadenceState {
    float rpm = 0.0F;
    float forward_delta_radians = 0.0F;
    uint32_t revolutions = 0;
    int64_t last_revolution_us = 0;
    int64_t revolution_duration_us = 0;
    bool revolution_completed = false;
    bool moving = false;
    float corrected_gyro_z_dps = 0.0F;
    float forward_velocity_dps = 0.0F;
    float integrated_forward_angle_degrees = 0.0F;
    float integrated_reverse_angle_degrees = 0.0F;
    float last_candidate_rpm = 0.0F;
    uint32_t imu_invalid_reads = 0;
    uint32_t rejected_revolution_periods = 0;
    uint32_t integration_gap_count = 0;
    float last_sample_interval_ms = 0.0F;
    uint32_t dropout_count = 0;
    CadenceDropoutReason last_dropout_reason = CadenceDropoutReason::None;
    int64_t last_dropout_us = 0;
};

class CadenceEstimator {
public:
    void updateConfig(const DeviceConfig &config);
    CadenceState update(const ImuSample &sample, int64_t now_us);
    CadenceState latest() const;
    void reset();

private:
    void recordDropout(CadenceDropoutReason reason, int64_t now_us);
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
