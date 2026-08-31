#include "power_estimator.h"

#include <algorithm>
#include <cmath>

#include "esp_log.h"
#include "esp_timer.h"

namespace openwatts {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr const char *kSlidingZeroTag = "sliding_zero";

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
    const bool torque_available = hx711_ready && config_.strain_calibration_valid &&
                                  std::isfinite(config_.counts_per_nm) && config_.counts_per_nm > 0.0F;

    const int32_t sliding_offset_counts = torque_available
        ? static_cast<int32_t>(std::lround(sliding_zero_correction_nm_ * config_.counts_per_nm *
                                            static_cast<float>(config_.torque_sign)))
        : 0;
    const int32_t effective_zero_offset_counts = config_.zero_offset_counts + sliding_offset_counts;

    const float unfiltered_torque = torque_available
        ? ((filtered_counts - static_cast<float>(effective_zero_offset_counts)) / config_.counts_per_nm) *
              static_cast<float>(config_.torque_sign)
        : 0.0F;

    if (!torque_available) {
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
    if (std::isfinite(cadence.forward_delta_radians) && cadence.forward_delta_radians > 0.0F) {
        // Recovery-phase and bridge-sign excursions are not useful propulsive
        // torque. Ignore them without replacing the current BLE power value
        // with zero; the completed revolution produces the next update.
        revolution_work_joules_ += std::max(0.0F, latest_.torque_nm) * cadence.forward_delta_radians;
        revolution_angle_radians_ += cadence.forward_delta_radians;
        if (!revolution_min_torque_valid_ || latest_.torque_nm < revolution_min_torque_nm_) {
            revolution_min_torque_nm_ = latest_.torque_nm;
        }
        revolution_min_torque_valid_ = true;
    }
    if (!cadence.moving || !std::isfinite(cadence.rpm) || cadence.rpm <= 0.0F || cadence.rpm > 250.0F) {
        reject(PowerRejectionReason::InvalidCadence);
        if (cadence.forward_delta_radians <= 0.0F) reset();
        return latest_;
    }
    if (cadence.last_revolution_us <= 0) {
        reject(PowerRejectionReason::StaleCadence);
        reset();
        return latest_;
    }
    if (!cadence.revolution_completed || cadence.revolution_duration_us <= 0 ||
        revolution_angle_radians_ < (2.0F * kPi * 0.90F)) {
        latest_.power_watts = has_filtered_power_
            ? static_cast<int16_t>(std::clamp(std::lround(filtered_power_), 0L, 32767L)) : 0;
        latest_.valid = has_filtered_power_;
        return latest_;
    }
    const float duration_seconds = static_cast<float>(cadence.revolution_duration_us) / 1000000.0F;
    const float measured_left_watts = std::max(0.0F, revolution_work_joules_ / duration_seconds);
    const float watts = measured_left_watts * kSingleSidedPowerMultiplier;
    revolution_work_joules_ = 0.0F;
    revolution_angle_radians_ = 0.0F;

    if (revolution_min_torque_valid_) {
        sliding_window_[sliding_window_index_] = revolution_min_torque_nm_;
        sliding_window_index_ = (sliding_window_index_ + 1U) % kSlidingWindowCapacity;
        if (sliding_window_count_ < kSlidingWindowCapacity) ++sliding_window_count_;

        if (!sliding_zero_baseline_established_) {
            // First few revolutions after warmup set the reference dead-spot
            // level; nothing is corrected until this baseline exists.
            if (++sliding_baseline_revolution_count_ >= config_.sliding_zero_baseline_revolutions &&
                sliding_window_count_ >= config_.sliding_zero_baseline_revolutions) {
                sliding_zero_baseline_nm_ = slidingWindowMedian();
                sliding_zero_baseline_established_ = true;
            }
        } else if (config_.sliding_zero_enabled &&
                   sliding_window_count_ >= config_.sliding_zero_window_revolutions) {
            const float current_nm = slidingWindowMedian();
            const float error_nm = current_nm - sliding_zero_baseline_nm_;
            // Deadband ignores noise-level wobble; a real error is walked
            // down gradually (correction_fraction) rather than corrected in
            // one step, so a single noisy revolution can't swing the zero.
            if (std::fabs(error_nm) > config_.sliding_zero_deadband_nm) {
                const float candidate_nm =
                    sliding_zero_correction_nm_ + (error_nm * config_.sliding_zero_correction_fraction);
                sliding_zero_correction_nm_ = std::clamp(
                    candidate_nm, -config_.sliding_zero_max_correction_nm, config_.sliding_zero_max_correction_nm);
            }
        }
        if (config_.debug_logging_enabled) {
            ESP_LOGI(kSlidingZeroTag,
                     "rev_min=%.3f window_med=%.3f baseline=%.3f (%s) correction=%.3f enabled=%d",
                     static_cast<double>(revolution_min_torque_nm_), static_cast<double>(slidingWindowMedian()),
                     static_cast<double>(sliding_zero_baseline_nm_),
                     sliding_zero_baseline_established_ ? "set" : "learning",
                     static_cast<double>(sliding_zero_correction_nm_), config_.sliding_zero_enabled ? 1 : 0);
        }

        // Zero timestamp means resetSlidingZero() hasn't run yet (before the
        // first ride); skip logging rather than record against a bogus epoch.
        if (sliding_zero_ride_started_us_ != 0) {
            SlidingZeroLogEntry &entry = sliding_zero_log_[sliding_zero_log_index_];
            entry.elapsed_ms = static_cast<uint32_t>(
                (esp_timer_get_time() - sliding_zero_ride_started_us_) / 1000);
            entry.revolution_min_nm = revolution_min_torque_nm_;
            entry.window_median_nm = slidingWindowMedian();
            entry.baseline_nm = sliding_zero_baseline_nm_;
            entry.correction_nm = sliding_zero_correction_nm_;
            entry.baseline_established = sliding_zero_baseline_established_;
            sliding_zero_log_index_ = (sliding_zero_log_index_ + 1U) % kSlidingZeroLogCapacity;
            if (sliding_zero_log_count_ < kSlidingZeroLogCapacity) ++sliding_zero_log_count_;
        }
        revolution_min_torque_valid_ = false;
    }

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
    revolution_work_joules_ = 0.0F;
    revolution_angle_radians_ = 0.0F;
    revolution_min_torque_valid_ = false;
}

float PowerEstimator::slidingWindowMedian() const {
    const uint8_t n = std::min<uint8_t>(sliding_window_count_, config_.sliding_zero_window_revolutions);
    float sorted[kSlidingWindowCapacity]{};
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t idx = (sliding_window_index_ + kSlidingWindowCapacity - 1U - i) % kSlidingWindowCapacity;
        sorted[i] = sliding_window_[idx];
    }
    std::sort(sorted, sorted + n);
    return n ? sorted[n / 2] : 0.0F;
}

void PowerEstimator::resetSlidingZero() {
    sliding_window_count_ = 0;
    sliding_window_index_ = 0;
    sliding_baseline_revolution_count_ = 0;
    sliding_zero_baseline_established_ = false;
    sliding_zero_baseline_nm_ = 0.0F;
    sliding_zero_correction_nm_ = 0.0F;
    revolution_min_torque_valid_ = false;
    sliding_zero_log_count_ = 0;
    sliding_zero_log_index_ = 0;
    sliding_zero_ride_started_us_ = esp_timer_get_time();
}

SlidingZeroLogEntry PowerEstimator::slidingZeroLogEntryAt(uint32_t sequential_index) const {
    if (sequential_index >= sliding_zero_log_count_) return {};
    const uint32_t oldest_index = sliding_zero_log_count_ < kSlidingZeroLogCapacity
        ? 0
        : sliding_zero_log_index_;
    const uint32_t idx = (oldest_index + sequential_index) % kSlidingZeroLogCapacity;
    return sliding_zero_log_[idx];
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
