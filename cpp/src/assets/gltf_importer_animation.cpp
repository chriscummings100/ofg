// Internal glTF animation importer.
#include "gltf_importer_animation.hpp"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr std::int32_t _gltf_component_float = 5126;
constexpr std::int32_t _gltf_type_vec3 = 3;
constexpr std::int32_t _gltf_type_vec4 = 4;
constexpr std::int32_t _gltf_type_scalar = 65;

// Returns one accessor after validating its index.
const GltfAccessor& require_accessor(const GltfDocument& document, std::int32_t accessor_index, const char* label) {
    if (accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= document.accessors().size()) {
        throw EngineError(std::string("glTF ") + label + " accessor index is out of range.");
    }
    return document.accessors()[static_cast<std::size_t>(accessor_index)];
}

// Reads one little-endian float component from an accessor data view.
float read_float_component(const GltfAccessorDataView& view, std::size_t element_index, std::size_t component_index) {
    float value = 0.0f;
    const std::byte* source = view.m_data.data() + element_index * view.m_stride + component_index * sizeof(float);
    std::memcpy(&value, source, sizeof(float));
    return value;
}

// Reads one Vec3 element from a FLOAT VEC3 accessor.
math::Vec3 read_vec3(const GltfAccessorDataView& view, std::size_t element_index) {
    return math::vec3(read_float_component(view, element_index, 0),
        read_float_component(view, element_index, 1),
        read_float_component(view, element_index, 2));
}

// Reads one Vec4 element from a FLOAT VEC4 accessor.
math::Vec4 read_vec4(const GltfAccessorDataView& view, std::size_t element_index) {
    return math::vec4(read_float_component(view, element_index, 0),
        read_float_component(view, element_index, 1),
        read_float_component(view, element_index, 2),
        read_float_component(view, element_index, 3));
}

// Validates a FLOAT SCALAR accessor used for animation input times.
void require_float_scalar_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_scalar) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT SCALAR.");
    }
}

// Validates a FLOAT VEC3 accessor used for translation or scale outputs.
void require_float_vec3_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec3) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC3.");
    }
}

// Validates a FLOAT VEC4 accessor used for rotation outputs.
void require_float_vec4_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec4) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC4.");
    }
}

// Converts a glTF interpolation label into an OFG animation interpolation.
AnimationInterpolation convert_interpolation(const std::string& interpolation) {
    if (interpolation.empty() || interpolation == "LINEAR") {
        return AnimationInterpolation::Linear;
    }
    if (interpolation == "STEP") {
        return AnimationInterpolation::Step;
    }
    if (interpolation == "CUBICSPLINE") {
        throw EngineError("glTF animation CUBICSPLINE interpolation is not supported yet.");
    }
    throw EngineError("glTF animation uses unknown interpolation '" + interpolation + "'.");
}

// Converts a glTF animation target path into an OFG target path.
AnimationTargetPath convert_target_path(const std::string& path) {
    if (path == "translation") {
        return AnimationTargetPath::Translation;
    }
    if (path == "rotation") {
        return AnimationTargetPath::Rotation;
    }
    if (path == "scale") {
        return AnimationTargetPath::Scale;
    }
    if (path == "weights") {
        throw EngineError("glTF animation morph target weights are not supported yet.");
    }
    throw EngineError("glTF animation channel uses unknown target path '" + path + "'.");
}

// Reads animation input times from a FLOAT SCALAR accessor.
std::vector<double> read_animation_times(const GltfDocument& document, std::int32_t accessor_index) {
    const GltfAccessor& accessor = require_accessor(document, accessor_index, "animation input");
    require_float_scalar_accessor(accessor, "animation input");
    const GltfAccessorDataView view = document.accessor_data(static_cast<std::size_t>(accessor_index));

    std::vector<double> times;
    times.reserve(accessor.m_count);
    double previous = -1.0;
    for (std::size_t index = 0; index < accessor.m_count; ++index) {
        const double time = static_cast<double>(read_float_component(view, index, 0));
        if (!std::isfinite(time) || time < 0.0) {
            throw EngineError("glTF animation input times must be finite and non-negative.");
        }
        if (index > 0U && time <= previous) {
            throw EngineError("glTF animation input times must be strictly increasing.");
        }
        times.push_back(time);
        previous = time;
    }
    if (times.empty()) {
        throw EngineError("glTF animation input accessor must contain at least one keyframe.");
    }
    return times;
}

