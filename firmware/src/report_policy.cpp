#include "report_policy.h"

#include <cmath>

namespace openwatts {

ReportReason decideBatteryReport(const DeviceConfig &config, const BatteryReading &reading,
                                 BatteryState state, const ReportHistory &history,
                                 uint64_t now_seconds) {
    if (!reading.valid) {
        return ReportReason::None;
    }
    if (!history.has_success) return ReportReason::Boot;
    if (state != history.last_state) return ReportReason::BatteryState;
    if (std::fabs(reading.voltage - history.last_voltage) >= config.battery_report_voltage_delta) {
        return ReportReason::VoltageChange;
    }
    if (history.retry_pending &&
        now_seconds - history.last_attempt_seconds >= config.battery_report_retry_interval_seconds) {
        return ReportReason::Retry;
    }
    if (now_seconds - history.last_success_seconds >= config.battery_heartbeat_interval_seconds) {
        return ReportReason::Heartbeat;
    }
    return ReportReason::None;
}

const char *reportReasonName(ReportReason reason) {
    switch (reason) {
        case ReportReason::None: return "None";
        case ReportReason::Boot: return "Boot";
        case ReportReason::BatteryState: return "Battery State";
        case ReportReason::VoltageChange: return "Voltage Change";
        case ReportReason::Heartbeat: return "Heartbeat";
        case ReportReason::Retry: return "Retry";
        case ReportReason::UsbConnected: return "USB Connected";
        case ReportReason::UsbDisconnected: return "USB Disconnected";
        case ReportReason::Manual: return "Manual";
    }
    return "None";
}

}  // namespace openwatts
