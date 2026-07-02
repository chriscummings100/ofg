// Camera scene component implementation.
#include "ofg/scene/camera.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene_update.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace ofg {
namespace {

constexpr float _pi = 3.14159265358979323846f;
constexpr float _default_vertical_fov_radians = 55.0f * _pi / 180.0f;
constexpr float _max_pitch_radians = 89.0f * _pi / 180.0f;
constexpr float _look_sensitivity_radians_per_pixel = 0.0025f;
constexpr float _debug_move_units_per_second = 5.0f;
constexpr float _debug_fast_multiplier = 4.0f;
constexpr float _debug_slow_multiplier = 0.25f;
constexpr float _first_person_eye_offset_y = 0.7f;
constexpr float _first_person_eye_forward_offset = 0.28f;
constexpr float _third_person_target_offset_y = 0.55f;
constexpr float _third_person_distance = 4.0f;

// Validates perspective projection settings shared by construction and mutation paths.
void validate_perspective(float vertical_fov_radians, float near_z, float far_z) {
    if (!std::isfinite(vertical_fov_radians) || !std::isfinite(near_z) || !std::isfinite(far_z)) {
        throw EngineError("Camera perspective values must be finite.");
    }
    if (vertical_fov_radians <= 0.0f || vertical_fov_radians >= _pi) {
        throw EngineError("Camera vertical field of view must be greater than 0 and less than pi radians.");
    }
    if (near_z <= 0.0f || far_z <= near_z) {
        throw EngineError("Camera perspective requires 0 < near_z < far_z.");
    }
}

// Builds a local-to-parent matrix from translation and rotation only.
math::Mat4 parent_from_camera_local(const LocalTransform& transform) noexcept {
    return math::mul(math::mat4_translation(transform.m_position), math::mat4_from_quat(transform.m_rotation));
}

// Resolves the camera entity transform into world space while ignoring scale.
math::Mat4 world_from_camera_entity(const Entity& entity) noexcept {
    math::Mat4 world_from_camera = math::mat4_identity();
    for (const Entity* current = &entity; current != nullptr; current = current->parent()) {
        world_from_camera = math::mul(parent_from_camera_local(current->local_transform()), world_from_camera);
    }
    return world_from_camera;
}

// Returns a Vec3 from a matrix column used as a direction vector.
math::Vec3 direction_from_column(math::Vec4 column) noexcept {
    return math::vec3(column.x, column.y, column.z);
}

// Returns the current camera forward direction in world space.
math::Vec3 camera_forward(const Camera& camera) {
    if (camera.entity() == nullptr) {
        throw EngineError("Camera control requires an owning entity.");
    }
    const math::Mat4 world_from_camera = world_from_camera_entity(*camera.entity());
    const math::Vec4 forward = math::mul(world_from_camera, math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    return math::vec3(forward.x, forward.y, forward.z);
}

// Builds a normalized forward vector from controller yaw and pitch.
math::Vec3 forward_from_angles(float yaw_radians, float pitch_radians) noexcept {
    const float cos_pitch = std::cos(pitch_radians);
    return math::vec3(std::sin(yaw_radians) * cos_pitch, std::sin(pitch_radians), std::cos(yaw_radians) * cos_pitch);
}

// Builds the horizontal right vector for the current yaw.
math::Vec3 right_from_yaw(float yaw_radians) noexcept {
    return math::vec3(std::cos(yaw_radians), 0.0f, -std::sin(yaw_radians));
}

// Normalizes movement only when combined axes would otherwise move faster diagonally.
math::Vec3 normalize_movement(math::Vec3 movement) noexcept {
    const float length_squared = math::length_squared(movement);
    if (length_squared <= 1.0f || length_squared <= 0.0f) {
        return movement;
    }
    return math::mul(movement, 1.0f / std::sqrt(length_squared));
}

// Converts a finite camera forward vector into yaw/pitch controller state.
void derive_angles(math::Vec3 forward, float& yaw_radians, float& pitch_radians) {
    std::string error;
    const std::optional<math::Vec3> normalized_forward = math::normalize(forward, error);
    if (!normalized_forward.has_value()) {
        throw EngineError(error.empty() ? "Camera control could not derive orientation." : error);
    }

    yaw_radians = std::atan2(normalized_forward->x, normalized_forward->z);
    pitch_radians =
        std::clamp(std::asin(std::clamp(normalized_forward->y, -1.0f, 1.0f)), -_max_pitch_radians, _max_pitch_radians);
}

// Extracts the owning entity or reports a component binding error.
Entity& require_camera_entity(Camera& camera) {
    Entity* entity = camera.entity();
    if (entity == nullptr) {
        throw EngineError("Camera update requires an owning entity.");
    }
    return *entity;
}

// Applies the requested yaw/pitch orientation to a camera entity transform.
void apply_camera_orientation(Entity& entity, float yaw_radians, float pitch_radians) {
    const math::Vec3 position = entity.local_transform().m_position;
    const math::Vec3 forward = forward_from_angles(yaw_radians, pitch_radians);
    std::string error;
    std::optional<math::Quat> rotation =
        math::quat_look_at_lh(position, math::add(position, forward), math::vec3(0.0f, 1.0f, 0.0f), error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Camera control rotation creation failed." : error);
    }
    entity.local_transform().m_rotation = *rotation;
}

// Returns the next camera control mode in the user-facing cycle order.
CameraControlMode next_mode(CameraControlMode mode) noexcept {
    switch (mode) {
    case CameraControlMode::Debug:
        return CameraControlMode::FirstPerson;
    case CameraControlMode::FirstPerson:
        return CameraControlMode::ThirdPerson;
    case CameraControlMode::ThirdPerson:
        return CameraControlMode::Debug;
    }
    return CameraControlMode::Debug;
}

// Returns the speed multiplier implied by debug camera modifier state.
float debug_speed_multiplier(const ControlInput& input) noexcept {
    if (input.m_fast && !input.m_slow) {
        return _debug_fast_multiplier;
    }
    if (input.m_slow && !input.m_fast) {
        return _debug_slow_multiplier;
    }
    return 1.0f;
}

// Builds a yaw-only player rotation used by player camera modes.
math::Quat yaw_rotation(float yaw_radians) {
    std::string error;
    // Camera yaw uses +X as positive look direction from the shared local +Z forward axis.
    std::optional<math::Quat> rotation = math::quat_from_axis_angle(math::vec3(0.0f, 1.0f, 0.0f), yaw_radians, error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Camera control player yaw creation failed." : error);
    }
    return *rotation;
}

} // namespace

// Converts a camera control mode into its debug-status string value.
const char* camera_control_mode_name(CameraControlMode mode) noexcept {
    switch (mode) {
    case CameraControlMode::Debug:
        return "debug";
    case CameraControlMode::FirstPerson:
        return "first_person";
    case CameraControlMode::ThirdPerson:
        return "third_person";
    }
    return "debug";
}

// Binds this camera to one scene-owned entity.
Camera::Camera(Entity* entity) noexcept
    : Component(ComponentType::Camera, entity), m_vertical_fov_radians(_default_vertical_fov_radians) {}

// Applies debug fly-camera movement and look behavior to the camera entity.
void Camera::update_debug_control(const SceneUpdateContext& context) {
    Entity& entity = require_camera_entity(*this);
    if (context.m_controls.m_look_active) {
        m_yaw_radians += context.m_controls.m_look_delta_x * _look_sensitivity_radians_per_pixel;
        m_pitch_radians =
            std::clamp(m_pitch_radians - context.m_controls.m_look_delta_y * _look_sensitivity_radians_per_pixel,
                -_max_pitch_radians,
                _max_pitch_radians);
        apply_camera_orientation(entity, m_yaw_radians, m_pitch_radians);
    }

    math::Vec3 movement = math::vec3(0.0f, 0.0f, 0.0f);
    movement = math::add(movement, math::mul(right_from_yaw(m_yaw_radians), context.m_controls.m_move_x));
    movement = math::add(movement, math::mul(math::vec3(0.0f, 1.0f, 0.0f), context.m_controls.m_move_y));
    movement = math::add(
        movement, math::mul(forward_from_angles(m_yaw_radians, m_pitch_radians), context.m_controls.m_move_z));
    movement = normalize_movement(movement);

    const float distance =
        _debug_move_units_per_second * debug_speed_multiplier(context.m_controls) * context.m_delta_seconds;
    if (distance > 0.0f && math::length_squared(movement) > 0.0f) {
        entity.local_transform().m_position =
            math::add(entity.local_transform().m_position, math::mul(movement, distance));
    }
}

// Applies first-person camera placement to the camera entity and player facing.
void Camera::update_first_person_control(const SceneUpdateContext& context) {
    if (context.m_primary_player == nullptr || context.m_primary_player->entity() == nullptr) {
        return;
    }

    Entity& camera_entity = require_camera_entity(*this);
    Entity& player_entity = *context.m_primary_player->entity();
    player_entity.local_transform().m_rotation = yaw_rotation(m_yaw_radians);
    const math::Vec3 eye_height =
        math::add(player_entity.local_transform().m_position, math::vec3(0.0f, _first_person_eye_offset_y, 0.0f));
    camera_entity.local_transform().m_position =
        math::add(eye_height, math::mul(forward_from_angles(m_yaw_radians, 0.0f), _first_person_eye_forward_offset));
    apply_camera_orientation(camera_entity, m_yaw_radians, m_pitch_radians);
}

// Applies third-person camera placement to the camera entity and player facing.
void Camera::update_third_person_control(const SceneUpdateContext& context) {
    if (context.m_primary_player == nullptr || context.m_primary_player->entity() == nullptr) {
        return;
    }

    Entity& camera_entity = require_camera_entity(*this);
    Entity& player_entity = *context.m_primary_player->entity();
    player_entity.local_transform().m_rotation = yaw_rotation(m_yaw_radians);
    const math::Vec3 target =
        math::add(player_entity.local_transform().m_position, math::vec3(0.0f, _third_person_target_offset_y, 0.0f));
    const math::Vec3 forward = forward_from_angles(m_yaw_radians, m_pitch_radians);
    const math::Vec3 eye = math::add(target, math::mul(forward, -_third_person_distance));

    std::string error;
    std::optional<math::Quat> rotation = math::quat_look_at_lh(eye, target, math::vec3(0.0f, 1.0f, 0.0f), error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Third-person camera rotation creation failed." : error);
    }
    camera_entity.local_transform().m_position = eye;
    camera_entity.local_transform().m_rotation = *rotation;
}

// Returns the vertical perspective field of view in radians.
float Camera::vertical_fov_radians() const noexcept {
    return m_vertical_fov_radians;
}

// Returns the near clip distance.
float Camera::near_z() const noexcept {
    return m_near_z;
}

// Returns the far clip distance.
float Camera::far_z() const noexcept {
    return m_far_z;
}

// Replaces perspective projection settings after validating their range.
void Camera::set_perspective(float vertical_fov_radians, float near_z, float far_z) {
    validate_perspective(vertical_fov_radians, near_z, far_z);
    m_vertical_fov_radians = vertical_fov_radians;
    m_near_z = near_z;
    m_far_z = far_z;
}

// Returns the active camera control mode.
CameraControlMode Camera::control_mode() const noexcept {
    return m_control_mode;
}

// Replaces the active camera control mode and recaptures orientation on next update.
void Camera::set_control_mode(CameraControlMode mode) noexcept {
    m_control_mode = mode;
    m_has_control_orientation = false;
}

// Applies camera-relevant controls for one frame.
void Camera::update(const SceneUpdateContext& context) {
    if (context.m_main_camera != this) {
        return;
    }
    validate_control_input(context.m_controls);
    if (!std::isfinite(context.m_delta_seconds) || context.m_delta_seconds < 0.0f) {
        throw EngineError("Camera update requires a finite non-negative delta.");
    }
    if (context.m_controls.m_cycle_camera_mode) {
        set_control_mode(next_mode(m_control_mode));
    }
    if (!m_has_control_orientation) {
        derive_angles(camera_forward(*this), m_yaw_radians, m_pitch_radians);
        m_has_control_orientation = true;
    }
    if (m_control_mode == CameraControlMode::Debug) {
        update_debug_control(context);
        return;
    }
    if (context.m_controls.m_look_active) {
        m_yaw_radians += context.m_controls.m_look_delta_x * _look_sensitivity_radians_per_pixel;
        m_pitch_radians =
            std::clamp(m_pitch_radians - context.m_controls.m_look_delta_y * _look_sensitivity_radians_per_pixel,
                -_max_pitch_radians,
                _max_pitch_radians);
    }

    switch (m_control_mode) {
    case CameraControlMode::Debug:
        return;
    case CameraControlMode::FirstPerson:
        update_first_person_control(context);
        return;
    case CameraControlMode::ThirdPerson:
        update_third_person_control(context);
        return;
    }
}

// Resolves this camera and its owning entity into renderer-facing properties.
CameraProperties Camera::camera_properties(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0.0f) {
        throw EngineError("Camera properties require a positive finite aspect ratio.");
    }
    if (entity() == nullptr) {
        throw EngineError("Camera properties require an owning entity.");
    }

    const math::Mat4 world_from_camera = world_from_camera_entity(*entity());
    const math::Vec3 eye = direction_from_column(world_from_camera[3]);
    const math::Vec3 camera_up = direction_from_column(world_from_camera[1]);
    const math::Vec3 camera_forward = direction_from_column(world_from_camera[2]);
    const math::Vec3 target = math::add(eye, camera_forward);

    CameraProperties properties =
        camera_properties_from_look_at(this, eye, target, camera_up, m_vertical_fov_radians, aspect, m_near_z, m_far_z);
    properties.world_from_camera = world_from_camera;
    return properties;
}

} // namespace ofg
