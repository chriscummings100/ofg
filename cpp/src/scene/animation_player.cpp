// Scene animation player implementation.
#include "ofg/scene/animation_player.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/scene/scene_update.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace ofg {
namespace {

// Linearly interpolates scalar values.
float lerp(float a, float b, double t) noexcept {
    return static_cast<float>(static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * t);
}

// Linearly interpolates a Vec3 encoded in Vec4 xyz components.
math::Vec3 lerp_vec3(math::Vec4 a, math::Vec4 b, double t) noexcept {
    return math::vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
}

// Returns a normalized quaternion from an animation value.
math::Quat normalized_quat(math::Vec4 value) {
    std::string error;
    const std::optional<math::Quat> normalized = math::normalize(math::Quat{value.x, value.y, value.z, value.w}, error);
    if (!normalized.has_value()) {
        throw EngineError("Animation rotation keyframe contains an invalid quaternion: " + error);
    }
    return *normalized;
}

// Interpolates quaternions with shortest-path sign handling and normalization.
math::Quat interpolate_quat(math::Vec4 a, math::Vec4 b, double t) {
    math::Quat start = normalized_quat(a);
    math::Quat end = normalized_quat(b);
    const float dot = start.x * end.x + start.y * end.y + start.z * end.z + start.w * end.w;
    if (dot < 0.0f) {
        end.x = -end.x;
        end.y = -end.y;
        end.z = -end.z;
        end.w = -end.w;
    }

    std::string error;
    const std::optional<math::Quat> normalized = math::normalize(
        math::Quat{lerp(start.x, end.x, t), lerp(start.y, end.y, t), lerp(start.z, end.z, t), lerp(start.w, end.w, t)},
        error);
    if (!normalized.has_value()) {
        throw EngineError("Animation rotation interpolation produced an invalid quaternion: " + error);
    }
    return *normalized;
}

// Wraps a time into the clip range when looping is enabled.
double sample_time_for_clip(const AnimationClip& clip, double time_seconds, bool loop) {
    if (!std::isfinite(time_seconds) || time_seconds < 0.0) {
        throw EngineError("AnimationPlayer time must be finite and non-negative.");
    }
    const double duration = clip.duration_seconds();
    if (!loop || duration <= 0.0) {
        return time_seconds;
    }
    double wrapped = std::fmod(time_seconds, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return wrapped;
}

// Finds the lower keyframe index for a sampled time.
std::size_t lower_keyframe_index(const AnimationChannel& channel, double time_seconds) {
    const std::vector<double>& times = channel.m_input_times_seconds;
    if (time_seconds <= times.front()) {
        return 0;
    }
    for (std::size_t index = 0; index + 1U < times.size(); ++index) {
        if (time_seconds < times[index + 1U]) {
            return index;
        }
    }
    return times.size() - 1U;
}

// Validates one animation channel target against this player's bindings.
void validate_channel_target(const AnimationChannel& channel, const std::vector<Ptr<Entity>>& targets_by_node_index) {
    if (channel.m_target_node_index >= targets_by_node_index.size()) {
        throw EngineError("Animation channel targets a node outside this model instance.");
    }
    if (targets_by_node_index[channel.m_target_node_index] == nullptr) {
        throw EngineError("Animation channel target entity has been destroyed.");
    }
    if (channel.m_input_times_seconds.size() != channel.m_output_values.size() ||
        channel.m_input_times_seconds.empty()) {
        throw EngineError("Animation channel has invalid keyframe data.");
    }
}

// Returns the interpolation sample window for one channel and time.
void sample_window(const AnimationChannel& channel,
    double time_seconds,
    std::size_t& lower_index,
    std::size_t& upper_index,
    double& t) {
    lower_index = lower_keyframe_index(channel, time_seconds);
    upper_index = std::min(lower_index + 1U, channel.m_input_times_seconds.size() - 1U);
    t = 0.0;
    if (upper_index != lower_index && channel.m_interpolation == AnimationInterpolation::Linear) {
        const double lower_time = channel.m_input_times_seconds[lower_index];
        const double upper_time = channel.m_input_times_seconds[upper_index];
        t = (time_seconds - lower_time) / (upper_time - lower_time);
        t = std::clamp(t, 0.0, 1.0);
    }
}

// Adds a weighted Vec3 value into an accumulator.
void add_weighted_vec3(math::Vec3& sum, math::Vec3 value, float weight) noexcept {
    sum = math::add(sum, math::mul(value, weight));
}

} // namespace

// Binds this animation player to one scene-owned entity.
AnimationPlayer::AnimationPlayer(Entity* entity) noexcept : Component(ComponentType::AnimationPlayer, entity) {}

// Stores the source-node-index to live-entity binding for a model instance.
void AnimationPlayer::bind_targets(std::vector<Ptr<Entity>> targets_by_node_index) {
    m_rest_transforms_by_node_index.clear();
    m_rest_transforms_by_node_index.reserve(targets_by_node_index.size());
    for (const Ptr<Entity>& target : targets_by_node_index) {
        if (target == nullptr) {
            throw EngineError("AnimationPlayer cannot bind a missing model node entity.");
        }
        m_rest_transforms_by_node_index.push_back(target->local_transform());
    }
    m_pose_accumulators.resize(targets_by_node_index.size());
    m_targets_by_node_index = std::move(targets_by_node_index);
}

// Starts playing one clip from the beginning.
void AnimationPlayer::play(AnimationClip& clip, bool loop) {
    clear_clip_states();
    m_time_seconds = 0.0;
    AnimationClipState state;
    state.m_clip = &clip;
    state.m_loop = loop;
    state.m_weight = 1.0f;
    state.m_playback_speed = 1.0f;
    state.m_playing = true;
    m_clip_states.push_back(std::move(state));
}

// Adds or updates a weighted clip state without resetting its local time.
void AnimationPlayer::set_clip_state(AnimationClip& clip, float weight, bool loop, float playback_speed) {
    if (!std::isfinite(weight) || weight < 0.0f) {
        throw EngineError("Animation clip blend weight must be finite and non-negative.");
    }
    if (!std::isfinite(playback_speed) || playback_speed < 0.0f) {
        throw EngineError("Animation clip playback speed must be finite and non-negative.");
    }

    AnimationClipState* state = find_clip_state(clip);
    if (state == nullptr) {
        AnimationClipState next;
        next.m_clip = &clip;
        next.m_time_seconds = m_time_seconds;
        next.m_weight = weight;
        next.m_playback_speed = playback_speed;
        next.m_loop = loop;
        next.m_playing = true;
        m_clip_states.push_back(std::move(next));
        return;
    }
    state->m_weight = weight;
    state->m_playback_speed = playback_speed;
    state->m_loop = loop;
    state->m_playing = true;
}

// Replaces the blend weight for an existing clip state.
void AnimationPlayer::set_clip_weight(AnimationClip& clip, float weight) {
    if (!std::isfinite(weight) || weight < 0.0f) {
        throw EngineError("Animation clip blend weight must be finite and non-negative.");
    }
    AnimationClipState* state = find_clip_state(clip);
    if (state == nullptr) {
        throw EngineError("AnimationPlayer cannot set the weight for a clip that is not active.");
    }
    state->m_weight = weight;
}

// Removes every active clip state.
void AnimationPlayer::clear_clip_states() noexcept {
    m_clip_states.clear();
    m_time_seconds = 0.0;
}

// Stops playback without changing the current target transforms.
void AnimationPlayer::stop() noexcept {
    for (AnimationClipState& state : m_clip_states) {
        state.m_playing = false;
    }
}

// Sets the local playback time used on the next update.
void AnimationPlayer::set_time_seconds(double time_seconds) {
    if (!std::isfinite(time_seconds) || time_seconds < 0.0) {
        throw EngineError("AnimationPlayer time must be finite and non-negative.");
    }
    m_time_seconds = time_seconds;
    for (AnimationClipState& state : m_clip_states) {
        state.m_time_seconds = time_seconds;
    }
}

// Returns the current local playback time.
double AnimationPlayer::time_seconds() const noexcept {
    return m_time_seconds;
}

// Returns the currently playing clip, if any.
AnimationClip* AnimationPlayer::clip() const noexcept {
    if (m_clip_states.empty()) {
        return nullptr;
    }
    return m_clip_states.front().m_clip.get();
}

// Returns active clip states in evaluation order.
std::span<const AnimationClipState> AnimationPlayer::clip_states() const noexcept {
    return m_clip_states;
}

// Accumulates one weighted channel sample into the target pose accumulator.
void AnimationPlayer::accumulate_channel_sample(const AnimationChannel& channel, double time_seconds, float weight) {
    validate_channel_target(channel, m_targets_by_node_index);
    if (channel.m_target_node_index >= m_pose_accumulators.size()) {
        throw EngineError("Animation pose accumulator is missing a target node.");
    }

    std::size_t lower_index = 0;
    std::size_t upper_index = 0;
    double t = 0.0;
    sample_window(channel, time_seconds, lower_index, upper_index, t);

    PoseAccumulator& accumulator = m_pose_accumulators[channel.m_target_node_index];
    const math::Vec4 lower_value = channel.m_output_values[lower_index];
    const math::Vec4 upper_value = channel.m_output_values[upper_index];
    switch (channel.m_target_path) {
    case AnimationTargetPath::Translation:
        add_weighted_vec3(accumulator.m_position_sum,
            channel.m_interpolation == AnimationInterpolation::Step
                ? math::vec3(lower_value.x, lower_value.y, lower_value.z)
                : lerp_vec3(lower_value, upper_value, t),
            weight);
        accumulator.m_position_weight += weight;
        return;
    case AnimationTargetPath::Scale:
        add_weighted_vec3(accumulator.m_scale_sum,
            channel.m_interpolation == AnimationInterpolation::Step
                ? math::vec3(lower_value.x, lower_value.y, lower_value.z)
                : lerp_vec3(lower_value, upper_value, t),
            weight);
        accumulator.m_scale_weight += weight;
        return;
    case AnimationTargetPath::Rotation: {
        math::Quat value = channel.m_interpolation == AnimationInterpolation::Step
                               ? normalized_quat(lower_value)
                               : interpolate_quat(lower_value, upper_value, t);
        if (!accumulator.m_has_rotation) {
            accumulator.m_rotation_reference = value;
            accumulator.m_has_rotation = true;
        }
        const math::Quat reference = accumulator.m_rotation_reference;
        const float dot = reference.x * value.x + reference.y * value.y + reference.z * value.z + reference.w * value.w;
        if (dot < 0.0f) {
            value.x = -value.x;
            value.y = -value.y;
            value.z = -value.z;
            value.w = -value.w;
        }
        accumulator.m_rotation_sum.x += value.x * weight;
        accumulator.m_rotation_sum.y += value.y * weight;
        accumulator.m_rotation_sum.z += value.z * weight;
        accumulator.m_rotation_sum.w += value.w * weight;
        accumulator.m_rotation_weight += weight;
        return;
    }
    }
    throw EngineError("Animation channel has an unknown target path.");
}

// Applies completed pose accumulators to all live target entity transforms.
void AnimationPlayer::apply_accumulated_poses() {
    for (std::size_t index = 0; index < m_targets_by_node_index.size(); ++index) {
        Entity* target = m_targets_by_node_index[index].get();
        if (target == nullptr) {
            throw EngineError("AnimationPlayer target entity has been destroyed.");
        }
        const PoseAccumulator& accumulator = m_pose_accumulators[index];
        LocalTransform transform = m_rest_transforms_by_node_index[index];
        if (accumulator.m_position_weight > 0.0f) {
            transform.m_position = math::mul(accumulator.m_position_sum, 1.0f / accumulator.m_position_weight);
        }
        if (accumulator.m_scale_weight > 0.0f) {
            transform.m_scale = math::mul(accumulator.m_scale_sum, 1.0f / accumulator.m_scale_weight);
        }
        if (accumulator.m_rotation_weight > 0.0f) {
            std::string error;
            const std::optional<math::Quat> rotation = math::normalize(accumulator.m_rotation_sum, error);
            if (!rotation.has_value()) {
                throw EngineError("Animation rotation blending produced an invalid quaternion: " + error);
            }
            transform.m_rotation = *rotation;
        }
        target->local_transform() = transform;
    }
}

// Updates bound entity local transforms for the current clip.
void AnimationPlayer::update(const SceneUpdateContext& context) {
    if (m_clip_states.empty()) {
        return;
    }
    if (!std::isfinite(context.m_delta_seconds) || context.m_delta_seconds < 0.0f) {
        throw EngineError("AnimationPlayer update requires a finite non-negative delta.");
    }
    if (m_targets_by_node_index.size() != m_rest_transforms_by_node_index.size()) {
        throw EngineError("AnimationPlayer target binding is incomplete.");
    }
    if (m_pose_accumulators.size() != m_targets_by_node_index.size()) {
        throw EngineError("AnimationPlayer pose accumulator binding is incomplete.");
    }

    bool has_playing_state = false;
    for (AnimationClipState& state : m_clip_states) {
        if (!state.m_playing) {
            continue;
        }
        if (state.m_clip == nullptr) {
            throw EngineError("AnimationPlayer clip has been destroyed.");
        }
        state.m_time_seconds += static_cast<double>(context.m_delta_seconds) * state.m_playback_speed;
        has_playing_state = true;
    }
    if (!has_playing_state) {
        return;
    }

    for (PoseAccumulator& accumulator : m_pose_accumulators) {
        accumulator = PoseAccumulator{};
    }
    for (const AnimationClipState& state : m_clip_states) {
        if (!state.m_playing || state.m_weight <= 0.0f) {
            continue;
        }
        AnimationClip* active_clip = state.m_clip.get();
        if (active_clip == nullptr) {
            throw EngineError("AnimationPlayer clip has been destroyed.");
        }
        const double sample_time = sample_time_for_clip(*active_clip, state.m_time_seconds, state.m_loop);
        for (const AnimationChannel& channel : active_clip->channels()) {
            accumulate_channel_sample(channel, sample_time, state.m_weight);
        }
    }

    apply_accumulated_poses();
    if (!m_clip_states.empty()) {
        m_time_seconds = m_clip_states.front().m_time_seconds;
    }
}

// Finds one mutable clip state by object identity.
AnimationClipState* AnimationPlayer::find_clip_state(AnimationClip& clip) noexcept {
    for (AnimationClipState& state : m_clip_states) {
        if (state.m_clip.get() == &clip) {
            return &state;
        }
    }
    return nullptr;
}

} // namespace ofg
