#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace openwatts {

class Hx711 {
public:
    Hx711(gpio_num_t dout, gpio_num_t sck);
    esp_err_t begin();
    esp_err_t prepareForSleep();
    esp_err_t resumeFromSleep();
    bool read(int32_t &value, uint32_t timeout_ms = 100);
    void observe(bool success, int32_t raw_value, float smoothing);
    bool ready() const;
    float filtered() const;
    float noiseEstimate() const;
    uint32_t readFailures() const;

private:
    gpio_num_t dout_;
    gpio_num_t sck_;
    bool has_filter_ = false;
    bool ready_ = false;
    float filtered_ = 0.0F;
    float noise_estimate_ = 0.0F;
    // Successful conversions add confidence; timeouts remove it.  This keeps
    // a floating DOUT pin from being mistaken for a connected bridge.
    int8_t signal_confidence_ = 0;
    uint32_t consecutive_failures_ = 0;
    uint32_t read_failures_ = 0;
};

}  // namespace openwatts
