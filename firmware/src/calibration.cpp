#include "calibration.h"

#include <algorithm>
#include <cmath>

namespace openwatts {
namespace {
constexpr double kGravity = 9.80665;
constexpr double kMaxStdDev = 6000.0;
constexpr int32_t kMaxPeakToPeak = 25000;
constexpr double kMinSignalToNoise = 10.0;
}

void SampleStatistics::clear() { *this = {}; }
void SampleStatistics::add(int32_t value) {
    ++count;
    const double delta = value - mean;
    mean += delta / count;
    m2 += delta * (value - mean);
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
}
double SampleStatistics::standardDeviation() const { return count > 1 ? std::sqrt(m2 / (count - 1)) : 0; }
int32_t SampleStatistics::peakToPeak() const { return count ? maximum - minimum : 0; }

esp_err_t CalibrationManager::start(double mass, double lever_mm, bool reverse, int64_t now, bool permitted, bool hx) {
    if (!std::isfinite(mass) || !std::isfinite(lever_mm) || mass < .01 || mass > 200 || lever_mm < 10 || lever_mm > 1000) return ESP_ERR_INVALID_ARG;
    state_ = {};
    state_.mass_kg = mass; state_.lever_arm_mm = lever_mm; state_.reverse_direction = reverse;
    state_.reference_torque_nm = mass * kGravity * lever_mm / 1000.0;
    if (!beginCapture(CalibrationStep::CapturingZero, now, permitted, hx)) return ESP_ERR_INVALID_STATE;
    state_.result = "capturing_unloaded_zero";
    return ESP_OK;
}
esp_err_t CalibrationManager::captureLoaded(int64_t now, bool permitted, bool hx) {
    if (state_.step != CalibrationStep::ReadyForLoad) return ESP_ERR_INVALID_STATE;
    state_.loaded.clear();
    if (!beginCapture(CalibrationStep::CapturingLoad, now, permitted, hx)) return ESP_ERR_INVALID_STATE;
    state_.result = "capturing_loaded_value"; return ESP_OK;
}
esp_err_t CalibrationManager::verify(int64_t now, bool permitted, bool hx) {
    if (state_.step != CalibrationStep::Review && state_.step != CalibrationStep::Saved &&
        state_.step != CalibrationStep::Verified) return ESP_ERR_INVALID_STATE;
    state_.verify.clear();
    if (!beginCapture(CalibrationStep::Verifying, now, permitted, hx)) return ESP_ERR_INVALID_STATE;
    state_.result = "verifying_known_load"; return ESP_OK;
}
bool CalibrationManager::beginCapture(CalibrationStep step, int64_t now, bool permitted, bool hx) {
    if (!permitted || !hx) { state_.step = CalibrationStep::Error; state_.error = !permitted ? "maintenance_required" : "signal_unavailable"; return false; }
    state_.step = step; state_.active = true; state_.error = "none"; capture_started_us_ = now; failures_ = 0; return true;
}
void CalibrationManager::observe(bool attempted, bool success, int32_t raw, bool hx, int64_t now, bool permitted) {
    if (!state_.active) return;
    if (!permitted || !hx) { state_.step = CalibrationStep::Error; state_.active = false; state_.error = !permitted ? "maintenance_ended" : "signal_unavailable"; return; }
    if (!attempted) return;
    if (!success) { if (++failures_ > 5) { state_.step = CalibrationStep::Error; state_.active = false; state_.error = "excessive_read_failures"; } return; }
    SampleStatistics *stats = state_.step == CalibrationStep::CapturingZero ? &state_.zero : state_.step == CalibrationStep::CapturingLoad ? &state_.loaded : state_.step == CalibrationStep::Verifying ? &state_.verify : nullptr;
    if (!stats) return;
    stats->add(raw);
    if (now - capture_started_us_ >= kCaptureDurationUs) finishCapture();
}
bool CalibrationManager::stable(const SampleStatistics &s) const { return s.count >= kMinimumSamples && s.standardDeviation() <= kMaxStdDev && s.peakToPeak() <= kMaxPeakToPeak; }
void CalibrationManager::finishCapture() {
    SampleStatistics *s = state_.step == CalibrationStep::CapturingZero ? &state_.zero : state_.step == CalibrationStep::CapturingLoad ? &state_.loaded : &state_.verify;
    if (!stable(*s)) { state_.step = CalibrationStep::Error; state_.active = false; state_.error = "signal_unstable"; return; }
    if (state_.step == CalibrationStep::CapturingZero) { state_.step = CalibrationStep::ReadyForLoad; state_.result = "unloaded_zero_captured"; return; }
    if (state_.step == CalibrationStep::CapturingLoad) { if (validateResult()) { state_.step = CalibrationStep::Review; state_.valid = true; state_.result = "ready_to_save"; } return; }
    state_.verification_torque_nm = (state_.verify.mean - state_.zero.mean) * state_.nm_per_count;
    state_.verification_error_percent = state_.reference_torque_nm > 0 ? 100.0 * (state_.verification_torque_nm - state_.reference_torque_nm) / state_.reference_torque_nm : 0;
    state_.step = CalibrationStep::Verified; state_.result = std::fabs(state_.verification_error_percent) <= 5 ? "verification_passed" : "verification_out_of_tolerance";
}
bool CalibrationManager::validateResult() {
    state_.raw_delta = state_.loaded.mean - state_.zero.mean;
    const double noise = std::max(1.0, state_.zero.standardDeviation() + state_.loaded.standardDeviation());
    if (!std::isfinite(state_.raw_delta) || std::fabs(state_.raw_delta) < std::max(1000.0, noise * kMinSignalToNoise)) { state_.step = CalibrationStep::Error; state_.active = false; state_.error = "loaded_delta_too_small"; return false; }
    state_.nm_per_count = state_.reference_torque_nm / state_.raw_delta;
    if (state_.reverse_direction) state_.nm_per_count = -state_.nm_per_count;
    if (!std::isfinite(state_.nm_per_count) || std::fabs(state_.nm_per_count) < 1e-9 || std::fabs(state_.nm_per_count) > 1.0) { state_.step = CalibrationStep::Error; state_.active = false; state_.error = "calculated_scale_implausible"; return false; }
    state_.counts_per_nm = 1.0 / std::fabs(state_.nm_per_count); return true;
}
esp_err_t CalibrationManager::apply(DeviceConfig &c) {
    if ((state_.step != CalibrationStep::Review && state_.step != CalibrationStep::Verified) || !state_.valid)
        return ESP_ERR_INVALID_STATE;
    c.calibration_zero_reference_counts = static_cast<int32_t>(std::lround(state_.zero.mean));
    c.runtime_zero_offset_counts = c.calibration_zero_reference_counts;
    c.zero_offset_counts = c.runtime_zero_offset_counts;
    c.counts_per_nm = static_cast<float>(state_.counts_per_nm);
    c.torque_sign = state_.nm_per_count < 0 ? -1 : 1;
    c.calibration_mass_kg = static_cast<float>(state_.mass_kg);
    c.calibration_lever_arm_mm = static_cast<float>(state_.lever_arm_mm);
    c.strain_calibration_valid = true; state_.step = CalibrationStep::Saved; state_.result = "saved"; return ESP_OK;
}
esp_err_t CalibrationManager::manualTare(DeviceConfig &c, bool hx, float filtered, float noise) {
    if (!c.strain_calibration_valid || !hx || !std::isfinite(filtered) || !std::isfinite(noise) || noise > kMaxStdDev) return ESP_ERR_INVALID_STATE;
    c.runtime_zero_offset_counts = static_cast<int32_t>(std::lround(filtered)); c.zero_offset_counts = c.runtime_zero_offset_counts; return ESP_OK;
}
esp_err_t CalibrationManager::reverseDirection(DeviceConfig &c) { if (!c.strain_calibration_valid) return ESP_ERR_INVALID_STATE; c.torque_sign = c.torque_sign < 0 ? 1 : -1; return ESP_OK; }
void CalibrationManager::resetCalibration(DeviceConfig &c) {
    c.strain_calibration_valid = false; c.zero_offset_counts = 0; c.calibration_zero_reference_counts = 0; c.runtime_zero_offset_counts = 0; c.counts_per_nm = 10000; c.torque_sign = 1; c.calibration_mass_kg = 0; c.calibration_lever_arm_mm = 0; discard();
}
void CalibrationManager::discard() { state_ = {}; }
const char *CalibrationManager::stepName(CalibrationStep s) { switch(s) { case CalibrationStep::Idle:return "prepare";case CalibrationStep::CapturingZero:return "zero_capturing";case CalibrationStep::ReadyForLoad:return "load_ready";case CalibrationStep::CapturingLoad:return "load_capturing";case CalibrationStep::Review:return "review";case CalibrationStep::Saved:return "verify_ready";case CalibrationStep::Verifying:return "verify_capturing";case CalibrationStep::Verified:return "verified";case CalibrationStep::Error:return "error";} return "unknown"; }

}  // namespace openwatts
