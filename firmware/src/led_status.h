#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "esp_timer.h"

namespace openwatts {

enum class LedPattern : uint8_t {
    Off,
    Solid,
    Breathing,
    Boot,
    Ota,
    CalibrationRequired,
    FatalSos,
};

class LedStatusController {
public:
    esp_err_t begin();
    void setAutomaticState(bool usb_present, bool maintenance, bool ble_connected,
                           bool calibration_required, bool fatal_error);
    void setSleeping(bool sleeping);
    void setOtaActive(bool active);

private:
    static void timerCallback(void *arg);
    void updateOutput();
    LedPattern effectivePattern() const;

    esp_timer_handle_t timer_ = nullptr;
    std::atomic<LedPattern> automatic_{LedPattern::Breathing};
    std::atomic<bool> sleeping_{false};
    std::atomic<bool> ota_active_{false};
    int64_t boot_started_us_ = 0;
    int64_t pattern_started_us_ = 0;
    LedPattern rendered_{LedPattern::Off};
};

LedStatusController &ledStatus();

}  // namespace openwatts
