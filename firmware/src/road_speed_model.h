#pragma once

#include <cstdint>

namespace openwatts {

struct RoadModel {
    static constexpr uint16_t kVersion = 1;
    static constexpr float kBicycleMassKg = 10.0F;
    static constexpr float kCdA = 0.40F;
    static constexpr float kRollingResistance = 0.005F;
    static constexpr float kDrivetrainEfficiency = 0.97F;
    static constexpr float kAirDensityKgM3 = 1.225F;
    static constexpr float kGravityMps2 = 9.80665F;

    static float speedMetersPerSecond(float crank_power_watts, float rider_mass_kg);
};

}  // namespace openwatts
