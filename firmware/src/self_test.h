#pragma once

#include <cstdint>

#include "esp_err.h"

namespace openwatts {

class Hx711;
class Lsm6ds3;
struct SelfTestResult {
    bool imu_detected = false;
    uint8_t imu_who_am_i = 0;
    bool hx711_ready = false;
    int32_t hx711_raw = 0;
    uint32_t battery_mv = 0;
    bool usb_present = false;
    bool charge_active = false;
    bool led_tested = false;
};

class SelfTest {
public:
    SelfTestResult run(Lsm6ds3 &imu, Hx711 &hx711);
    static const char *summary(const SelfTestResult &result);
};

}  // namespace openwatts
