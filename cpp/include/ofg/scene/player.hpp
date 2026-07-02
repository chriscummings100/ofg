// Player scene component.
//
// Player owns flat-plane movement for the controllable entity plus the first
// hardcoded visible-player model and locomotion animation binding. Game creates
// and caches the component, while Player keeps player-specific scene/resource
// details local to the component.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/scene/component.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace ofg {

class AnimationClip;
class AnimationPlayer;
class MeshRenderer;
class ModelResource;
class ModelResourceImportContext;
class Scene;
struct SceneUpdateContext;

struct LocomotionAnimationWeights {
    float m_idle{1.0f};
    float m_walk{0.0f};
    float m_sprint{0.0f};
};

// Computes stable idle/walk/sprint animation weights from movement speeds.
[[nodiscard]] LocomotionAnimationWeights compute_locomotion_animation_weights(
    float speed, float walk_speed, float sprint_speed);

class Player : public Component {
public:
    // Binds this player to one scene-owned entity.
    explicit Player(Entity* entity) noexcept;
    // Releases player-owned model resources after the concrete resource types are complete.
    ~Player() override;

    // Returns the walking speed in world units per second.
    [[nodiscard]] float walk_speed() const noexcept;
    // Returns the fast movement speed in world units per second.
    [[nodiscard]] float fast_speed() const noexcept;
    // Replaces the walking speed after validating it is finite and non-negative.
    void set_walk_speed(float speed);
    // Returns the height used to keep the centered player box grounded.
    [[nodiscard]] float height() const noexcept;
    // Replaces the player height after validating it is finite and positive.
    void set_height(float height);
    // Returns the latest intended flat movement speed in world units per second.
    [[nodiscard]] float current_speed() const noexcept;
    // Imports and attaches the default hardcoded player model to this player entity.
    void load_default_model(
        GpuContext gpu, Scene& scene, std::span<const std::byte> player_glb, std::span<const std::byte> animation_glb);
    // Returns whether the hardcoded player model has been imported and attached.
    [[nodiscard]] bool default_model_loaded() const noexcept;
    // Binds the mesh renderer used as a visible fallback while the model is unavailable.
    void bind_fallback_renderer(MeshRenderer& renderer);
    // Sets whether the fallback renderer is currently visible.
    void set_fallback_visible(bool visible) noexcept;
    // Returns whether the fallback renderer is currently visible.
    [[nodiscard]] bool fallback_visible() const noexcept;
    // Binds the animation player and clips driven by this player's movement speed.
    void bind_locomotion_animation(AnimationPlayer& animation_player,
        AnimationClip& idle_clip,
        AnimationClip& walk_clip,
        AnimationClip& sprint_clip);
    // Returns the last computed idle animation weight.
    [[nodiscard]] float idle_animation_weight() const noexcept;
    // Returns the last computed walk animation weight.
    [[nodiscard]] float walk_animation_weight() const noexcept;
    // Returns the last computed sprint animation weight.
    [[nodiscard]] float sprint_animation_weight() const noexcept;
    // Applies player-relevant controls for one frame.
    void update(const SceneUpdateContext& context);

private:
    // Updates locomotion clip weights from the current movement speed.
    void update_locomotion_animation();

    float m_walk_speed{3.5f};
    float m_height{1.8f};
    float m_current_speed{0.0f};
    bool m_fallback_visible{true};
    bool m_default_model_loaded{false};
    std::unique_ptr<ModelResourceImportContext> m_model_import_context;
    std::unique_ptr<ModelResource> m_model_resource;
    std::unique_ptr<ModelResource> m_animation_resource;
    std::vector<std::unique_ptr<AnimationClip>> m_locomotion_clips;
    Ptr<MeshRenderer> m_fallback_renderer;
    Ptr<Entity> m_model_root_entity;
    Ptr<AnimationPlayer> m_model_animation_player;
    Ptr<AnimationPlayer> m_locomotion_animation_player;
    Ptr<AnimationClip> m_idle_clip;
    Ptr<AnimationClip> m_walk_clip;
    Ptr<AnimationClip> m_sprint_clip;
    float m_idle_animation_weight{1.0f};
    float m_walk_animation_weight{0.0f};
    float m_sprint_animation_weight{0.0f};
    bool m_has_locomotion_clips{false};
};

} // namespace ofg
