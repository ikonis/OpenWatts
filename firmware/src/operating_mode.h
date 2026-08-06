#pragma once

#include <cstdint>

namespace openwatts {

struct DeviceConfig;

enum class OperatingMode : uint8_t {
    Normal = 0,
    Maintenance = 1,
};

class OperatingPolicy {
public:
    static bool isMaintenance(const DeviceConfig &config);
    static bool permitsWifi(const DeviceConfig &config, bool usb_present, bool report_runtime = false);
    static bool permitsMaintenanceTools(const DeviceConfig &config);
    static bool permitsInactivitySleep(const DeviceConfig &config);
    static uint32_t mqttEvaluationIntervalSeconds(const DeviceConfig &config, bool usb_present);
    static const char *name(OperatingMode mode);
    static const char *value(OperatingMode mode);
};

}  // namespace openwatts
