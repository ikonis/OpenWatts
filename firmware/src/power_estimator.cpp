#include "power_estimator.h"

#include <algorithm>
#include <cmath>

namespace openwatts {
namespace {
constexpr float kPi = 3.14159265358979323846F;

float median(const float *values, uint8_t count) {
    float sorted[5]{};
    for (uint8_t i = 0; i < count; ++i) sorted[i] = values[i];
    std::sort(sorted, sorted + count);
    return sorted[count / 2];
}
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

    latest_.raw_counts = raw_counts;
    latest_.filtered_counts = hx711_ready ? filtered_counts : 0.0F;
    latest_.noise_estimate = hx711_ready ? noise_estimate : 0.0F;
    latest_.cadence_rpm = cadence.rpm;
    latest_.valid = false;
    latest_.power_watts = 0;
    latest_.cumulative_crank_revolutions = static_cast<uint16_t>(cadence.revolutions & 0xFFFFU);
    latest_.last_crank_event_time =
        static_cast<uint16_t>(((cadence.last_revolution_us * 1024LL) / 1000000LL) & 0xFFFF);
    latest_.hx711_ready = hx711_ready;
    latest_.pedaling = cadence.moving;

    auto reject = [this](PowerRejectionReason reason) {
        last_rejection_ = reason;
        ++rejected_samples_;
        latest_.valid = false;
        latest_.power_watts = 0;
    };
    if (!hx711_ready) {
        reject(PowerRejectionReason::Hx711Unavailable);
        reset();
        return latest_;
    }
    if (!config_.strain_calibration_valid) {
        reject(PowerRejectionReason::CalibrationRequired);
        reset();
        return latest_;
    }
    if (!std::isfinite(latest_.torque_nm)) {
        reject(PowerRejectionReason::InvalidTorque);
        reset();
        return latest_;
    }
    if (latest_.torque_nm < 0.0F) {
        reject(PowerRejectionReason::NegativeTorque);
        return latest_;
    }
    if (!cadence.moving || !std::isfinite(cadence.rpm) || cadence.rpm <= 0.0F || cadence.rpm > 250.0F) {
        reject(PowerRejectionReason::InvalidCadence);
        reset();
        return latest_;
    }
    if (cadence.last_revolution_us <= 0) {
        reject(PowerRejectionReason::StaleCadence);
        reset();
        return latest_;
    }
    const float watts = latest_.torque_nm * cadence.rpm * 2.0F * kPi / 60.0F;
    if (!std::isfinite(watts)) {
        reject(PowerRejectionReason::NonFinitePower);
        return latest_;
    }
    if (watts > static_cast<float>(config_.maximum_valid_power_watts)) {
        reject(PowerRejectionReason::AboveMaximum);
        return latest_;
    }

    median_window_[median_index_] = watts;
    median_index_ = (median_index_ + 1U) % 5U;
    if (median_count_ < 5U) ++median_count_;
    const float robust = median(median_window_, median_count_);
    const float alpha = std::clamp(config_.power_filter_alpha, 0.05F, 1.0F);
    if (!has_filtered_power_) {
        filtered_power_ = robust;
        has_filtered_power_ = true;
    } else {
        filtered_power_ += alpha * (robust - filtered_power_);
    }
    latest_.power_watts = static_cast<int16_t>(std::clamp(std::lround(filtered_power_), 0L, 32767L));
    latest_.valid = true;
    last_rejection_ = PowerRejectionReason::None;
    return latest_;
}

PowerSample PowerEstimator::latest() const {
    return latest_;
}

PowerRejectionReason PowerEstimator::lastRejectionReason() const { return last_rejection_; }
uint32_t PowerEstimator::rejectedSamples() const { return rejected_samples_; }

void PowerEstimator::reset() {
    median_count_ = 0;
    median_index_ = 0;
    has_filtered_power_ = false;
    filtered_power_ = 0.0F;
}

const char *PowerEstimator::rejectionName(PowerRejectionReason reason) {
    switch (reason) {
        case PowerRejectionReason::None: return "None";
        case PowerRejectionReason::Hx711Unavailable: return "HX711 unavailable";
        case PowerRejectionReason::CalibrationRequired: return "Calibration required";
        case PowerRejectionReason::InvalidTorque: return "Invalid torque";
        case PowerRejectionReason::NegativeTorque: return "Negative torque";
        case PowerRejectionReason::InvalidCadence: return "Invalid cadence";
        case PowerRejectionReason::StaleCadence: return "Stale cadence";
        case PowerRejectionReason::NonFinitePower: return "Non-finite power";
        case PowerRejectionReason::AboveMaximum: return "Above maximum";
    }
    return "Unknown";
}

}  // namespace openwatts
