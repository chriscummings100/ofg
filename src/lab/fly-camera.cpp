// Deterministic, no-roll camera movement shared by the native and browser inspection hosts.
#include "lab/fly-camera.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>

namespace ofg {
void FlyCamera::reset(Entity& camera, bool closeup)
{
    m_yaw = 0;
    m_pitch = 0;
    camera.setLocalPosition(closeup ? math::Vec3{0, 0, -5} : math::Vec3{0, -1.65f, -15});
    camera.setLocalRotation(math::quatIdentity());
}

void FlyCamera::update(Entity& camera, const FlyCameraInput& input, float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0 || !std::isfinite(input.lookPixels.x) ||
        !std::isfinite(input.lookPixels.y) || !std::isfinite(math::lengthSquared(input.movement)))
    {
        throw EngineError("Fly camera requires finite input and a nonnegative frame delta.");
    }
    if (input.reset || input.closeup)
    {
        reset(camera, input.closeup);
        return;
    }
    m_yaw = std::remainder(m_yaw + input.lookPixels.x * 0.0025f, 6.283185307f);
    m_pitch = std::clamp(m_pitch - input.lookPixels.y * 0.0025f, -1.553343f, 1.553343f);
    const math::Vec3 forward{std::sin(m_yaw), 0, std::cos(m_yaw)};
    const math::Vec3 right{std::cos(m_yaw), 0, -std::sin(m_yaw)};
    auto movement = math::add(math::mul(right, input.movement.x), math::mul(forward, input.movement.z));
    movement.y = input.movement.y;
    const float length = math::length(movement);
    if (length > 1)
    {
        movement = math::mul(movement, 1 / length);
    }
    const auto position =
        math::add(camera.localTransform().position, math::mul(movement, deltaSeconds * (input.fast ? 12.f : 4.f)));
    const math::Vec3 direction{forward.x * std::cos(m_pitch), std::sin(m_pitch), forward.z * std::cos(m_pitch)};
    std::string error;
    auto rotation = math::quatLookAtLh(position, math::add(position, direction), {0, 1, 0}, error);
    if (!rotation)
    {
        throw EngineError("Fly camera: " + error);
    }
    camera.setLocalPosition(position);
    camera.setLocalRotation(*rotation);
}
} // namespace ofg
