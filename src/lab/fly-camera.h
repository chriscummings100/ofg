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
    // Selects fixture-specific reset and close-up positions without changing movement behavior.
    void setResetPositions(math::Vec3 overview, math::Vec3 closeup)
    {
        m_overview = overview;
        m_closeup = closeup;
    }
    // Restores the reproducible grid overview (or close-up) on an unparented camera entity.
    void reset(Entity& camera, bool closeup = false);
    // Applies mouse displacement and normalized horizontal/world-up motion; delta is finite seconds >= 0.
    void update(Entity& camera, const FlyCameraInput& input, float deltaSeconds);

private:
    math::Vec3 m_overview{0, -1.65f, -15};
    math::Vec3 m_closeup{0, 0, -5};
};
} // namespace ofg
