#include "operating_mode.h"

#include "config.h"

namespace openwatts {

bool OperatingPolicy::isMaintenance(const DeviceConfig &config) {
    return config.operating_mode == OperatingMode::Maintenance;
}

bool OperatingPolicy::permitsWifi(const DeviceConfig &config, bool usb_present, bool report_runtime) {
    return usb_present || report_runtime || isMaintenance(config);
}

bool OperatingPolicy::permitsMaintenanceTools(const DeviceConfig &config) {
    return isMaintenance(config);
}

bool OperatingPolicy::permitsInactivitySleep(const DeviceConfig &config) {
    return !isMaintenance(config);
}

uint32_t OperatingPolicy::mqttEvaluationIntervalSeconds(const DeviceConfig &config, bool usb_present) {
    if (isMaintenance(config)) return usb_present ? 5U : 30U;
    return usb_present ? 5U : config.battery_check_interval_seconds;
}

const char *OperatingPolicy::name(OperatingMode mode) {
    return mode == OperatingMode::Maintenance ? "Maintenance" : "Normal";
}

const char *OperatingPolicy::value(OperatingMode mode) {
    return mode == OperatingMode::Maintenance ? "maintenance" : "normal";
}

}  // namespace openwatts
