// Scene component that plays imported animation clips on entity transforms.
//
// AnimationPlayer is owned by Scene like other components. It binds a model
// instance's source-node-index table to live entities and writes local
// translation/rotation/scale during Scene::update.
#pragma once

#include "ofg/animation/animation_clip.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/scene/component.hpp"
#include "ofg/scene/entity.hpp"

#include <span>
#include <vector>

namespace ofg {

struct SceneUpdateContext;

struct AnimationClipState {
    Ptr<AnimationClip> m_clip;
    double m_time_seconds{0.0};
    float m_weight{1.0f};
    float m_playback_speed{1.0f};
    bool m_loop{true};
    bool m_playing{true};
};

class AnimationPlayer : public Component {
public:
    // Binds this animation player to one scene-owned entity.
    explicit AnimationPlayer(Entity* entity) noexcept;

    // Stores the source-node-index to live-entity binding for a model instance.
    void bind_targets(std::vector<Ptr<Entity>> targets_by_node_index);
    // Starts playing one clip from the beginning.
    void play(AnimationClip& clip, bool loop = true);
    // Adds or updates a weighted clip state without resetting its local time.
    void set_clip_state(AnimationClip& clip, float weight, bool loop = true, float playback_speed = 1.0f);
    // Replaces the blend weight for an existing clip state.
    void set_clip_weight(AnimationClip& clip, float weight);
    // Removes every active clip state.
    void clear_clip_states() noexcept;
    // Stops playback without changing the current target transforms.
    void stop() noexcept;
    // Sets the local playback time used on the next update.
    void set_time_seconds(double time_seconds);
    // Returns the current local playback time.
    [[nodiscard]] double time_seconds() const noexcept;
    // Returns the currently playing clip, if any.
    [[nodiscard]] AnimationClip* clip() const noexcept;
    // Returns active clip states in evaluation order.
    [[nodiscard]] std::span<const AnimationClipState> clip_states() const noexcept;
    // Updates bound entity local transforms for the current clip.
    void update(const SceneUpdateContext& context);

private:
    struct PoseAccumulator {
        math::Vec3 m_position_sum{0.0f, 0.0f, 0.0f};
        float m_position_weight{0.0f};
        math::Vec3 m_scale_sum{0.0f, 0.0f, 0.0f};
        float m_scale_weight{0.0f};
        math::Quat m_rotation_sum{0.0f, 0.0f, 0.0f, 0.0f};
        math::Quat m_rotation_reference{math::quat_identity()};
        float m_rotation_weight{0.0f};
        bool m_has_rotation{false};
    };

    [[nodiscard]] AnimationClipState* find_clip_state(AnimationClip& clip) noexcept;
    void accumulate_channel_sample(const AnimationChannel& channel, double time_seconds, float weight);
    void apply_accumulated_poses();

    std::vector<Ptr<Entity>> m_targets_by_node_index;
    std::vector<LocalTransform> m_rest_transforms_by_node_index;
    std::vector<AnimationClipState> m_clip_states;
    std::vector<PoseAccumulator> m_pose_accumulators;
    double m_time_seconds{0.0};
};

} // namespace ofg
