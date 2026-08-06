#pragma once

#include <cstdint>

#include "config.h"

namespace openwatts {

enum class BatteryState : uint8_t {
    Invalid,
    Healthy,
    ChargeSoon,
    ChargeNow,
    Critical,
    Protection,
};

struct BatteryReading {
    float voltage = 0.0F;
    uint8_t estimated_percent = 0;
    bool valid = false;
};

class BatteryPolicy {
public:
    explicit BatteryPolicy(const DeviceConfig &config);
    void updateConfig(const DeviceConfig &config);
    BatteryState qualify(const BatteryReading &reading);
    BatteryState state() const;
    static const char *name(BatteryState state);

private:
    BatteryState classify(float voltage) const;
    DeviceConfig config_{};
    BatteryState state_ = BatteryState::Invalid;
    BatteryState candidate_ = BatteryState::Invalid;
    uint8_t candidate_count_ = 0;
};

}  // namespace openwatts
