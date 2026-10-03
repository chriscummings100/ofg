// Camera projection validation using shared left-handed perspective math.
#include "scene/camera.h"
#include "core/engine-error.h"
#include "math/transform.h"

namespace ofg {
Camera::Camera(Entity* entity) noexcept
    : Component(entity)
{
}

void Camera::setPerspective(float verticalFovRadians, float nearDistance, float farDistance)
{
    std::string error;
    if (!math::perspectiveLh(verticalFovRadians, 1.0f, nearDistance, farDistance, error))
    {
        throw EngineError("Camera: " + error);
    }
    m_verticalFov = verticalFovRadians;
    m_nearDistance = nearDistance;
    m_farDistance = farDistance;
}

math::Mat4 Camera::projectionMatrix(float aspectRatio) const
{
    std::string error;
    auto projection = math::perspectiveLh(m_verticalFov, aspectRatio, m_nearDistance, m_farDistance, error);
    if (!projection)
    {
        throw EngineError("Camera: " + error);
    }
    return *projection;
}
} // namespace ofg
