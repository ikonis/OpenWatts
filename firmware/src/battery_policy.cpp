#include "battery_policy.h"

#include <algorithm>

namespace openwatts {

BatteryPolicy::BatteryPolicy(const DeviceConfig &config) : config_(config) {}

BatteryState BatteryPolicy::classify(float voltage) const {
    if (voltage < 2.0F || voltage > 4.35F) return BatteryState::Invalid;
    if (voltage <= config_.battery_protection_voltage) return BatteryState::Protection;
    if (voltage <= config_.battery_critical_voltage) return BatteryState::Critical;
    if (voltage <= config_.battery_charge_now_voltage) return BatteryState::ChargeNow;
    if (voltage <= config_.battery_charge_soon_voltage) return BatteryState::ChargeSoon;
    return BatteryState::Healthy;
}

BatteryState BatteryPolicy::qualify(const BatteryReading &reading) {
    const BatteryState observed = reading.valid ? classify(reading.voltage) : BatteryState::Invalid;
    if (observed == state_) {
        candidate_ = observed;
        candidate_count_ = 0;
        return state_;
    }
    if (observed != candidate_) {
        candidate_ = observed;
        candidate_count_ = 1;
    } else if (candidate_count_ < UINT8_MAX) {
        ++candidate_count_;
    }
    const uint8_t required = std::max<uint8_t>(1, config_.battery_qualification_count);
    if (candidate_count_ >= required) {
        state_ = candidate_;
        candidate_count_ = 0;
    }
    return state_;
}

BatteryState BatteryPolicy::state() const { return state_; }

const char *BatteryPolicy::name(BatteryState state) {
    switch (state) {
        case BatteryState::Invalid: return "Invalid";
        case BatteryState::Healthy: return "Healthy";
        case BatteryState::ChargeSoon: return "Charge Soon";
        case BatteryState::ChargeNow: return "Charge Now";
        case BatteryState::Critical: return "Critical";
        case BatteryState::Protection: return "Protection";
    }
    return "Invalid";
}

}  // namespace openwatts
