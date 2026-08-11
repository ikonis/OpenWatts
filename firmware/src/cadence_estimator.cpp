#include "cadence_estimator.h"

#include <cmath>

namespace openwatts {

void CadenceEstimator::updateConfig(const DeviceConfig &config) {
    config_ = config;
}

CadenceState CadenceEstimator::update(const ImuSample &sample, int64_t now_us) {
    latest_.forward_delta_radians = 0.0F;
    latest_.revolution_completed = false;
    latest_.revolution_duration_us = 0;
    if (!sample.valid) {
        // A transient I2C/IMU read failure is not evidence that the crank
        // stopped. Preserve the most recent accepted cadence until the normal
        // cadence timeout expires; otherwise one failed sample becomes a
        // one-frame 0 RPM / 0 W dropout over BLE.
        if (latest_.last_revolution_us == 0 ||
            now_us - latest_.last_revolution_us >
                static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL) {
            latest_.rpm = 0.0F;
        }
        latest_.moving = latest_.rpm > 0.1F;
        last_sample_us_ = 0;
        return latest_;
    }

    constexpr float kMotionThresholdDps = 5.0F;
    constexpr float kBiasLearningAlpha = 0.02F;
    const float gyro_z = sample.gyro_dps[2];
    const float corrected_z = gyro_z - gyro_z_bias_dps_;
    const float dt = last_sample_us_ > 0 ? static_cast<float>(now_us - last_sample_us_) / 1000000.0F : 0.0F;
    last_sample_us_ = now_us;

    // Installed RevA characterization: forward is negative gyro Z and
    // reverse is positive Z. Absolute crank position is deliberately unused.
    const float forward_velocity_dps = -corrected_z * static_cast<float>(config_.rotation_direction_convention);
    if (std::fabs(corrected_z) < kMotionThresholdDps) {
        gyro_z_bias_dps_ += (gyro_z - gyro_z_bias_dps_) * kBiasLearningAlpha;
        if (stationary_since_us_ == 0) stationary_since_us_ = now_us;
        if (now_us - stationary_since_us_ >= static_cast<int64_t>(config_.imu_stationary_timeout_ms) * 1000LL) {
            forward_angle_degrees_ = 0.0F;
            reverse_angle_degrees_ = 0.0F;
            motion_started_us_ = 0;
            previous_revolution_us_ = 0;
        }
    } else {
        stationary_since_us_ = 0;
    }

    if (dt > 0.0F && dt < 0.25F && forward_velocity_dps >= kMotionThresholdDps) {
        reverse_angle_degrees_ = 0.0F;
        if (motion_started_us_ == 0) motion_started_us_ = now_us;
        const float forward_delta_degrees = forward_velocity_dps * dt;
        latest_.forward_delta_radians = forward_delta_degrees * 0.01745329251994329577F;
        forward_angle_degrees_ += forward_delta_degrees;
        while (forward_angle_degrees_ >= 360.0F) {
            forward_angle_degrees_ -= 360.0F;
            const int64_t period_start_us = previous_revolution_us_ > 0 ? previous_revolution_us_ : motion_started_us_;
            const int64_t period_us = now_us - period_start_us;
            previous_revolution_us_ = now_us;
            latest_.last_revolution_us = now_us;
            latest_.revolution_duration_us = period_us;
            latest_.revolution_completed = true;
            latest_.revolutions += 1;
            if (period_us > 0) {
                const float candidate_rpm = 60000000.0F / static_cast<float>(period_us);
                if (std::isfinite(candidate_rpm) && candidate_rpm >= config_.minimum_cadence_rpm &&
                    candidate_rpm <= config_.maximum_cadence_rpm) latest_.rpm = candidate_rpm;
            }
        }
    } else if (dt > 0.0F && dt < 0.25F && forward_velocity_dps <= -kMotionThresholdDps) {
        // A clipped-in rider and drivetrain vibration can produce brief
        // reverse gyro spikes during an otherwise forward revolution. Ignore
        // those instead of erasing cadence immediately. A deliberate reverse
        // quarter-turn still resets the forward tracker deterministically.
        const bool forward_cadence_recent = latest_.last_revolution_us > 0 &&
            now_us - latest_.last_revolution_us <=
                static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL;
        if (!forward_cadence_recent) reverse_angle_degrees_ += -forward_velocity_dps * dt;
        if (!forward_cadence_recent && reverse_angle_degrees_ >= 90.0F) {
            forward_angle_degrees_ = 0.0F;
            reverse_angle_degrees_ = 0.0F;
            motion_started_us_ = 0;
            previous_revolution_us_ = 0;
            latest_.rpm = 0.0F;
        }
    }

    if (latest_.last_revolution_us == 0 ||
        (now_us - latest_.last_revolution_us) > static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL) {
        latest_.rpm = 0.0F;
    }
    latest_.moving = latest_.rpm > 0.1F;
    return latest_;
}

CadenceState CadenceEstimator::latest() const {
    return latest_;
}

void CadenceEstimator::reset() {
    latest_ = {};
    gyro_z_bias_dps_ = 0.0F;
    forward_angle_degrees_ = 0.0F;
    reverse_angle_degrees_ = 0.0F;
    last_sample_us_ = 0;
    motion_started_us_ = 0;
    stationary_since_us_ = 0;
    previous_revolution_us_ = 0;
}

}  // namespace openwatts
