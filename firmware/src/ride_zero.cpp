#include "ride_zero.h"

#include <algorithm>
#include <cmath>

namespace openwatts {

void RideZeroController::observe(float filtered_counts, bool fresh, int64_t now_us) {
    if (!fresh || !std::isfinite(filtered_counts)) return;
    values_[index_] = filtered_counts;
    index_ = (index_ + 1U) % kWindowSize;
    if (count_ < kWindowSize) ++count_;
    last_sample_us_ = now_us;
}

RideZeroAttempt RideZeroController::attempt(RideZeroTrigger trigger, const DeviceConfig &config,
                                             const PowerSample &sample, bool ble_ride_active, bool imu_stationary,
                                             bool calibration_active, int64_t now_us) {
    last_ = {};
    last_.trigger = trigger;
    last_.samples = count_;
    double sum = 0.0, sum_squares = 0.0;
    float minimum = count_ ? values_[0] : 0.0F, maximum = minimum;
    for (uint32_t i = 0; i < count_; ++i) {
        sum += values_[i];
        sum_squares += values_[i] * values_[i];
        minimum = std::min(minimum, values_[i]);
        maximum = std::max(maximum, values_[i]);
    }
    if (count_ > 0) last_.average = static_cast<float>(sum / count_);
    if (count_ > 1) last_.variance = static_cast<float>(
        std::max(0.0, (sum_squares - sum * sum / count_) / (count_ - 1.0)));
    last_.standard_deviation = std::sqrt(last_.variance);
    last_.range = maximum - minimum;
    auto reject = [this](RideZeroResult result) { last_.result = result; return last_; };
    if (!config.auto_ride_zero_enabled) return reject(RideZeroResult::Disabled);
    if (locked_) return reject(RideZeroResult::Locked);
    if (!config.strain_calibration_valid || !std::isfinite(config.counts_per_nm) || config.counts_per_nm <= 0.0F)
        return reject(RideZeroResult::CalibrationRequired);
    if (calibration_active) return reject(RideZeroResult::CalibrationRequired);
    if (!sample.hx711_ready) return reject(RideZeroResult::SensorUnavailable);
    if (sample.pedaling || sample.cadence_rpm > 0.1F) return reject(RideZeroResult::CadenceActive);
    if (ble_ride_active) return reject(RideZeroResult::BleRideActive);
    if (!imu_stationary) return reject(RideZeroResult::ImuUnstable);
    if (count_ < kMinimumSamples || last_sample_us_ == 0 || now_us - last_sample_us_ > 2000000LL)
        return reject(RideZeroResult::InsufficientSamples);
    if (!std::isfinite(last_.average) || !std::isfinite(last_.variance) ||
        !std::isfinite(last_.standard_deviation) || !std::isfinite(last_.range))
        return reject(RideZeroResult::InvalidData);
    // BLE connection is only a convenience opportunity. A rider may already
    // be clipping in, so never let it replace a plausible existing zero with
    // a large, stable pedal load. USB and post-ride opportunities may correct
    // larger thermal/mechanical drift after their normal stability checks.
    if (trigger == RideZeroTrigger::BleConnected &&
        std::fabs(last_.average - config.runtime_zero_offset_counts) > config.counts_per_nm * 10.0F)
        return reject(RideZeroResult::LoadedReference);
    // Compare variation around the candidate center, not its distance from the
    // old zero. Thermal drift moves the center but does not resemble a person
    // holding pedal force. Learned limits adapt slowly after trusted accepts.
    const float default_stddev = config.counts_per_nm * 0.35F;
    const float default_range = config.counts_per_nm * 3.0F;
    const float learned_stddev = config.ride_zero_baseline_stddev_counts > 0.0F
        ? config.ride_zero_baseline_stddev_counts * 2.5F : default_stddev;
    const float learned_range = config.ride_zero_baseline_range_counts > 0.0F
        ? config.ride_zero_baseline_range_counts * 1.75F : default_range;
    if (last_.standard_deviation > std::max(default_stddev, learned_stddev) ||
        last_.range > std::max(default_range, learned_range))
        return reject(RideZeroResult::UnstableStrain);
    last_.zero_offset = static_cast<int32_t>(std::lround(last_.average));
    last_.result = RideZeroResult::Accepted;
    return last_;
}

void RideZeroController::lock() { locked_ = true; }
void RideZeroController::resetLifecycle() { locked_ = false; }
bool RideZeroController::locked() const { return locked_; }
const RideZeroAttempt &RideZeroController::lastAttempt() const { return last_; }
const char *RideZeroController::triggerName(RideZeroTrigger trigger) {
    switch (trigger) {
        case RideZeroTrigger::UsbRemoved: return "USB removed";
        case RideZeroTrigger::UsbInserted: return "USB inserted";
        case RideZeroTrigger::BleConnected: return "Bluetooth connected";
        case RideZeroTrigger::BleDisconnected: return "Bluetooth disconnected";
        case RideZeroTrigger::BeforeSleep: return "Before sleep";
        case RideZeroTrigger::Stationary: return "Stationary";
    }
    return "Unknown";
}
const char *RideZeroController::resultName(RideZeroResult result) {
    switch (result) {
        case RideZeroResult::Accepted: return "Accepted";
        case RideZeroResult::Disabled: return "Disabled";
        case RideZeroResult::Locked: return "Locked for ride";
        case RideZeroResult::CalibrationRequired: return "Calibration required";
        case RideZeroResult::SensorUnavailable: return "Strain sensor unavailable";
        case RideZeroResult::CadenceActive: return "Cadence active";
        case RideZeroResult::BleRideActive: return "Bluetooth ride active";
        case RideZeroResult::ImuUnstable: return "Crank is moving";
        case RideZeroResult::InsufficientSamples: return "Insufficient fresh samples";
        case RideZeroResult::UnstableStrain: return "Strain is not stable";
        case RideZeroResult::LoadedReference: return "Possible pedal load";
        case RideZeroResult::InvalidData: return "Invalid strain data";
    }
    return "Unknown";
}

}  // namespace openwatts
