// Passive perspective camera; its owning entity supplies the world-space pose.
#pragma once

#include "scene/component.h"
#include "math/mat.h"

namespace ofg {
class Scene;
class Camera : public Component
{
public:
    // Releases observation of the owning entity.
    ~Camera() override = default;
    // Validates vertical FOV in radians and positive near/far distances with far > near.
    void setPerspective(float verticalFovRadians, float nearDistance, float farDistance);
    // Builds the [0,1] depth projection for a positive finite width/height ratio.
    math::Mat4 projectionMatrix(float aspectRatio) const;

private:
    friend class Scene;
    // Creates a 60-degree camera with near/far distances 0.1 and 1000 scene units.
    explicit Camera(Entity* entity) noexcept;
    float m_verticalFov{1.0471975512f};
    float m_nearDistance{0.1f};
    float m_farDistance{1000.0f};
};
} // namespace ofg
