#pragma once

#include <cstdint>

namespace openwatts {

enum class RotationDirection : int8_t { Unknown = 0, Forward = 1, Reverse = -1 };
enum class RotationValidity : uint8_t {
    Unavailable,
    Calibrating,
    Stationary,
    Valid,
    Stale,
    SensorMissing,
    SensorFailed,
    InsufficientPostWakeData,
    ImplausibleInterval,
    Reverse,
};

struct RotationSample {
    int64_t timestamp_us = 0;
    float angle_degrees = 0.0F;       // [0, 360), reference not yet calibrated.
    float angular_velocity_rad_s = 0.0F;
    float cadence_rpm = 0.0F;
    RotationDirection direction = RotationDirection::Unknown;
    RotationValidity validity = RotationValidity::Unavailable;
    float confidence = 0.0F;
    uint32_t revolution_id = 0;
    bool crossed_revolution_boundary = false;
};

struct CompletedRevolution {
    uint32_t revolution_id = 0;
    int64_t duration_us = 0;
    float average_cadence_rpm = 0.0F;
    float average_angular_velocity_rad_s = 0.0F;
    uint32_t sample_count = 0;
    float angular_coverage_degrees = 0.0F;
    float average_torque_nm = 0.0F;
    float integrated_torque_angle = 0.0F;
    float raw_power_watts = 0.0F;
    float filtered_power_watts = 0.0F;
    RotationValidity validity = RotationValidity::Unavailable;
    float confidence = 0.0F;
};

float normalizeAngleDegrees(float degrees);

class RotationProvider {
public:
    virtual ~RotationProvider() = default;
    virtual RotationSample update(int64_t timestamp_us) = 0;
    virtual void reset() = 0;
};

class PowerEstimatorStrategy {
public:
    virtual ~PowerEstimatorStrategy() = default;
    virtual void reset() = 0;
};

}  // namespace openwatts
