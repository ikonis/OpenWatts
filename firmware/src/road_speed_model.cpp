#include "road_speed_model.h"

#include <cmath>

namespace openwatts {

float RoadModel::speedMetersPerSecond(float crank_power_watts, float rider_mass_kg) {
    if (!std::isfinite(crank_power_watts) || !std::isfinite(rider_mass_kg) ||
        crank_power_watts <= 0.0F || rider_mass_kg <= 0.0F) return 0.0F;
    const double wheel_power = static_cast<double>(crank_power_watts) * kDrivetrainEfficiency;
    const double rolling = kRollingResistance * (static_cast<double>(rider_mass_kg) + kBicycleMassKg) * kGravityMps2;
    const double aerodynamic = 0.5 * kAirDensityKgM3 * kCdA;
    double low = 0.0;
    double high = 40.0;
    for (int i = 0; i < 40; ++i) {
        const double speed = (low + high) * 0.5;
        const double required = rolling * speed + aerodynamic * speed * speed * speed;
        if (required < wheel_power) low = speed; else high = speed;
    }
    const double result = (low + high) * 0.5;
    return std::isfinite(result) ? static_cast<float>(result) : 0.0F;
}

}  // namespace openwatts
