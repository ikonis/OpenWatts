#pragma once

#include <cstdint>

namespace openwatts {

enum class RuntimeMode : uint8_t {
    Normal,
    UsbMaintenance,
    TimerDecision,
    Report,
};

enum class WakeClass : uint8_t {
    PowerOn,
    Usb,
    Motion,
    Timer,
    SoftwareReport,
    Unknown,
};

const char *runtimeName(RuntimeMode mode);
const char *wakeName(WakeClass wake);

}  // namespace openwatts
