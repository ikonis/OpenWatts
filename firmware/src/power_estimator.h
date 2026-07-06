#pragma once

#include <cstdint>

#include "cadence_estimator.h"
#include "config.h"

namespace openwatts {

struct PowerSample {
    int32_t raw_counts = 0;
    float filtered_counts = 0.0F;
    float noise_estimate = 0.0F;
    float torque_nm = 0.0F;
    float cadence_rpm = 0.0F;
    int16_t power_watts = 0;
    uint16_t cumulative_crank_revolutions = 0;
    uint16_t last_crank_event_time = 0;
    bool hx711_ready = false;
    bool pedaling = false;
};

class PowerEstimator {
public:
    void updateConfig(const DeviceConfig &config);
    PowerSample update(int32_t raw_counts, float filtered_counts, float noise_estimate, bool hx711_ready,
                       const CadenceState &cadence);
    PowerSample latest() const;

private:
    DeviceConfig config_{};
    PowerSample latest_{};
    bool has_filtered_torque_ = false;
};

}  // namespace openwatts
