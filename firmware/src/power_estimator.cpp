#include "power_estimator.h"

#include <algorithm>
#include <cmath>

namespace openwatts {
namespace {
constexpr float kPi = 3.14159265358979323846F;
}

void PowerEstimator::updateConfig(const DeviceConfig &config) {
    config_ = config;
}

PowerSample PowerEstimator::update(int32_t raw_counts, float filtered_counts, float noise_estimate, bool hx711_ready,
                                   const CadenceState &cadence) {
    const float unfiltered_torque = hx711_ready
        ? ((filtered_counts - static_cast<float>(config_.zero_offset_counts)) / config_.counts_per_nm) *
              static_cast<float>(config_.torque_sign)
        : 0.0F;

    if (!hx711_ready) {
        latest_.torque_nm = 0.0F;
        has_filtered_torque_ = false;
    } else if (!has_filtered_torque_) {
        latest_.torque_nm = unfiltered_torque;
        has_filtered_torque_ = true;
    } else {
        latest_.torque_nm =
            (latest_.torque_nm * config_.hx711_smoothing) + (unfiltered_torque * (1.0F - config_.hx711_smoothing));
    }

    const float angular_velocity = cadence.rpm * 2.0F * kPi / 60.0F;
    const float watts = latest_.torque_nm * angular_velocity;

    latest_.raw_counts = raw_counts;
    latest_.filtered_counts = hx711_ready ? filtered_counts : 0.0F;
    latest_.noise_estimate = hx711_ready ? noise_estimate : 0.0F;
    latest_.cadence_rpm = cadence.rpm;
    latest_.power_watts = static_cast<int16_t>(std::clamp(std::lround(watts), -32768L, 32767L));
    latest_.cumulative_crank_revolutions = static_cast<uint16_t>(cadence.revolutions & 0xFFFFU);
    latest_.last_crank_event_time =
        static_cast<uint16_t>(((cadence.last_revolution_us * 1024LL) / 1000000LL) & 0xFFFF);
    latest_.hx711_ready = hx711_ready;
    latest_.pedaling = cadence.moving;
    return latest_;
}

PowerSample PowerEstimator::latest() const {
    return latest_;
}

}  // namespace openwatts
