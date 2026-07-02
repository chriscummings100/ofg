// Player locomotion animation controller implementation.
#include "ofg/scene/player_animation_controller.hpp"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/player.hpp"

#include <algorithm>
#include <cmath>

namespace ofg {
namespace {

constexpr float _minimum_speed = 0.0001f;

} // namespace

// Computes stable idle/walk/sprint animation weights from movement speeds.
LocomotionAnimationWeights compute_locomotion_animation_weights(float speed, float walk_speed, float sprint_speed) {
    if (!std::isfinite(speed) || speed < 0.0f) {
        throw EngineError("PlayerAnimationController requires a finite non-negative player speed.");
    }
    if (!std::isfinite(walk_speed) || walk_speed < 0.0f) {
        throw EngineError("PlayerAnimationController requires a finite non-negative walk speed.");
    }
    if (!std::isfinite(sprint_speed) || sprint_speed < 0.0f) {
        throw EngineError("PlayerAnimationController requires a finite non-negative sprint speed.");
    }

    LocomotionAnimationWeights weights;
    if (speed <= _minimum_speed) {
        return weights;
    }
    if (walk_speed <= _minimum_speed) {
        weights.m_idle = 0.0f;
        weights.m_sprint = 1.0f;
        return weights;
    }
    if (speed <= walk_speed) {
        const float walk_blend = std::clamp(speed / walk_speed, 0.0f, 1.0f);
        weights.m_idle = 1.0f - walk_blend;
        weights.m_walk = walk_blend;
        return weights;
    }

    const float sprint_reference_speed = std::max(sprint_speed, walk_speed + _minimum_speed);
    const float sprint_blend = std::clamp((speed - walk_speed) / (sprint_reference_speed - walk_speed), 0.0f, 1.0f);
    weights.m_idle = 0.0f;
    weights.m_walk = 1.0f - sprint_blend;
    weights.m_sprint = sprint_blend;
    return weights;
}

// Binds this controller to one scene-owned entity.
PlayerAnimationController::PlayerAnimationController(Entity* entity) noexcept
    : Component(ComponentType::PlayerAnimationController, entity) {}

// Binds the movement source and animation sink used during scene updates.
void PlayerAnimationController::bind(Player& player, AnimationPlayer& animation_player) {
    m_player = &player;
    m_animation_player = &animation_player;
    m_has_binding = true;
}

// Binds the locomotion clips controlled by player speed.
void PlayerAnimationController::set_locomotion_clips(
    AnimationClip& idle_clip, AnimationClip& walk_clip, AnimationClip& sprint_clip) {
    m_idle_clip = &idle_clip;
    m_walk_clip = &walk_clip;
    m_sprint_clip = &sprint_clip;
    m_has_locomotion_clips = true;
}

// Updates animation clip weights from the latest player speed.
void PlayerAnimationController::update(const SceneUpdateContext&) {
    if (!m_has_binding || !m_has_locomotion_clips) {
        return;
    }
    Player* player = m_player.get();
    AnimationPlayer* animation_player = m_animation_player.get();
    AnimationClip* idle_clip = m_idle_clip.get();
    AnimationClip* walk_clip = m_walk_clip.get();
    AnimationClip* sprint_clip = m_sprint_clip.get();
    if (player == nullptr || animation_player == nullptr) {
        throw EngineError("PlayerAnimationController binding target has been destroyed.");
    }
    if (idle_clip == nullptr || walk_clip == nullptr || sprint_clip == nullptr) {
        throw EngineError("PlayerAnimationController locomotion clip has been destroyed.");
    }

    const LocomotionAnimationWeights weights =
        compute_locomotion_animation_weights(player->current_speed(), player->walk_speed(), player->fast_speed());
    m_idle_weight = weights.m_idle;
    m_walk_weight = weights.m_walk;
    m_sprint_weight = weights.m_sprint;
    animation_player->set_clip_state(*idle_clip, m_idle_weight, true, 1.0f);
    animation_player->set_clip_state(*walk_clip, m_walk_weight, true, 1.0f);
    animation_player->set_clip_state(*sprint_clip, m_sprint_weight, true, 1.0f);
}

// Returns the last computed idle weight.
float PlayerAnimationController::idle_weight() const noexcept {
    return m_idle_weight;
}

// Returns the last computed walk weight.
float PlayerAnimationController::walk_weight() const noexcept {
    return m_walk_weight;
}

// Returns the last computed sprint weight.
float PlayerAnimationController::sprint_weight() const noexcept {
    return m_sprint_weight;
}

} // namespace ofg
