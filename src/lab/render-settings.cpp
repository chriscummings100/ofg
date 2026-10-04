// Keep malformed UI drafts from reaching shader math; scene asset/type ownership is unchanged by editing.
#include "lab/render-settings.h"
#include <cmath>
#include <numbers>

namespace ofg {
namespace {
// Tests all components without assuming that a finite sum implies finite inputs.
bool finite(math::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
// Accepts finite zero values for controls that can disable a contribution.
bool nonnegative(float value)
{
    return std::isfinite(value) && value >= 0;
}
} // namespace

bool applyLightingEdit(Lighting& destination, const Lighting& candidate)
{
    if (candidate.outdoor && !validOutdoorLighting(*candidate.outdoor))
    {
        return false;
    }
    if (candidate.lightCount > candidate.lights.size() || candidate.debugView > 4 || !nonnegative(candidate.exposure) ||
        !nonnegative(candidate.environmentIntensity) || !std::isfinite(candidate.environmentRotation))
    {
        return false;
    }
    for (uint32_t i = 0; i < candidate.lightCount; ++i)
    {
        const auto& light = candidate.lights[i];
        if (!finite(light.position) || !finite(light.direction) || !finite(light.color) ||
            !nonnegative(light.color.x) || !nonnegative(light.color.y) || !nonnegative(light.color.z) ||
            !nonnegative(light.intensity) || !nonnegative(light.range) ||
            (light.type != LightType::Directional && light.type != LightType::Point && light.type != LightType::Spot))
        {
            return false;
        }
        if (!std::isfinite(math::lengthSquared(light.direction)) || math::lengthSquared(light.direction) < 1e-12f)
        {
            return false;
        }
        if (light.type == LightType::Spot &&
            (!nonnegative(light.innerCone) || !std::isfinite(light.outerCone) || light.outerCone <= light.innerCone ||
             light.outerCone > std::numbers::pi_v<float> / 2))
        {
            return false;
        }
    }
    destination = candidate;
    return true;
}
} // namespace ofg
