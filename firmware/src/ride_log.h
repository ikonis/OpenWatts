#pragma once

#include <cstdint>

#include "power_estimator.h"

namespace openwatts {

struct LastRideSummary {
    static constexpr uint32_t kSchemaVersion = 1;
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
};

class RideLog {
public:
    void begin(const LastRideSummary &stored);
    bool update(const PowerSample &sample, int64_t now_us, uint32_t qualification_seconds);
    void clear();
    const LastRideSummary &lastRide() const;
    bool candidate() const;
    bool active() const;
    bool completedPendingSave() const;
    void markSaved();

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
};

}  // namespace openwatts
