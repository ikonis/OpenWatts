#include "ride_zero.h"

#include <cmath>

namespace openwatts {
namespace { constexpr float kMaximumStableStdDevCounts = 100.0F; }

void RideZeroController::observe(float filtered_counts, bool fresh, int64_t now_us) {
    if (!fresh || !std::isfinite(filtered_counts)) return;
    values_[index_] = filtered_counts;
    index_ = (index_ + 1U) % kWindowSize;
    if (count_ < kWindowSize) ++count_;
    last_sample_us_ = now_us;
}

RideZeroAttempt RideZeroController::attempt(RideZeroTrigger trigger, const DeviceConfig &config,
                                             const PowerSample &sample, bool ble_ride_active,
                                             bool calibration_active, int64_t now_us) {
    last_ = {};
    last_.trigger = trigger;
    last_.samples = count_;
    double sum = 0.0, sum_squares = 0.0;
    for (uint32_t i = 0; i < count_; ++i) { sum += values_[i]; sum_squares += values_[i] * values_[i]; }
    if (count_ > 0) last_.average = static_cast<float>(sum / count_);
    if (count_ > 1) last_.variance = static_cast<float>(
        std::max(0.0, (sum_squares - sum * sum / count_) / (count_ - 1.0)));
    auto reject = [this](RideZeroResult result) { last_.result = result; return last_; };
    if (!config.auto_ride_zero_enabled) return reject(RideZeroResult::Disabled);
    if (locked_) return reject(RideZeroResult::Locked);
    if (!config.strain_calibration_valid || !std::isfinite(config.counts_per_nm) || config.counts_per_nm <= 0.0F)
        return reject(RideZeroResult::CalibrationRequired);
    if (calibration_active) return reject(RideZeroResult::CalibrationRequired);
    if (!sample.hx711_ready) return reject(RideZeroResult::SensorUnavailable);
    if (sample.pedaling || sample.cadence_rpm > 0.1F) return reject(RideZeroResult::CadenceActive);
    if (ble_ride_active) return reject(RideZeroResult::BleRideActive);
    if (count_ < kMinimumSamples || last_sample_us_ == 0 || now_us - last_sample_us_ > 2000000LL)
        return reject(RideZeroResult::InsufficientSamples);
    if (!std::isfinite(last_.average) || !std::isfinite(last_.variance))
        return reject(RideZeroResult::InvalidData);
    if (last_.variance > kMaximumStableStdDevCounts * kMaximumStableStdDevCounts)
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
        case RideZeroResult::InsufficientSamples: return "Insufficient fresh samples";
        case RideZeroResult::UnstableStrain: return "Strain is not stable";
        case RideZeroResult::InvalidData: return "Invalid strain data";
    }
    return "Unknown";
}

}  // namespace openwatts
