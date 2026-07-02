// Player scene component implementation.
#include "ofg/scene/player.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/scene_update.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace ofg {
namespace {

constexpr float _fast_multiplier = 2.0f;
constexpr float _slow_multiplier = 0.35f;

// Returns a flat normalized direction, or a zero vector when no movement exists.
math::Vec3 flat_normalized(math::Vec3 value) {
    value.y = 0.0f;
    const float length_squared = math::length_squared(value);
    if (length_squared <= 0.0f) {
        return math::vec3(0.0f, 0.0f, 0.0f);
    }
    if (length_squared <= 1.0f) {
        return value;
    }
    return math::mul(value, 1.0f / std::sqrt(length_squared));
}

// Returns a movement speed multiplier from the current control modifiers.
float speed_multiplier(const ControlInput& controls) noexcept {
    if (controls.m_fast && !controls.m_slow) {
        return _fast_multiplier;
    }
    if (controls.m_slow && !controls.m_fast) {
        return _slow_multiplier;
    }
    return 1.0f;
}

// Extracts the owning entity or reports a component binding error.
Entity& require_entity(Player& player) {
    Entity* entity = player.entity();
    if (entity == nullptr) {
        throw EngineError("Player update requires an owning entity.");
    }
    return *entity;
}

// Returns the entity's local right direction.
math::Vec3 entity_right(const Entity& entity) noexcept {
    const math::Mat4 rotation = math::mat4_from_quat(entity.local_transform().m_rotation);
    const math::Vec4 right = math::mul(rotation, math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    return math::vec3(right.x, 0.0f, right.z);
}

// Returns the entity's local forward direction.
math::Vec3 entity_forward(const Entity& entity) noexcept {
    const math::Mat4 rotation = math::mat4_from_quat(entity.local_transform().m_rotation);
    const math::Vec4 forward = math::mul(rotation, math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    return math::vec3(forward.x, 0.0f, forward.z);
}

} // namespace

// Binds this player to one scene-owned entity.
Player::Player(Entity* entity) noexcept : Component(ComponentType::Player, entity) {}

// Returns the walking speed in world units per second.
float Player::walk_speed() const noexcept {
    return m_walk_speed;
}

// Returns the fast movement speed in world units per second.
float Player::fast_speed() const noexcept {
    return m_walk_speed * _fast_multiplier;
}

// Replaces the walking speed after validating it is finite and non-negative.
void Player::set_walk_speed(float speed) {
    if (!std::isfinite(speed) || speed < 0.0f) {
        throw EngineError("Player walk speed must be a finite non-negative value.");
    }
    m_walk_speed = speed;
}

// Returns the height used to keep the centered player box grounded.
float Player::height() const noexcept {
    return m_height;
}

// Replaces the player height after validating it is finite and positive.
void Player::set_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        throw EngineError("Player height must be a positive finite value.");
    }
    m_height = height;
}

// Returns the latest intended flat movement speed in world units per second.
float Player::current_speed() const noexcept {
    return m_current_speed;
}

// Applies player-relevant controls for one frame.
void Player::update(const SceneUpdateContext& context) {
    m_current_speed = 0.0f;
    if (context.m_primary_player != this) {
        return;
    }
    if (!std::isfinite(context.m_delta_seconds) || context.m_delta_seconds < 0.0f) {
        throw EngineError("Player update requires a finite non-negative delta.");
    }

    Entity& owner = require_entity(*this);
    LocalTransform& transform = owner.local_transform();
    transform.m_position.y = m_height * 0.5f;
    if (context.m_main_camera == nullptr || context.m_main_camera->control_mode() == CameraControlMode::Debug) {
        return;
    }

    math::Vec3 movement = math::vec3(0.0f, 0.0f, 0.0f);
    movement = math::add(movement, math::mul(entity_right(owner), context.m_controls.m_move_x));
    movement = math::add(movement, math::mul(entity_forward(owner), context.m_controls.m_move_z));
    movement = flat_normalized(movement);

    m_current_speed = m_walk_speed * speed_multiplier(context.m_controls) * std::sqrt(math::length_squared(movement));
    const float distance = m_current_speed * context.m_delta_seconds;
    if (distance > 0.0f && math::length_squared(movement) > 0.0f) {
        transform.m_position = math::add(transform.m_position, math::mul(movement, distance));
        transform.m_position.y = m_height * 0.5f;
    }
}

} // namespace ofg