// Reads animation output values for one target path.
std::vector<math::Vec4> read_animation_outputs(
    const GltfDocument& document, std::int32_t accessor_index, AnimationTargetPath path, std::size_t expected_count) {
    const GltfAccessor& accessor = require_accessor(document, accessor_index, "animation output");
    if (accessor.m_count != expected_count) {
        throw EngineError("glTF animation output count must match input keyframe count.");
    }
    if (path == AnimationTargetPath::Rotation) {
        require_float_vec4_accessor(accessor, "animation rotation output");
    } else {
        require_float_vec3_accessor(accessor, "animation vector output");
    }

    const GltfAccessorDataView view = document.accessor_data(static_cast<std::size_t>(accessor_index));
    std::vector<math::Vec4> values;
    values.reserve(accessor.m_count);
    for (std::size_t index = 0; index < accessor.m_count; ++index) {
        if (path == AnimationTargetPath::Rotation) {
            const math::Vec4 rotation = read_vec4(view, index);
            std::string error;
            const std::optional<math::Quat> normalized =
                math::normalize(math::Quat{rotation.x, rotation.y, rotation.z, rotation.w}, error);
            if (!normalized.has_value()) {
                throw EngineError("glTF animation rotation output has an invalid quaternion: " + error);
            }
            values.push_back(math::vec4(normalized->x, normalized->y, normalized->z, normalized->w));
        } else {
            const math::Vec3 vector = read_vec3(view, index);
            if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) {
                throw EngineError("glTF animation vector output must be finite.");
            }
            values.push_back(math::vec4(vector.x, vector.y, vector.z, 0.0f));
        }
    }
    return values;
}

} // namespace

namespace gltf_importer_detail {

// Imports one glTF animation into an AnimationClip.
std::unique_ptr<AnimationClip> import_animation_clip(
    const GltfDocument& document, const GltfImportOptions& options, std::uint32_t animation_index) {
    if (animation_index >= document.animations().size()) {
        throw EngineError("glTF animation index is outside the animation table.");
    }
    const GltfAnimation& gltf_animation = document.animations()[animation_index];
    if (gltf_animation.m_channels.empty()) {
        throw EngineError("glTF animation must contain at least one channel.");
    }

    const std::string name = gltf_animation.m_name.empty()
                                 ? options.m_model_name + " animation " + std::to_string(animation_index)
                                 : gltf_animation.m_name;
    auto clip = std::make_unique<AnimationClip>(name);
    double duration = 0.0;
    for (const GltfAnimationChannel& gltf_channel : gltf_animation.m_channels) {
        if (gltf_channel.m_sampler_index < 0 ||
            static_cast<std::size_t>(gltf_channel.m_sampler_index) >= gltf_animation.m_samplers.size()) {
            throw EngineError("glTF animation channel references a sampler outside the animation sampler table.");
        }
        if (gltf_channel.m_target_node_index < 0 ||
            static_cast<std::size_t>(gltf_channel.m_target_node_index) >= document.nodes().size()) {
            throw EngineError("glTF animation channel targets a node outside the node table.");
        }

        const GltfAnimationSampler& sampler =
            gltf_animation.m_samplers[static_cast<std::size_t>(gltf_channel.m_sampler_index)];
        AnimationChannel channel;
        channel.m_target_node_index = static_cast<std::uint32_t>(gltf_channel.m_target_node_index);
        channel.m_target_path = convert_target_path(gltf_channel.m_target_path);
        channel.m_interpolation = convert_interpolation(sampler.m_interpolation);
        channel.m_input_times_seconds = read_animation_times(document, sampler.m_input_accessor_index);
        channel.m_output_values = read_animation_outputs(
            document, sampler.m_output_accessor_index, channel.m_target_path, channel.m_input_times_seconds.size());
        duration = std::max(duration, channel.m_input_times_seconds.back());
        clip->add_channel(std::move(channel));
    }
    clip->set_duration_seconds(duration);
    return clip;
}

} // namespace gltf_importer_detail
} // namespace ofg
