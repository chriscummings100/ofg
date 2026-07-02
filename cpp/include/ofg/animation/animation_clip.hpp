// Runtime animation clip data imported from model assets.
//
// AnimationClip stores source-node-index channels in engine-owned data. A scene
// AnimationPlayer binds those source node indices to live Entity instances.
#pragma once

#include "ofg/core/object.hpp"
#include "ofg/math/vec.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ofg {

enum class AnimationTargetPath {
    Translation,
    Rotation,
    Scale,
};

enum class AnimationInterpolation {
    Step,
    Linear,
};

struct AnimationChannel {
    std::uint32_t m_target_node_index{0};
    AnimationTargetPath m_target_path{AnimationTargetPath::Translation};
    AnimationInterpolation m_interpolation{AnimationInterpolation::Linear};
    std::vector<double> m_input_times_seconds;
    std::vector<math::Vec4> m_output_values;
};

class AnimationClip : public Object {
public:
    // Creates a named animation clip.
    explicit AnimationClip(std::string name);
    AnimationClip(const AnimationClip&) = delete;
    AnimationClip& operator=(const AnimationClip&) = delete;
    AnimationClip(AnimationClip&&) = delete;
    AnimationClip& operator=(AnimationClip&&) = delete;
    ~AnimationClip() override = default;

    // Returns the imported or generated clip name.
    [[nodiscard]] const std::string& name() const noexcept;
    // Returns the clip duration inferred from its sampler inputs.
    [[nodiscard]] double duration_seconds() const noexcept;
    // Replaces the clip duration after import validation.
    void set_duration_seconds(double duration_seconds);
    // Returns source-node-index animation channels.
    [[nodiscard]] std::span<const AnimationChannel> channels() const noexcept;
    // Appends one validated channel.
    void add_channel(AnimationChannel channel);

private:
    std::string m_name;
    double m_duration_seconds{0.0};
    std::vector<AnimationChannel> m_channels;
};

} // namespace ofg
