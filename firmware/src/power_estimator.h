#pragma once

#include <cstdint>

#include "cadence_estimator.h"
#include "config.h"

namespace openwatts {

// RevA measures strain on the left crank only.  Cycling applications expect
// total rider power, so this converts the measured-side result using the
// conventional single-sided estimate.  It is intentionally not configurable.
constexpr float kSingleSidedPowerMultiplier = 2.0F;

struct PowerSample {
    int32_t raw_counts = 0;
    float filtered_counts = 0.0F;
    float noise_estimate = 0.0F;
    float torque_nm = 0.0F;
    float cadence_rpm = 0.0F;
    // Estimated total rider cycling power in watts.  Raw HX711 data and
    // torque_nm remain left-crank measurements.
    int16_t power_watts = 0;
    uint16_t cumulative_crank_revolutions = 0;
    uint16_t last_crank_event_time = 0;
    bool hx711_ready = false;
    bool pedaling = false;
    bool valid = false;
};

enum class PowerRejectionReason : uint8_t {
    None,
    Hx711Unavailable,
    CalibrationRequired,
    InvalidTorque,
    NegativeTorque,
    InvalidCadence,
    StaleCadence,
    NonFinitePower,
    AboveMaximum,
};

class PowerEstimator {
public:
    void updateConfig(const DeviceConfig &config);
    PowerSample update(int32_t raw_counts, float filtered_counts, float noise_estimate, bool hx711_ready,
                       const CadenceState &cadence);
    PowerSample latest() const;
    PowerRejectionReason lastRejectionReason() const;
    uint32_t rejectedSamples() const;
    void reset();
    static const char *rejectionName(PowerRejectionReason reason);

private:
    DeviceConfig config_{};
    PowerSample latest_{};
    bool has_filtered_torque_ = false;
    float median_window_[5]{};
    uint8_t median_count_ = 0;
    uint8_t median_index_ = 0;
    float filtered_power_ = 0.0F;
    bool has_filtered_power_ = false;
    float revolution_work_joules_ = 0.0F;
    float revolution_angle_radians_ = 0.0F;
    PowerRejectionReason last_rejection_ = PowerRejectionReason::None;
    uint32_t rejected_samples_ = 0;
};

}  // namespace openwatts
