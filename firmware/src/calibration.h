#pragma once

#include <cstdint>

#include "config.h"
#include "esp_err.h"

namespace openwatts {

enum class CalibrationStep : uint8_t { Idle, CapturingZero, ReadyForLoad, CapturingLoad, Review, Saved, Verifying, Verified, Error };

struct SampleStatistics {
    uint32_t count = 0;
    double mean = 0;
    double m2 = 0;
    int32_t minimum = INT32_MAX;
    int32_t maximum = INT32_MIN;
    void clear();
    void add(int32_t value);
    double standardDeviation() const;
    int32_t peakToPeak() const;
};

struct CalibrationSnapshot {
    CalibrationStep step = CalibrationStep::Idle;
    SampleStatistics zero{}, loaded{}, verify{};
    double mass_kg = 0;
    double lever_arm_mm = 0;
    double reference_torque_nm = 0;
    double raw_delta = 0;
    double counts_per_nm = 0;
    double nm_per_count = 0;
    double verification_torque_nm = 0;
    double verification_error_percent = 0;
    bool reverse_direction = false;
    bool active = false;
    bool valid = false;
    const char *result = "idle";
    const char *error = "none";
};

class CalibrationManager {
public:
    static constexpr int64_t kCaptureDurationUs = 3000000;
    static constexpr uint32_t kMinimumSamples = 20;
    esp_err_t start(double mass_kg, double lever_arm_mm, bool reverse, int64_t now_us, bool permitted, bool hx_ready);
    esp_err_t captureLoaded(int64_t now_us, bool permitted, bool hx_ready);
    esp_err_t verify(int64_t now_us, bool permitted, bool hx_ready);
    void observe(bool attempted, bool success, int32_t raw, bool hx_ready, int64_t now_us, bool permitted);
    esp_err_t apply(DeviceConfig &config);
    esp_err_t manualTare(DeviceConfig &config, bool hx_ready, float filtered, float noise);
    esp_err_t reverseDirection(DeviceConfig &config);
    void resetCalibration(DeviceConfig &config);
    void discard();
    CalibrationSnapshot snapshot() const { return state_; }
    static const char *stepName(CalibrationStep step);

private:
    bool beginCapture(CalibrationStep step, int64_t now_us, bool permitted, bool hx_ready);
    void finishCapture();
    bool stable(const SampleStatistics &stats) const;
    bool validateResult();
    CalibrationSnapshot state_{};
    int64_t capture_started_us_ = 0;
    uint32_t failures_ = 0;
};

}  // namespace openwatts
