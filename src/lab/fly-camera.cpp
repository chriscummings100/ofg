// Deterministic, no-roll camera movement shared by the native and browser inspection hosts.
#include "lab/fly-camera.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>

namespace ofg {
void FlyCamera::reset(Entity& camera, bool closeup)
{
    camera.setLocalPosition(closeup ? m_closeup : m_overview);
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
    // The entity is the authoritative pose, including changes made by Frame Island or other inspection tools.
    const auto current = math::mat4FromQuat(camera.localTransform().rotation)[2];
    const auto yaw = std::remainder(std::atan2(current.x, current.z) + input.lookPixels.x * 0.0025f, 6.283185307f);
    const auto pitch =
        std::clamp(std::asin(std::clamp(current.y, -1.f, 1.f)) - input.lookPixels.y * 0.0025f, -1.553343f, 1.553343f);
    const math::Vec3 forward{std::sin(yaw), 0, std::cos(yaw)};
    const math::Vec3 right{std::cos(yaw), 0, -std::sin(yaw)};
    auto movement = math::add(math::mul(right, input.movement.x), math::mul(forward, input.movement.z));
    movement.y = input.movement.y;
    const float length = math::length(movement);
    if (length > 1)
    {
        movement = math::mul(movement, 1 / length);
    }
    const auto position =
        math::add(camera.localTransform().position, math::mul(movement, deltaSeconds * (input.fast ? 12.f : 4.f)));
    const math::Vec3 direction{forward.x * std::cos(pitch), std::sin(pitch), forward.z * std::cos(pitch)};
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
