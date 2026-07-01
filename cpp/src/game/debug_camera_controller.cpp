// Debug fly camera controller implementation.
#include "ofg/game/debug_camera_controller.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace ofg {
namespace {

constexpr float _pi = 3.14159265358979323846f;
constexpr float _max_pitch_radians = 89.0f * _pi / 180.0f;
constexpr float _look_sensitivity_radians_per_pixel = 0.0025f;
constexpr float _base_move_units_per_second = 5.0f;
constexpr float _fast_multiplier = 4.0f;
constexpr float _slow_multiplier = 0.25f;
constexpr float _max_delta_seconds = 0.1f;

// Returns the current camera forward direction in world space.
math::Vec3 camera_forward(const Camera& camera) {
    const CameraProperties properties = camera.camera_properties(1.0f);
    const math::Vec4 forward = math::mul(properties.world_from_camera, math::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return math::vec3(forward.x, forward.y, forward.z);
}

// Builds a normalized forward vector from controller yaw and pitch.
math::Vec3 forward_from_angles(float yaw_radians, float pitch_radians) noexcept {
    const float cos_pitch = std::cos(pitch_radians);
    return math::vec3(std::sin(yaw_radians) * cos_pitch, std::sin(pitch_radians), -std::cos(yaw_radians) * cos_pitch);
}

// Builds the horizontal right vector for the current yaw.
math::Vec3 right_from_yaw(float yaw_radians) noexcept {
    return math::vec3(std::cos(yaw_radians), 0.0f, std::sin(yaw_radians));
}

// Returns a clamped non-negative frame delta in seconds.
float frame_delta_seconds(double current_time_ms, double last_time_ms, bool has_last_time) noexcept {
    if (!has_last_time) {
        return 0.0f;
    }
    const double delta_ms = current_time_ms - last_time_ms;
    if (!std::isfinite(delta_ms) || delta_ms <= 0.0) {
        return 0.0f;
    }
    return std::min(static_cast<float>(delta_ms * 0.001), _max_delta_seconds);
}

// Returns the speed multiplier implied by fast/slow modifier state.
float speed_multiplier(DebugCameraInput input) noexcept {
    if (input.fast && !input.slow) {
        return _fast_multiplier;
    }
    if (input.slow && !input.fast) {
        return _slow_multiplier;
    }
    return 1.0f;
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
        throw EngineError(error.empty() ? "Debug camera could not derive orientation." : error);
    }

    yaw_radians = std::atan2(normalized_forward->x, -normalized_forward->z);
    pitch_radians =
        std::clamp(std::asin(std::clamp(normalized_forward->y, -1.0f, 1.0f)), -_max_pitch_radians, _max_pitch_radians);
}

// Writes the current yaw/pitch back to the camera entity transform.
void apply_orientation(Camera& camera, float yaw_radians, float pitch_radians) {
    Entity* entity = camera.entity();
    if (entity == nullptr) {
        throw EngineError("Debug camera requires an owning entity.");
    }

    const math::Vec3 position = entity->local_transform().m_position;
    const math::Vec3 forward = forward_from_angles(yaw_radians, pitch_radians);
    std::string error;
    std::optional<math::Quat> rotation =
        math::quat_look_at_rh(position, math::add(position, forward), math::vec3(0.0f, 1.0f, 0.0f), error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Debug camera rotation creation failed." : error);
    }
    entity->local_transform().m_rotation = *rotation;
}

} // namespace

// Validates that the numeric input fields are finite before storage or use.
void validate_debug_camera_input(DebugCameraInput input) {
    if (!std::isfinite(input.move_x) || !std::isfinite(input.move_y) || !std::isfinite(input.move_z) ||
        !std::isfinite(input.look_delta_x) || !std::isfinite(input.look_delta_y)) {
        throw EngineError("Debug camera input values must be finite.");
    }
}

// Restores initial timing/orientation capture state.
void DebugCameraController::reset() noexcept {
    m_tracked_camera = nullptr;
    m_last_time_ms = 0.0;
    m_yaw_radians = 0.0f;
    m_pitch_radians = 0.0f;
    m_has_last_time = false;
    m_has_orientation = false;
}

// Applies one input snapshot to the scene's active camera.
void DebugCameraController::update(Scene& scene, DebugCameraInput input, double time_ms) {
    validate_debug_camera_input(input);
    if (!std::isfinite(time_ms)) {
        throw EngineError("Debug camera update time must be finite.");
    }

    Camera* camera = scene.main_camera();
    if (camera == nullptr) {
        reset();
        return;
    }
    if (camera != m_tracked_camera || !m_has_orientation) {
        derive_angles(camera_forward(*camera), m_yaw_radians, m_pitch_radians);
        m_tracked_camera = camera;
        m_has_orientation = true;
    }

    const float delta_seconds = frame_delta_seconds(time_ms, m_last_time_ms, m_has_last_time);
    m_last_time_ms = time_ms;
    m_has_last_time = true;

    if (input.look_active) {
        m_yaw_radians += input.look_delta_x * _look_sensitivity_radians_per_pixel;
        m_pitch_radians = std::clamp(m_pitch_radians - input.look_delta_y * _look_sensitivity_radians_per_pixel,
            -_max_pitch_radians,
            _max_pitch_radians);
        apply_orientation(*camera, m_yaw_radians, m_pitch_radians);
    }

    math::Vec3 movement = math::vec3(0.0f, 0.0f, 0.0f);
    movement = math::add(movement, math::mul(right_from_yaw(m_yaw_radians), input.move_x));
    movement = math::add(movement, math::mul(math::vec3(0.0f, 1.0f, 0.0f), input.move_y));
    movement = math::add(movement, math::mul(forward_from_angles(m_yaw_radians, m_pitch_radians), input.move_z));
    movement = normalize_movement(movement);

    const float distance = _base_move_units_per_second * speed_multiplier(input) * delta_seconds;
    if (distance > 0.0f && math::length_squared(movement) > 0.0f) {
        Entity* entity = camera->entity();
        if (entity == nullptr) {
            throw EngineError("Debug camera requires an owning entity.");
        }
        entity->local_transform().m_position =
            math::add(entity->local_transform().m_position, math::mul(movement, distance));
    }
}

} // namespace ofg
