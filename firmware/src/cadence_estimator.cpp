#include "cadence_estimator.h"

#include <cmath>

#include "esp_log.h"

namespace openwatts {

namespace {
constexpr const char *kTag = "cadence";
}

const char *cadenceDropoutReasonName(CadenceDropoutReason reason) {
    switch (reason) {
        case CadenceDropoutReason::RevolutionTimeout: return "revolution_timeout";
        case CadenceDropoutReason::ImuInvalidTimeout: return "imu_invalid_timeout";
        case CadenceDropoutReason::ReverseReset: return "reverse_reset";
        case CadenceDropoutReason::None:
        default: return "none";
    }
}

void CadenceEstimator::recordDropout(CadenceDropoutReason reason, int64_t now_us) {
    if (latest_.rpm <= 0.1F) return;
    latest_.dropout_count += 1;
    latest_.last_dropout_reason = reason;
    latest_.last_dropout_us = now_us;
    ESP_LOGW(kTag, "dropout reason=%s rpm=%.1f age_ms=%lld angle=%.1f reverse=%.1f gyro=%.1f forward=%.1f "
                   "invalid=%u rejected=%u gaps=%u dt_ms=%.1f",
             cadenceDropoutReasonName(reason), static_cast<double>(latest_.rpm),
             latest_.last_revolution_us > 0 ? (now_us - latest_.last_revolution_us) / 1000LL : -1LL,
             static_cast<double>(forward_angle_degrees_), static_cast<double>(reverse_angle_degrees_),
             static_cast<double>(latest_.corrected_gyro_z_dps), static_cast<double>(latest_.forward_velocity_dps),
             static_cast<unsigned>(latest_.imu_invalid_reads),
             static_cast<unsigned>(latest_.rejected_revolution_periods),
             static_cast<unsigned>(latest_.integration_gap_count),
             static_cast<double>(latest_.last_sample_interval_ms));
}

void CadenceEstimator::updateConfig(const DeviceConfig &config) {
    config_ = config;
}

CadenceState CadenceEstimator::update(const ImuSample &sample, int64_t now_us) {
    latest_.forward_delta_radians = 0.0F;
    latest_.revolution_completed = false;
    latest_.revolution_duration_us = 0;
    if (!sample.valid) {
        latest_.imu_invalid_reads += 1;
        // A transient I2C/IMU read failure is not evidence that the crank
        // stopped. Preserve the most recent accepted cadence until the normal
        // cadence timeout expires; otherwise one failed sample becomes a
        // one-frame 0 RPM / 0 W dropout over BLE.
        if (latest_.last_revolution_us == 0 ||
            now_us - latest_.last_revolution_us >
                static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL) {
            recordDropout(CadenceDropoutReason::ImuInvalidTimeout, now_us);
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
    latest_.last_sample_interval_ms = dt * 1000.0F;
    last_sample_us_ = now_us;

    // Installed RevA characterization: forward is negative gyro Z and
    // reverse is positive Z. Absolute crank position is deliberately unused.
    const float forward_velocity_dps = -corrected_z * static_cast<float>(config_.rotation_direction_convention);
    latest_.corrected_gyro_z_dps = corrected_z;
    latest_.forward_velocity_dps = forward_velocity_dps;
    if (dt >= 0.25F) {
        latest_.integration_gap_count += 1;
        ESP_LOGW(kTag, "integration gap dt_ms=%.1f rpm=%.1f angle=%.1f gyro=%.1f",
                 static_cast<double>(dt * 1000.0F), static_cast<double>(latest_.rpm),
                 static_cast<double>(forward_angle_degrees_), static_cast<double>(corrected_z));
    }
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
                latest_.last_candidate_rpm = candidate_rpm;
                if (std::isfinite(candidate_rpm) && candidate_rpm >= config_.minimum_cadence_rpm &&
                    candidate_rpm <= config_.maximum_cadence_rpm) {
                    latest_.rpm = candidate_rpm;
                    if (config_.ride_diagnostics_enabled) {
                        ESP_LOGI(kTag, "revolution rpm=%.1f period_ms=%.1f residual_angle=%.1f gyro=%.1f",
                                 static_cast<double>(candidate_rpm), static_cast<double>(period_us / 1000.0F),
                                 static_cast<double>(forward_angle_degrees_), static_cast<double>(corrected_z));
                    }
                } else {
                    latest_.rejected_revolution_periods += 1;
                    ESP_LOGW(kTag, "rejected revolution rpm=%.1f period_ms=%.1f allowed=%u..%u angle=%.1f",
                             static_cast<double>(candidate_rpm), static_cast<double>(period_us / 1000.0F),
                             static_cast<unsigned>(config_.minimum_cadence_rpm),
                             static_cast<unsigned>(config_.maximum_cadence_rpm),
                             static_cast<double>(forward_angle_degrees_));
                }
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
            ESP_LOGW(kTag, "reverse reset reverse_angle=%.1f forward_angle=%.1f gyro=%.1f",
                     static_cast<double>(reverse_angle_degrees_), static_cast<double>(forward_angle_degrees_),
                     static_cast<double>(corrected_z));
            forward_angle_degrees_ = 0.0F;
            reverse_angle_degrees_ = 0.0F;
            motion_started_us_ = 0;
            previous_revolution_us_ = 0;
            recordDropout(CadenceDropoutReason::ReverseReset, now_us);
            latest_.rpm = 0.0F;
        }
    }

    if (latest_.last_revolution_us == 0 ||
        (now_us - latest_.last_revolution_us) > static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL) {
        recordDropout(CadenceDropoutReason::RevolutionTimeout, now_us);
        latest_.rpm = 0.0F;
    }
    latest_.integrated_forward_angle_degrees = forward_angle_degrees_;
    latest_.integrated_reverse_angle_degrees = reverse_angle_degrees_;
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
