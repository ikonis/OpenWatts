#include "ride_log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwatts {
namespace {
constexpr float kCadenceActiveRpm = 5.0F;
constexpr int64_t kCandidateCadenceUs = 10LL * 1000000LL;
constexpr int64_t kRideEndStationaryUs = 300LL * 1000000LL;
}

void RideLog::begin(const LastRideSummary &stored) {
    last_ = stored.schema_version == LastRideSummary::kSchemaVersion && stored.valid
                ? stored : LastRideSummary{};
    resetCurrent();
}

bool RideLog::update(const PowerSample &sample, int64_t now_us, uint32_t qualification_seconds,
                     float rider_mass_kg) {
    const bool cadence_active = std::isfinite(sample.cadence_rpm) &&
                                sample.cadence_rpm >= kCadenceActiveRpm;
    if (last_update_us_ == 0) last_update_us_ = now_us;
    const int64_t elapsed_us = now_us - last_update_us_;
    const double dt = std::clamp(static_cast<double>(elapsed_us) / 1000000.0, 0.0, 1.0);
    const bool distance_gap_valid = elapsed_us > 0 && elapsed_us <= 250000;
    last_update_us_ = now_us;

    if (!candidate_) {
        if (!cadence_active) { cadence_started_us_ = 0; return false; }
        if (cadence_started_us_ == 0) cadence_started_us_ = now_us;
        if (now_us - cadence_started_us_ < kCandidateCadenceUs) return false;
        candidate_ = true;
        ride_started_us_ = cadence_started_us_;
    }

    if (cadence_active) {
        stationary_started_us_ = 0;
        moving_seconds_ += dt;
        const float safe_power = sample.valid ? std::max<float>(0.0F, sample.power_watts) : 0.0F;
        power_watt_seconds_ += static_cast<double>(safe_power) * dt;
        cadence_rpm_seconds_ += static_cast<double>(sample.cadence_rpm) * dt;
        max_power_ = std::max(max_power_, safe_power);
        max_cadence_ = std::max(max_cadence_, sample.cadence_rpm);
        if (distance_gap_valid && sample.valid && sample.power_watts >= 0 &&
            std::isfinite(sample.cadence_rpm) && sample.cadence_rpm > 0.0F &&
            std::isfinite(rider_mass_kg)) {
            const float speed = RoadModel::speedMetersPerSecond(sample.power_watts, rider_mass_kg);
            if (std::isfinite(speed) && speed >= 0.0F) {
                estimated_distance_meters_ += static_cast<double>(speed) * dt;
                maximum_estimated_speed_mps_ = std::max(maximum_estimated_speed_mps_, speed);
                rider_mass_kg_ = rider_mass_kg;
            }
        }
        if (!qualified_ && moving_seconds_ >= qualification_seconds) qualified_ = true;
    } else if (qualified_) {
        if (stationary_started_us_ == 0) stationary_started_us_ = now_us;
        if (now_us - stationary_started_us_ >= kRideEndStationaryUs) {
            finish(now_us, "cadence_stopped");
            return true;
        }
    } else {
        resetCurrent();
    }
    return false;
}

void RideLog::finish(int64_t now_us, const char *reason) {
    LastRideSummary completed{};
    completed.sequence = last_.sequence + 1U;
    completed.moving_seconds = static_cast<uint32_t>(std::lround(moving_seconds_));
    completed.elapsed_seconds = static_cast<uint32_t>(std::max<int64_t>(0, now_us - ride_started_us_) / 1000000LL);
    completed.crank_revolutions = completed.moving_seconds > 0
        ? static_cast<uint32_t>(std::lround(cadence_rpm_seconds_ / 60.0)) : 0;
    completed.average_power_watts = moving_seconds_ > 0.0
        ? static_cast<float>(power_watt_seconds_ / moving_seconds_) : 0.0F;
    completed.maximum_power_watts = static_cast<int16_t>(
        std::clamp<int>(static_cast<int>(std::lround(max_power_)), 0, 32767));
    completed.average_cadence_rpm = moving_seconds_ > 0.0
        ? static_cast<float>(cadence_rpm_seconds_ / moving_seconds_) : 0.0F;
    completed.maximum_cadence_rpm = max_cadence_;
    completed.work_kj = static_cast<float>(power_watt_seconds_ / 1000.0);
    completed.estimated_distance_meters = static_cast<float>(estimated_distance_meters_);
    completed.average_estimated_speed_mps = moving_seconds_ > 0.0
        ? static_cast<float>(estimated_distance_meters_ / moving_seconds_) : 0.0F;
    completed.maximum_estimated_speed_mps = maximum_estimated_speed_mps_;
    completed.road_model_version = RoadModel::kVersion;
    completed.rider_mass_kg = rider_mass_kg_;
    completed.valid = true;
    std::strncpy(completed.end_reason, reason, sizeof(completed.end_reason) - 1);
    last_ = completed;
    resetCurrent();
    pending_save_ = true;
}

void RideLog::resetCurrent() {
    candidate_ = false; qualified_ = false; cadence_started_us_ = 0; ride_started_us_ = 0;
    last_update_us_ = 0; stationary_started_us_ = 0; moving_seconds_ = 0.0;
    power_watt_seconds_ = 0.0; cadence_rpm_seconds_ = 0.0; max_power_ = 0.0F; max_cadence_ = 0.0F;
    estimated_distance_meters_ = 0.0; maximum_estimated_speed_mps_ = 0.0F; rider_mass_kg_ = 0.0F;
}
void RideLog::clear() { last_ = LastRideSummary{}; resetCurrent(); pending_save_ = true; }
const LastRideSummary &RideLog::lastRide() const { return last_; }
bool RideLog::candidate() const { return candidate_; }
bool RideLog::active() const { return qualified_; }
bool RideLog::completedPendingSave() const { return pending_save_; }
void RideLog::markSaved() { pending_save_ = false; }

}  // namespace openwatts
