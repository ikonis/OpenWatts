#include "rotation.h"

#include <cmath>

namespace openwatts {

float normalizeAngleDegrees(float degrees) {
    if (!std::isfinite(degrees)) return 0.0F;
    float normalized = std::fmod(degrees, 360.0F);
    if (normalized < 0.0F) normalized += 360.0F;
    return normalized;
}

}  // namespace openwatts
