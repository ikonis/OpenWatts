#include "runtime.h"

namespace openwatts {

const char *runtimeName(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::Normal: return "Riding";
        case RuntimeMode::UsbMaintenance: return "USB Maintenance";
        case RuntimeMode::TimerDecision: return "Battery Check";
        case RuntimeMode::Report: return "Battery Report";
    }
    return "Unknown";
}

const char *wakeName(WakeClass wake) {
    switch (wake) {
        case WakeClass::PowerOn: return "Power on";
        case WakeClass::Usb: return "USB connected";
        case WakeClass::Motion: return "Motion";
        case WakeClass::Timer: return "Scheduled battery check";
        case WakeClass::SoftwareReport: return "Scheduled battery report";
        case WakeClass::Unknown: return "Unknown";
    }
    return "Unknown";
}

}  // namespace openwatts
