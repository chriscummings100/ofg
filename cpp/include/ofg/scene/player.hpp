// Player scene component.
//
// Player owns flat-plane movement for the controllable entity plus the first
// hardcoded visible-player model and locomotion animation binding. Game creates
// and caches the component, while Player keeps player-specific scene/resource
// details local to the component.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/component.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ofg {

class AnimationClip;
class AnimationPlayer;
class MeshRenderer;
class ModelResource;
struct RuntimeDebugStatus;
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
    // Returns whether the hardcoded player model has been imported and attached.
    [[nodiscard]] bool default_model_loaded() const noexcept;
    // Returns the current default model loading state for debug status.
    [[nodiscard]] const std::string& default_model_loading_state() const noexcept;
    // Returns the current default model loading error, or an empty string.
    [[nodiscard]] const std::string& default_model_load_error() const noexcept;
    // Publishes player-model loading fields into the public runtime debug snapshot.
    void publish_default_model_debug_status(RuntimeDebugStatus& status, std::string& last_error) const noexcept;
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
    // Requests and imports the default player model through model resources.
    void update_default_model_load(const SceneUpdateContext& context) noexcept;
    // Requests the default model and animation-library resources.
    void request_default_model_resources();
    // Binds loaded default model resources into the scene.
    void bind_loaded_default_model_resources(Scene& scene);
    // Records a default model load failure and keeps the fallback visible.
    void fail_default_model_load(std::string message) noexcept;

    float m_walk_speed{3.5f};
    float m_height{1.8f};
    float m_current_speed{0.0f};
    bool m_fallback_visible{true};
    bool m_default_model_loaded{false};
    std::string m_default_model_loading_state{"not_requested"};
    std::string m_default_model_load_error;
    Ptr<ModelResource> m_model_resource;
    Ptr<ModelResource> m_animation_resource;
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
