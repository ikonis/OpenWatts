#include "cadence_estimator.h"

#include <cmath>

namespace openwatts {

void CadenceEstimator::updateConfig(const DeviceConfig &config) {
    config_ = config;
}

CadenceState CadenceEstimator::update(const ImuSample &sample, int64_t now_us) {
    if (!sample.valid) {
        latest_.moving = false;
        latest_.rpm = 0.0F;
        return latest_;
    }

    // Bring-up stub: count a revolution on a positive gyro-Z threshold crossing.
    // TODO: replace with axis selection, filtering, orientation handling, and validation against a crank fixture.
    const float gyro_z = sample.gyro_dps[2];
    if (armed_ && gyro_z > config_.imu_revolution_threshold_dps) {
        previous_revolution_us_ = latest_.last_revolution_us;
        latest_.last_revolution_us = now_us;
        latest_.revolutions += 1;
        armed_ = false;

        if (previous_revolution_us_ > 0 && latest_.last_revolution_us > previous_revolution_us_) {
            const int64_t period_us = latest_.last_revolution_us - previous_revolution_us_;
            const float candidate_rpm = 60000000.0F / static_cast<float>(period_us);
            latest_.rpm = std::isfinite(candidate_rpm) &&
                                  candidate_rpm >= config_.minimum_cadence_rpm &&
                                  candidate_rpm <= config_.maximum_cadence_rpm
                              ? candidate_rpm : 0.0F;
        }
    } else if (gyro_z < (config_.imu_revolution_threshold_dps * 0.25F)) {
        armed_ = true;
    }

    if (latest_.last_revolution_us == 0 ||
        (now_us - latest_.last_revolution_us) > static_cast<int64_t>(config_.cadence_timeout_seconds) * 1000000LL) {
        latest_.rpm = 0.0F;
    }
    latest_.moving = latest_.rpm > 0.1F;
    return latest_;
}

CadenceState CadenceEstimator::latest() const {
    return latest_;
}

void CadenceEstimator::reset() {
    latest_ = {};
    armed_ = true;
    previous_revolution_us_ = 0;
}

}  // namespace openwatts
