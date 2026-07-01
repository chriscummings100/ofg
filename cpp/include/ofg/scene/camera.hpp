// Camera scene component.
//
// Camera stores projection settings for an entity-owned viewpoint. The owning
// entity's local transform supplies camera position and rotation; camera scale
// is intentionally ignored when resolving renderer-facing CameraProperties.
#pragma once

#include "ofg/render/camera_properties.hpp"
#include "ofg/scene/component.hpp"

namespace ofg {

class Entity;

class Camera : public Component {
public:
    // Binds this camera to one scene-owned entity.
    explicit Camera(Entity* entity) noexcept;

    // Returns the vertical perspective field of view in radians.
    [[nodiscard]] float vertical_fov_radians() const noexcept;
    // Returns the near clip distance.
    [[nodiscard]] float near_z() const noexcept;
    // Returns the far clip distance.
    [[nodiscard]] float far_z() const noexcept;
    // Replaces perspective projection settings after validating their range.
    void set_perspective(float vertical_fov_radians, float near_z, float far_z);
    // Resolves this camera and its owning entity into renderer-facing properties.
    [[nodiscard]] CameraProperties camera_properties(float aspect) const;

private:
    float m_vertical_fov_radians;
    float m_near_z{0.1f};
    float m_far_z{80.0f};
};

} // namespace ofg
