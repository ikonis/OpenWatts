#pragma once

#include <cstdint>

#include "power_estimator.h"
#include "road_speed_model.h"

namespace openwatts {

struct LastRideSummary {
    static constexpr uint32_t kSchemaVersion = 3;
    uint32_t schema_version = kSchemaVersion;
    uint32_t sequence = 0;
    uint32_t moving_seconds = 0;
    uint32_t elapsed_seconds = 0;
    uint32_t crank_revolutions = 0;
    float average_power_watts = 0.0F;
    int16_t maximum_power_watts = 0;
    float average_cadence_rpm = 0.0F;
    float maximum_cadence_rpm = 0.0F;
    float work_kj = 0.0F;
    bool valid = false;
    char end_reason[24] = "none";
    float estimated_distance_meters = 0.0F;
    float average_estimated_speed_mps = 0.0F;
    float maximum_estimated_speed_mps = 0.0F;
    uint16_t road_model_version = 0;
    float rider_mass_kg = 0.0F;
    bool mqtt_publish_pending = false;
};

class RideLog {
public:
    void begin(const LastRideSummary &stored);
    bool update(const PowerSample &sample, int64_t now_us, uint32_t qualification_seconds,
                float rider_mass_kg);
    bool finishForUsbConnection(int64_t now_us);
    void clear();
    const LastRideSummary &lastRide() const;
    bool candidate() const;
    bool active() const;
    uint32_t currentMovingSeconds() const;
    float currentDistanceMeters() const;
    bool completedPendingSave() const;
    void markSaved();
    void markMqttPublished();
    void markMqttPending();

private:
    void resetCurrent();
    void finish(int64_t now_us, const char *reason);

    LastRideSummary last_{};
    bool candidate_ = false;
    bool qualified_ = false;
    bool pending_save_ = false;
    int64_t cadence_started_us_ = 0;
    int64_t ride_started_us_ = 0;
    int64_t last_update_us_ = 0;
    int64_t stationary_started_us_ = 0;
    double moving_seconds_ = 0.0;
    double power_watt_seconds_ = 0.0;
    double cadence_rpm_seconds_ = 0.0;
    float max_power_ = 0.0F;
    float max_cadence_ = 0.0F;
    double estimated_distance_meters_ = 0.0;
    float maximum_estimated_speed_mps_ = 0.0F;
    float rider_mass_kg_ = 0.0F;
};

}  // namespace openwatts
