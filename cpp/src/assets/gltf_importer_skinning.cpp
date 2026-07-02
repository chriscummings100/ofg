// Internal glTF skinning attribute importer.
#include "gltf_importer_skinning.hpp"

#include "ofg/core/engine_error.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ofg {
namespace {

constexpr std::int32_t _gltf_component_unsigned_byte = 5121;
constexpr std::int32_t _gltf_component_unsigned_short = 5123;
constexpr std::int32_t _gltf_component_float = 5126;
constexpr std::int32_t _gltf_type_vec3 = 3;
constexpr std::int32_t _gltf_type_vec4 = 4;
constexpr float _minimum_weight_sum = 0.000001f;

// Finds one primitive attribute by glTF semantic.
const GltfAttribute* find_attribute(const GltfPrimitive& primitive, const std::string& semantic) noexcept {
    for (const GltfAttribute& attribute : primitive.m_attributes) {
        if (attribute.m_semantic == semantic) {
            return &attribute;
        }
    }
    return nullptr;
}

// Returns one accessor after validating its index.
const GltfAccessor& require_accessor(const GltfDocument& document, std::int32_t accessor_index, const char* label) {
    if (accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= document.accessors().size()) {
        throw EngineError(std::string("glTF ") + label + " accessor index is out of range.");
    }
    return document.accessors()[static_cast<std::size_t>(accessor_index)];
}

// Validates a FLOAT VEC3 accessor used as the vertex-count source.
void require_float_vec3_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec3) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC3.");
    }
}

// Validates a VEC4 accessor used for joint indices.
void require_joint_accessor(const GltfAccessor& accessor) {
    if (accessor.m_type != _gltf_type_vec4 || (accessor.m_component_type != _gltf_component_unsigned_byte &&
                                                  accessor.m_component_type != _gltf_component_unsigned_short)) {
        throw EngineError("glTF JOINTS_0 accessor must be UNSIGNED_BYTE or UNSIGNED_SHORT VEC4.");
    }
}

// Validates a VEC4 accessor used for weights.
void require_weight_accessor(const GltfAccessor& accessor) {
    if (accessor.m_type != _gltf_type_vec4) {
        throw EngineError("glTF WEIGHTS_0 accessor must be VEC4.");
    }
    if (accessor.m_component_type == _gltf_component_float) {
        return;
    }
    if ((accessor.m_component_type == _gltf_component_unsigned_byte ||
            accessor.m_component_type == _gltf_component_unsigned_short) &&
        accessor.m_normalized) {
        return;
    }
    throw EngineError("glTF WEIGHTS_0 accessor must be FLOAT VEC4 or normalized unsigned integer VEC4.");
}

// Reads one unsigned integer component from a JOINTS_0 accessor.
std::uint32_t read_joint_component(
    const GltfAccessorDataView& view, const GltfAccessor& accessor, std::size_t element, std::size_t component) {
    const std::byte* source = view.m_data.data() + element * view.m_stride;
    if (accessor.m_component_type == _gltf_component_unsigned_byte) {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[component]));
    }
    if (accessor.m_component_type == _gltf_component_unsigned_short) {
        std::uint16_t value = 0;
        std::memcpy(&value, source + component * sizeof(std::uint16_t), sizeof(value));
        return value;
    }
    throw EngineError("glTF JOINTS_0 accessor uses an unsupported component type.");
}

// Reads one normalized weight component.
float read_weight_component(
    const GltfAccessorDataView& view, const GltfAccessor& accessor, std::size_t element, std::size_t component) {
    const std::byte* source = view.m_data.data() + element * view.m_stride;
    if (accessor.m_component_type == _gltf_component_float) {
        float value = 0.0f;
        std::memcpy(&value, source + component * sizeof(float), sizeof(value));
        return value;
    }
    if (accessor.m_component_type == _gltf_component_unsigned_byte) {
        return static_cast<float>(std::to_integer<std::uint8_t>(source[component])) / 255.0f;
    }
    if (accessor.m_component_type == _gltf_component_unsigned_short) {
        std::uint16_t value = 0;
        std::memcpy(&value, source + component * sizeof(std::uint16_t), sizeof(value));
        return static_cast<float>(value) / 65535.0f;
    }
    throw EngineError("glTF WEIGHTS_0 accessor uses an unsupported component type.");
}

