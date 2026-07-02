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
struct SceneUpdateContext;

enum class CameraControlMode {
    Debug,
    FirstPerson,
    ThirdPerson,
};

// Converts a camera control mode into its debug-status string value.
[[nodiscard]] const char* camera_control_mode_name(CameraControlMode mode) noexcept;

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
    // Returns the active camera control mode.
    [[nodiscard]] CameraControlMode control_mode() const noexcept;
    // Replaces the active camera control mode and recaptures orientation on next update.
    void set_control_mode(CameraControlMode mode) noexcept;
    // Applies camera-relevant controls for one frame.
    void update(const SceneUpdateContext& context);
    // Resolves this camera and its owning entity into renderer-facing properties.
    [[nodiscard]] CameraProperties camera_properties(float aspect) const;

private:
    // Applies debug fly-camera controls.
    void update_debug_control(const SceneUpdateContext& context);
    // Applies first-person placement around the primary player.
    void update_first_person_control(const SceneUpdateContext& context);
    // Applies third-person placement around the primary player.
    void update_third_person_control(const SceneUpdateContext& context);

    float m_vertical_fov_radians;
    float m_near_z{0.1f};
    float m_far_z{80.0f};
    CameraControlMode m_control_mode{CameraControlMode::Debug};
    float m_yaw_radians{0.0f};
    float m_pitch_radians{0.0f};
    bool m_has_control_orientation{false};
};

} // namespace ofg
