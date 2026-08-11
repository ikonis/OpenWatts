#pragma once

#include <cstdint>

#include "config.h"
#include "power_estimator.h"

namespace openwatts {

enum class RideZeroTrigger : uint8_t { UsbRemoved, UsbInserted, BleConnected, BleDisconnected, BeforeSleep, Stationary };
enum class RideZeroResult : uint8_t {
    Accepted, Disabled, Locked, CalibrationRequired, SensorUnavailable,
    CadenceActive, BleRideActive, ImuUnstable, InsufficientSamples, UnstableStrain,
    LoadedReference, InvalidData,
};

struct RideZeroAttempt {
    RideZeroTrigger trigger = RideZeroTrigger::Stationary;
    RideZeroResult result = RideZeroResult::Disabled;
    int32_t zero_offset = 0;
    float average = 0.0F;
    float variance = 0.0F;
    float standard_deviation = 0.0F;
    float range = 0.0F;
    uint32_t samples = 0;
};

class RideZeroController {
public:
    static constexpr uint32_t kWindowSize = 64;
    static constexpr uint32_t kMinimumSamples = 32;
    void observe(float filtered_counts, bool fresh, int64_t now_us);
    RideZeroAttempt attempt(RideZeroTrigger trigger, const DeviceConfig &config,
                            const PowerSample &sample, bool ble_ride_active, bool imu_stationary,
                            bool calibration_active, int64_t now_us);
    void lock();
    void resetLifecycle();
    bool locked() const;
    const RideZeroAttempt &lastAttempt() const;
    static const char *triggerName(RideZeroTrigger trigger);
    static const char *resultName(RideZeroResult result);

private:
    float values_[kWindowSize]{};
    uint32_t count_ = 0;
    uint32_t index_ = 0;
    int64_t last_sample_us_ = 0;
    bool locked_ = false;
    RideZeroAttempt last_{};
};

}  // namespace openwatts
