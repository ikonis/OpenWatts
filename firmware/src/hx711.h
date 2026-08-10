#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace openwatts {

enum class Hx711PollResult : uint8_t {
    Sample,
    Waiting,
    Failure,
};

class Hx711 {
public:
    Hx711(gpio_num_t dout, gpio_num_t sck);
    esp_err_t begin();
    esp_err_t prepareForSleep();
    esp_err_t resumeFromSleep();
    bool read(int32_t &value, uint32_t timeout_ms = 100);
    Hx711PollResult poll(int32_t &value);
    void observe(bool success, int32_t raw_value, float smoothing);
    bool ready() const;
    int32_t lastRawCounts() const;
    float filtered() const;
    float noiseEstimate() const;
    float sampleRateHz() const;
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
    int64_t monitoring_started_us_ = 0;
    int64_t last_sample_us_ = 0;
    int64_t last_failure_us_ = 0;
    float sample_rate_hz_ = 0.0F;
    int64_t rate_window_started_us_ = 0;
    uint32_t rate_window_samples_ = 0;
    int32_t last_raw_counts_ = 0;
};

}  // namespace openwatts
