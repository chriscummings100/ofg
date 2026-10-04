// GPU-independent inspection camera: hosts supply input, while shared code owns pose and movement conventions.
#pragma once

#include "scene/entity.h"

namespace ofg {
struct FlyCameraInput
{
    math::Vec3 movement{}; // Right, world up, forward; each axis in [-1,1].
    math::Vec2 lookPixels{};
    bool fast{false};
    bool reset{false};
    bool closeup{false};
};

class FlyCamera
{
public:
    // Restores the reproducible grid overview (or close-up) on an unparented camera entity.
    void reset(Entity& camera, bool closeup = false);
    // Applies mouse displacement and normalized horizontal/world-up motion; delta is finite seconds >= 0.
    void update(Entity& camera, const FlyCameraInput& input, float deltaSeconds);

private:
    float m_yaw{0};
    float m_pitch{0};
};
} // namespace ofg
