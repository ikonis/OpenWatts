#pragma once

#include <array>
#include <cstdint>

#include "cadence_estimator.h"
#include "config.h"

namespace openwatts {

// RevA measures strain on the left crank only.  Cycling applications expect
// total rider power, so this converts the measured-side result using the
// conventional single-sided estimate.  It is intentionally not configurable.
constexpr float kSingleSidedPowerMultiplier = 2.0F;

// One entry per completed pedal revolution while the sliding-zero tracker is
// running. Recorded unconditionally (no start/stop, no toggle) into a fixed
// ring buffer so a ride's worth of history can be pulled over Wi-Fi/HTTP
// afterward without needing a USB/serial connection to the device.
struct SlidingZeroLogEntry {
    uint32_t elapsed_ms = 0;
    float revolution_min_nm = 0.0F;
    float window_median_nm = 0.0F;
    float baseline_nm = 0.0F;
    float correction_nm = 0.0F;
    bool baseline_established = false;
    // Peak-to-trough range this revolution (window-median), and whether it
    // was elevated enough above the learned baseline range to be treated as
    // real rider effort and have correction learning skipped this revolution.
    float range_nm = 0.0F;
    bool effort_suppressed = false;
};

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

    // Sliding in-ride zero: call once when a fresh stationary/manual zero is
    // accepted and the ride lifecycle locks in (transition into a locked
    // ride only). Starts a new baseline-learning window.
    void resetSlidingZero();

    // Ring-buffer log access, oldest-to-newest by sequential_index
    // [0, slidingZeroLogCount()).
    static constexpr uint32_t kSlidingZeroLogCapacity = 500;
    uint32_t slidingZeroLogCount() const { return sliding_zero_log_count_; }
    SlidingZeroLogEntry slidingZeroLogEntryAt(uint32_t sequential_index) const;

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

    // Sliding in-ride zero (low-torque-per-revolution tracking).
    float revolution_min_torque_nm_ = 0.0F;
    bool revolution_min_torque_valid_ = false;
    float revolution_max_torque_nm_ = 0.0F;
    bool revolution_max_torque_valid_ = false;
    static constexpr uint8_t kSlidingWindowCapacity = 8;
    float sliding_window_[kSlidingWindowCapacity]{};
    // Peak-to-trough range per revolution, indexed in lockstep with
    // sliding_window_. A real hard effort raises this range (the rider never
    // fully unloads through the dead spot); genuine sensor drift shifts the
    // whole waveform without widening it. Used to tell the two apart so
    // interval-shaped rides (short high/low blocks) don't get read as drift.
    float range_window_[kSlidingWindowCapacity]{};
    uint8_t sliding_window_count_ = 0;
    uint8_t sliding_window_index_ = 0;
    uint16_t sliding_baseline_revolution_count_ = 0;
    float sliding_zero_baseline_nm_ = 0.0F;
    float sliding_zero_baseline_range_nm_ = 0.0F;
    bool sliding_zero_baseline_established_ = false;
    float sliding_zero_correction_nm_ = 0.0F;
    float slidingWindowMedian() const;
    float slidingRangeWindowMedian() const;

    // Ring buffer of per-revolution sliding-zero log entries.
    std::array<SlidingZeroLogEntry, kSlidingZeroLogCapacity> sliding_zero_log_{};
    uint32_t sliding_zero_log_count_ = 0;
    uint32_t sliding_zero_log_index_ = 0;
    int64_t sliding_zero_ride_started_us_ = 0;
};

}  // namespace openwatts
