#pragma once

#include <cstdint>

#include "battery_policy.h"
#include "config.h"

namespace openwatts {

enum class ReportReason : uint8_t {
    None,
    Boot,
    BatteryState,
    VoltageChange,
    Heartbeat,
    Retry,
    UsbConnected,
    UsbDisconnected,
    Manual,
};

struct ReportHistory {
    bool has_success = false;
    float last_voltage = 0.0F;
    BatteryState last_state = BatteryState::Invalid;
    uint64_t last_success_seconds = 0;
    uint64_t last_attempt_seconds = 0;
    bool retry_pending = false;
};

ReportReason decideBatteryReport(const DeviceConfig &config, const BatteryReading &reading,
                                 BatteryState state, const ReportHistory &history,
                                 uint64_t now_seconds);
const char *reportReasonName(ReportReason reason);

}  // namespace openwatts
