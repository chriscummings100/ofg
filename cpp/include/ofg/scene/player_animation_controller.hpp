// Player locomotion animation controller.
//
// PlayerAnimationController bridges the movement Player component and an
// AnimationPlayer that drives the visible model. It owns no motion or skeleton
// state; it only updates clip blend weights from the latest player speed.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/scene/component.hpp"

namespace ofg {

class AnimationClip;
class AnimationPlayer;
class Entity;
class Player;
struct SceneUpdateContext;

struct LocomotionAnimationWeights {
    float m_idle{1.0f};
    float m_walk{0.0f};
    float m_sprint{0.0f};
};

// Computes stable idle/walk/sprint animation weights from movement speeds.
[[nodiscard]] LocomotionAnimationWeights compute_locomotion_animation_weights(
    float speed, float walk_speed, float sprint_speed);

class PlayerAnimationController : public Component {
public:
    // Binds this controller to one scene-owned entity.
    explicit PlayerAnimationController(Entity* entity) noexcept;

    // Binds the movement source and animation sink used during scene updates.
    void bind(Player& player, AnimationPlayer& animation_player);
    // Binds the locomotion clips controlled by player speed.
    void set_locomotion_clips(AnimationClip& idle_clip, AnimationClip& walk_clip, AnimationClip& sprint_clip);
    // Updates animation clip weights from the latest player speed.
    void update(const SceneUpdateContext& context);

    // Returns the last computed idle weight.
    [[nodiscard]] float idle_weight() const noexcept;
    // Returns the last computed walk weight.
    [[nodiscard]] float walk_weight() const noexcept;
    // Returns the last computed sprint weight.
    [[nodiscard]] float sprint_weight() const noexcept;

private:
    Ptr<Player> m_player;
    Ptr<AnimationPlayer> m_animation_player;
    Ptr<AnimationClip> m_idle_clip;
    Ptr<AnimationClip> m_walk_clip;
    Ptr<AnimationClip> m_sprint_clip;
    float m_idle_weight{1.0f};
    float m_walk_weight{0.0f};
    float m_sprint_weight{0.0f};
    bool m_has_binding{false};
    bool m_has_locomotion_clips{false};
};

} // namespace ofg
