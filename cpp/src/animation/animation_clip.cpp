// Runtime animation clip data implementation.
#include "ofg/animation/animation_clip.hpp"

#include "ofg/core/engine_error.hpp"

#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace ofg {

// Creates a named animation clip.
AnimationClip::AnimationClip(std::string name) : m_name(std::move(name)) {
    if (m_name.empty()) {
        throw EngineError("AnimationClip name must not be empty.");
    }
}

// Returns the imported or generated clip name.
const std::string& AnimationClip::name() const noexcept {
    return m_name;
}

// Returns the clip duration inferred from its sampler inputs.
double AnimationClip::duration_seconds() const noexcept {
    return m_duration_seconds;
}

// Replaces the clip duration after import validation.
void AnimationClip::set_duration_seconds(double duration_seconds) {
    if (!std::isfinite(duration_seconds) || duration_seconds < 0.0) {
        throw EngineError("AnimationClip duration must be finite and non-negative.");
    }
    m_duration_seconds = duration_seconds;
}

// Returns source-node-index animation channels.
std::span<const AnimationChannel> AnimationClip::channels() const noexcept {
    return m_channels;
}

// Appends one validated channel.
void AnimationClip::add_channel(AnimationChannel channel) {
    if (channel.m_input_times_seconds.size() != channel.m_output_values.size()) {
        throw EngineError("AnimationClip channel input and output counts must match.");
    }
    if (channel.m_input_times_seconds.empty()) {
        throw EngineError("AnimationClip channel requires at least one keyframe.");
    }
    m_channels.push_back(std::move(channel));
}

} // namespace ofg