// Decodes one primitive's influence attributes.
void append_primitive_influences(
    const GltfDocument& document, const GltfPrimitive& primitive, std::vector<SkinVertexInfluence>& influences) {
    const GltfAttribute* position_attribute = find_attribute(primitive, "POSITION");
    const GltfAttribute* joints_attribute = find_attribute(primitive, "JOINTS_0");
    const GltfAttribute* weights_attribute = find_attribute(primitive, "WEIGHTS_0");
    if (position_attribute == nullptr) {
        throw EngineError("glTF skinned primitive requires a POSITION attribute.");
    }
    if (joints_attribute == nullptr || weights_attribute == nullptr) {
        throw EngineError("glTF skinned primitive requires JOINTS_0 and WEIGHTS_0 attributes.");
    }

    const GltfAccessor& position_accessor =
        require_accessor(document, position_attribute->m_accessor_index, "POSITION");
    const GltfAccessor& joints_accessor = require_accessor(document, joints_attribute->m_accessor_index, "JOINTS_0");
    const GltfAccessor& weights_accessor = require_accessor(document, weights_attribute->m_accessor_index, "WEIGHTS_0");
    require_float_vec3_accessor(position_accessor, "POSITION");
    require_joint_accessor(joints_accessor);
    require_weight_accessor(weights_accessor);
    if (joints_accessor.m_count != position_accessor.m_count || weights_accessor.m_count != position_accessor.m_count) {
        throw EngineError("glTF JOINTS_0 and WEIGHTS_0 accessor counts must match POSITION.");
    }

    const GltfAccessorDataView joints_view =
        document.accessor_data(static_cast<std::size_t>(joints_attribute->m_accessor_index));
    const GltfAccessorDataView weights_view =
        document.accessor_data(static_cast<std::size_t>(weights_attribute->m_accessor_index));
    influences.reserve(influences.size() + position_accessor.m_count);
    for (std::size_t vertex_index = 0; vertex_index < position_accessor.m_count; ++vertex_index) {
        SkinVertexInfluence influence;
        float weight_sum = 0.0f;
        for (std::size_t component = 0; component < 4U; ++component) {
            influence.m_joint_indices[component] =
                read_joint_component(joints_view, joints_accessor, vertex_index, component);
            const float weight = read_weight_component(weights_view, weights_accessor, vertex_index, component);
            if (!std::isfinite(weight) || weight < 0.0f) {
                throw EngineError("glTF WEIGHTS_0 values must be finite and non-negative.");
            }
            influence.m_weights[component] = weight;
            weight_sum += weight;
        }
        if (weight_sum <= _minimum_weight_sum) {
            throw EngineError("glTF WEIGHTS_0 values must contain at least one nonzero weight per vertex.");
        }
        for (float& weight : influence.m_weights) {
            weight /= weight_sum;
        }
        influences.push_back(influence);
    }
}

} // namespace

namespace gltf_importer_detail {

// Imports per-vertex skin influences for one glTF mesh.
std::vector<SkinVertexInfluence> import_skin_vertex_influences(const GltfDocument& document, std::uint32_t mesh_index) {
    if (mesh_index >= document.meshes().size()) {
        throw EngineError("glTF skinning references a mesh index outside the mesh table.");
    }
    const GltfMesh& mesh = document.meshes()[mesh_index];
    if (mesh.m_primitives.empty()) {
        throw EngineError("glTF skinned mesh has no primitives.");
    }

    std::vector<SkinVertexInfluence> influences;
    for (const GltfPrimitive& primitive : mesh.m_primitives) {
        append_primitive_influences(document, primitive, influences);
    }
    return influences;
}

} // namespace gltf_importer_detail
} // namespace ofg
