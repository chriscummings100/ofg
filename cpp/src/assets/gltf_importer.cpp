// glTF document import into OFG model resources.
#include "ofg/assets/gltf_importer.hpp"

#include "gltf_importer_animation.hpp"
#include "gltf_importer_geometry.hpp"
#include "gltf_importer_resources.hpp"
#include "gltf_importer_skinning.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr std::int32_t _gltf_mode_triangles = 4;
constexpr std::int32_t _gltf_component_unsigned_byte = 5121;
constexpr std::int32_t _gltf_component_unsigned_short = 5123;
constexpr std::int32_t _gltf_component_unsigned_int = 5125;
constexpr std::int32_t _gltf_component_float = 5126;
constexpr std::int32_t _gltf_type_vec2 = 2;
constexpr std::int32_t _gltf_type_vec3 = 3;
constexpr std::int32_t _gltf_type_vec4 = 4;
constexpr std::int32_t _gltf_type_mat4 = 36;
constexpr std::int32_t _gltf_type_scalar = 65;
constexpr float _matrix_epsilon = 0.0001f;

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

// Reads one little-endian float component from an accessor data view.
float read_float_component(const GltfAccessorDataView& view, std::size_t element_index, std::size_t component_index) {
    float value = 0.0f;
    const std::byte* source = view.m_data.data() + element_index * view.m_stride + component_index * sizeof(float);
    std::memcpy(&value, source, sizeof(float));
    return value;
}

// Reads one Vec2 element from a FLOAT VEC2 accessor.
math::Vec2 read_vec2(const GltfAccessorDataView& view, std::size_t element_index) {
    return math::vec2(read_float_component(view, element_index, 0), read_float_component(view, element_index, 1));
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

// Reads one Mat4 element from a FLOAT MAT4 accessor.
math::Mat4 read_mat4(const GltfAccessorDataView& view, std::size_t element_index) {
    math::Mat4 matrix;
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            matrix[column][row] = read_float_component(view, element_index, column * 4U + row);
        }
    }
    return matrix;
}

// Reads one unsigned integer index from a scalar accessor.
std::uint32_t read_index(const GltfAccessorDataView& view,
    const GltfAccessor& accessor,
    std::size_t element_index,
    const std::uint32_t vertex_base) {
    const std::byte* source = view.m_data.data() + element_index * view.m_stride;
    std::uint32_t value = 0;
    if (accessor.m_component_type == _gltf_component_unsigned_byte) {
        value = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(*source));
    } else if (accessor.m_component_type == _gltf_component_unsigned_short) {
        std::uint16_t decoded = 0;
        std::memcpy(&decoded, source, sizeof(decoded));
        value = decoded;
    } else if (accessor.m_component_type == _gltf_component_unsigned_int) {
        std::memcpy(&value, source, sizeof(value));
    } else {
        throw EngineError("glTF index accessor uses an unsupported component type.");
    }
    return vertex_base + value;
}

// Validates a FLOAT VEC3 accessor used for positions or normals.
void require_float_vec3_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec3) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC3.");
    }
}

// Validates a FLOAT VEC2 accessor used for texture coordinates.
void require_float_vec2_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec2) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC2.");
    }
}

// Validates a FLOAT VEC4 accessor used for tangents.
void require_float_vec4_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_vec4) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT VEC4.");
    }
}

// Validates a FLOAT MAT4 accessor used for inverse bind matrices.
void require_float_mat4_accessor(const GltfAccessor& accessor, const char* label) {
    if (accessor.m_component_type != _gltf_component_float || accessor.m_type != _gltf_type_mat4) {
        throw EngineError(std::string("glTF ") + label + " accessor must be FLOAT MAT4.");
    }
}

// Validates a scalar integer index accessor.
void require_index_accessor(const GltfAccessor& accessor) {
    if (accessor.m_type != _gltf_type_scalar) {
        throw EngineError("glTF index accessor must be SCALAR.");
    }
    if (accessor.m_component_type != _gltf_component_unsigned_byte &&
        accessor.m_component_type != _gltf_component_unsigned_short &&
        accessor.m_component_type != _gltf_component_unsigned_int) {
        throw EngineError("glTF index accessor must use an unsigned integer component type.");
    }
}

// Returns a normalized quaternion from an orthonormal row-major rotation matrix.
math::Quat quat_from_rotation_matrix(float m00,
    float m01,
    float m02,
    float m10,
    float m11,
    float m12,
    float m20,
    float m21,
    float m22,
    const std::string& label) {
    math::Quat quaternion;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion.w = 0.25f * scale;
        quaternion.x = (m21 - m12) / scale;
        quaternion.y = (m02 - m20) / scale;
        quaternion.z = (m10 - m01) / scale;
    } else if (m00 > m11 && m00 > m22) {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        quaternion.w = (m21 - m12) / scale;
        quaternion.x = 0.25f * scale;
        quaternion.y = (m01 + m10) / scale;
        quaternion.z = (m02 + m20) / scale;
    } else if (m11 > m22) {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        quaternion.w = (m02 - m20) / scale;
        quaternion.x = (m01 + m10) / scale;
        quaternion.y = 0.25f * scale;
        quaternion.z = (m12 + m21) / scale;
    } else {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        quaternion.w = (m10 - m01) / scale;
        quaternion.x = (m02 + m20) / scale;
        quaternion.y = (m12 + m21) / scale;
        quaternion.z = 0.25f * scale;
    }

    std::string error;
    std::optional<math::Quat> normalized = math::normalize(quaternion, error);
    if (!normalized.has_value()) {
        throw EngineError("glTF node matrix for '" + label + "' cannot be converted to a rotation: " + error);
    }
    return *normalized;
}

// Returns whether two floats are close enough for transform decomposition checks.
bool near(float lhs, float rhs) noexcept {
    return std::abs(lhs - rhs) <= _matrix_epsilon;
}

// Decomposes an affine glTF column-major matrix into OFG local TRS.
LocalTransform decompose_node_matrix(const GltfNode& node) {
    const std::string label = node.m_name.empty() ? std::string("<unnamed>") : node.m_name;
    const auto& m = node.m_matrix;
    if (!near(static_cast<float>(m[3]), 0.0f) || !near(static_cast<float>(m[7]), 0.0f) ||
        !near(static_cast<float>(m[11]), 0.0f) || !near(static_cast<float>(m[15]), 1.0f)) {
        throw EngineError("glTF node matrix for '" + label + "' is not an affine transform.");
    }

    math::Vec3 c0 = math::vec3(static_cast<float>(m[0]), static_cast<float>(m[1]), static_cast<float>(m[2]));
    math::Vec3 c1 = math::vec3(static_cast<float>(m[4]), static_cast<float>(m[5]), static_cast<float>(m[6]));
    math::Vec3 c2 = math::vec3(static_cast<float>(m[8]), static_cast<float>(m[9]), static_cast<float>(m[10]));
    const float sx = math::length(c0);
    const float sy = math::length(c1);
    const float sz = math::length(c2);
    if (sx <= 0.0f || sy <= 0.0f || sz <= 0.0f) {
        throw EngineError("glTF node matrix for '" + label + "' has a zero scale axis.");
    }

    c0 = math::mul(c0, 1.0f / sx);
    c1 = math::mul(c1, 1.0f / sy);
    c2 = math::mul(c2, 1.0f / sz);
    if (!near(math::dot(c0, c1), 0.0f) || !near(math::dot(c0, c2), 0.0f) || !near(math::dot(c1, c2), 0.0f) ||
        math::dot(math::cross(c0, c1), c2) <= 0.0f) {
        throw EngineError("glTF node matrix for '" + label + "' contains shear or unsupported negative scale.");
    }

    LocalTransform transform;
    transform.m_position = math::vec3(static_cast<float>(m[12]), static_cast<float>(m[13]), static_cast<float>(m[14]));
    transform.m_scale = math::vec3(sx, sy, sz);
    transform.m_rotation = quat_from_rotation_matrix(c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z, label);
    return transform;
}

// Converts glTF node TRS or matrix data into an OFG local transform.
LocalTransform convert_node_transform(const GltfNode& node) {
    if (node.m_has_matrix) {
        if (node.m_has_translation || node.m_has_rotation || node.m_has_scale) {
            throw EngineError("glTF node '" + node.m_name + "' cannot combine matrix with TRS properties.");
        }
        return decompose_node_matrix(node);
    }

    LocalTransform transform;
    if (node.m_has_translation) {
        transform.m_position = math::vec3(static_cast<float>(node.m_translation[0]),
            static_cast<float>(node.m_translation[1]),
            static_cast<float>(node.m_translation[2]));
    }
    if (node.m_has_rotation) {
        transform.m_rotation = math::Quat{static_cast<float>(node.m_rotation[0]),
            static_cast<float>(node.m_rotation[1]),
            static_cast<float>(node.m_rotation[2]),
            static_cast<float>(node.m_rotation[3])};
        std::string error;
        std::optional<math::Quat> normalized = math::normalize(transform.m_rotation, error);
        if (!normalized.has_value()) {
            throw EngineError("glTF node '" + node.m_name + "' has an invalid rotation quaternion: " + error);
        }
        transform.m_rotation = *normalized;
    }
    if (node.m_has_scale) {
        transform.m_scale = math::vec3(static_cast<float>(node.m_scale[0]),
            static_cast<float>(node.m_scale[1]),
            static_cast<float>(node.m_scale[2]));
    }
    return transform;
}

// Appends one supported triangle primitive into a combined mesh.
void append_primitive(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceLoader& loader,
    const GltfPrimitive& primitive,
    std::vector<MeshVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<SubMesh>& submeshes) {
    if (primitive.m_mode != _gltf_mode_triangles) {
        throw EngineError("glTF importer supports only triangle-list primitives.");
    }
    if (primitive.m_morph_target_count != 0) {
        throw EngineError("glTF importer does not support morph targets yet.");
    }

    const GltfAttribute* position_attribute = find_attribute(primitive, "POSITION");
    const GltfAttribute* normal_attribute = find_attribute(primitive, "NORMAL");
    const GltfAttribute* tangent_attribute = find_attribute(primitive, "TANGENT");
    if (position_attribute == nullptr) {
        throw EngineError("glTF primitive requires a POSITION attribute.");
    }
    const GltfAccessor& position_accessor =
        require_accessor(document, position_attribute->m_accessor_index, "POSITION");
    require_float_vec3_accessor(position_accessor, "POSITION");

    const GltfAccessor* normal_accessor = nullptr;
    if (normal_attribute != nullptr) {
        normal_accessor = &require_accessor(document, normal_attribute->m_accessor_index, "NORMAL");
        require_float_vec3_accessor(*normal_accessor, "NORMAL");
        if (normal_accessor->m_count != position_accessor.m_count) {
            throw EngineError("glTF primitive POSITION and NORMAL accessors must have the same count.");
        }
    }

    const GltfAttribute* uv_attribute = find_attribute(primitive, "TEXCOORD_0");
    const GltfAccessor* uv_accessor = nullptr;
    const bool needs_uvs = gltf_importer_detail::primitive_requires_uvs(document, primitive);
    const bool needs_generated_tangents =
        tangent_attribute == nullptr && gltf_importer_detail::primitive_uses_normal_texture(document, primitive);
    if (uv_attribute == nullptr && needs_uvs) {
        throw EngineError("glTF primitive material uses textures but TEXCOORD_0 is missing.");
    }
    if (uv_attribute != nullptr) {
        uv_accessor = &require_accessor(document, uv_attribute->m_accessor_index, "TEXCOORD_0");
        require_float_vec2_accessor(*uv_accessor, "TEXCOORD_0");
        if (uv_accessor->m_count != position_accessor.m_count) {
            throw EngineError("glTF primitive TEXCOORD_0 accessor must match POSITION count.");
        }
    }

    const GltfAccessor* tangent_accessor = nullptr;
    if (tangent_attribute != nullptr) {
        tangent_accessor = &require_accessor(document, tangent_attribute->m_accessor_index, "TANGENT");
        require_float_vec4_accessor(*tangent_accessor, "TANGENT");
        if (tangent_accessor->m_count != position_accessor.m_count) {
            throw EngineError("glTF primitive TANGENT accessor must match POSITION count.");
        }
    }

    const GltfAccessorDataView position_view =
        document.accessor_data(static_cast<std::size_t>(position_attribute->m_accessor_index));
    const std::optional<GltfAccessorDataView> normal_view =
        normal_attribute == nullptr ? std::nullopt
                                    : std::optional<GltfAccessorDataView>{document.accessor_data(
                                          static_cast<std::size_t>(normal_attribute->m_accessor_index))};
    const std::optional<GltfAccessorDataView> uv_view =
        uv_attribute == nullptr ? std::nullopt
                                : std::optional<GltfAccessorDataView>{
                                      document.accessor_data(static_cast<std::size_t>(uv_attribute->m_accessor_index))};
    const std::optional<GltfAccessorDataView> tangent_view =
        tangent_attribute == nullptr ? std::nullopt
                                     : std::optional<GltfAccessorDataView>{document.accessor_data(
                                           static_cast<std::size_t>(tangent_attribute->m_accessor_index))};

    const std::uint32_t vertex_base = static_cast<std::uint32_t>(vertices.size());
    for (std::size_t vertex_index = 0; vertex_index < position_accessor.m_count; ++vertex_index) {
        const math::Vec3 position = read_vec3(position_view, vertex_index);
        const math::Vec3 normal =
            normal_view.has_value() ? read_vec3(*normal_view, vertex_index) : math::vec3(0.0f, 0.0f, 0.0f);
        const math::Vec2 uv = uv_view.has_value() ? read_vec2(*uv_view, vertex_index) : math::vec2(0.0f, 0.0f);
        if (tangent_view.has_value()) {
            const math::Vec4 tangent = read_vec4(*tangent_view, vertex_index);
            vertices.push_back(MeshVertex{{position.x, position.y, position.z},
                {normal.x, normal.y, normal.z},
                {tangent.x, tangent.y, tangent.z, tangent.w},
                {uv.x, uv.y}});
        } else {
            vertices.push_back(
                MeshVertex{{position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {uv.x, uv.y}});
        }
    }

    const std::uint32_t index_start = static_cast<std::uint32_t>(indices.size());
    if (primitive.m_indices_accessor_index >= 0) {
        const GltfAccessor& index_accessor = require_accessor(document, primitive.m_indices_accessor_index, "index");
        require_index_accessor(index_accessor);
        const GltfAccessorDataView index_view =
            document.accessor_data(static_cast<std::size_t>(primitive.m_indices_accessor_index));
        for (std::size_t index = 0; index < index_accessor.m_count; ++index) {
            indices.push_back(read_index(index_view, index_accessor, index, vertex_base));
        }
    } else {
        for (std::size_t index = 0; index < position_accessor.m_count; ++index) {
            indices.push_back(vertex_base + static_cast<std::uint32_t>(index));
        }
    }

    const std::uint32_t index_count = static_cast<std::uint32_t>(indices.size()) - index_start;
    if (index_count % 3U != 0U) {
        throw EngineError("glTF triangle-list primitive index count must be divisible by three.");
    }
    if (!normal_view.has_value()) {
        gltf_importer_detail::generate_normals(vertices,
            indices,
            index_start,
            index_count,
            vertex_base,
            static_cast<std::uint32_t>(position_accessor.m_count));
    }
    if (needs_generated_tangents) {
        gltf_importer_detail::generate_tangents(vertices,
            indices,
            index_start,
            index_count,
            vertex_base,
            static_cast<std::uint32_t>(position_accessor.m_count));
    }
    Material& material = gltf_importer_detail::material_for_primitive(document, options, loader, primitive);
    submeshes.push_back(SubMesh{
        options.m_model_name + " primitive " + std::to_string(submeshes.size()), index_start, index_count, &material});
}

// Imports one glTF mesh into a cached OFG Mesh resource.
Mesh& import_mesh(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceLoader& loader,
    std::uint32_t mesh_index) {
    if (mesh_index >= document.meshes().size()) {
        throw EngineError("glTF node references a mesh index outside the mesh table.");
    }
    const GltfMesh& gltf_mesh = document.meshes()[mesh_index];
    if (gltf_mesh.m_primitives.empty()) {
        throw EngineError("glTF mesh has no primitives.");
    }

    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubMesh> submeshes;
    for (const GltfPrimitive& primitive : gltf_mesh.m_primitives) {
        append_primitive(document, options, loader, primitive, vertices, indices, submeshes);
    }

    const std::string label = gltf_mesh.m_name.empty() ? options.m_model_name + " mesh " + std::to_string(mesh_index)
                                                       : options.m_model_name + " " + gltf_mesh.m_name;
    return loader.get_or_create_mesh(
        gltf_importer_detail::source_key(document, options) + "#mesh/" + std::to_string(mesh_index),
        label,
        std::move(vertices),
        std::move(indices),
        std::move(submeshes));
}

// Imports one glTF skin into a model-resource skin template.
SkinTemplate import_skin(const GltfDocument& document, std::uint32_t skin_index) {
    if (skin_index >= document.skins().size()) {
        throw EngineError("glTF skin index is outside the skin table.");
    }
    const GltfSkin& gltf_skin = document.skins()[skin_index];
    if (gltf_skin.m_joint_node_indices.empty()) {
        throw EngineError("glTF skin must contain at least one joint.");
    }

    SkinTemplate skin;
    skin.m_name = gltf_skin.m_name;
    skin.m_source_skin_index = skin_index;
    skin.m_joint_node_indices.reserve(gltf_skin.m_joint_node_indices.size());
    for (const std::int32_t joint_node_index : gltf_skin.m_joint_node_indices) {
        if (joint_node_index < 0 || static_cast<std::size_t>(joint_node_index) >= document.nodes().size()) {
            throw EngineError("glTF skin references a joint node outside the node table.");
        }
        skin.m_joint_node_indices.push_back(static_cast<std::uint32_t>(joint_node_index));
    }

    if (gltf_skin.m_skeleton_node_index >= 0) {
        if (static_cast<std::size_t>(gltf_skin.m_skeleton_node_index) >= document.nodes().size()) {
            throw EngineError("glTF skin references a skeleton root outside the node table.");
        }
        skin.m_skeleton_root_node_index = static_cast<std::uint32_t>(gltf_skin.m_skeleton_node_index);
    }

    if (gltf_skin.m_inverse_bind_matrices_accessor_index >= 0) {
        const GltfAccessor& inverse_bind_accessor =
            require_accessor(document, gltf_skin.m_inverse_bind_matrices_accessor_index, "inverseBindMatrices");
        require_float_mat4_accessor(inverse_bind_accessor, "inverseBindMatrices");
        if (inverse_bind_accessor.m_count != skin.m_joint_node_indices.size()) {
            throw EngineError("glTF inverseBindMatrices accessor count must match the skin joint count.");
        }
        const GltfAccessorDataView inverse_bind_view =
            document.accessor_data(static_cast<std::size_t>(gltf_skin.m_inverse_bind_matrices_accessor_index));
        skin.m_inverse_bind_matrices.reserve(inverse_bind_accessor.m_count);
        for (std::size_t matrix_index = 0; matrix_index < inverse_bind_accessor.m_count; ++matrix_index) {
            skin.m_inverse_bind_matrices.push_back(read_mat4(inverse_bind_view, matrix_index));
        }
    } else {
        skin.m_inverse_bind_matrices.assign(skin.m_joint_node_indices.size(), math::mat4_identity());
    }

    return skin;
}

// Computes parent indices and validates child references.
std::vector<std::int32_t> parent_indices_for_nodes(const GltfDocument& document) {
    std::vector<std::int32_t> parents(document.nodes().size(), -1);
    for (std::size_t node_index = 0; node_index < document.nodes().size(); ++node_index) {
        for (std::int32_t child_index : document.nodes()[node_index].m_child_node_indices) {
            if (child_index < 0 || static_cast<std::size_t>(child_index) >= document.nodes().size()) {
                throw EngineError("glTF node references a child outside the node table.");
            }
            std::int32_t& parent = parents[static_cast<std::size_t>(child_index)];
            if (parent != -1) {
                throw EngineError("glTF node graph is not a tree; a node has multiple parents.");
            }
            parent = static_cast<std::int32_t>(node_index);
        }
    }
    return parents;
}

// Appends model root indices from nodes with no parent.
void add_root_nodes(ModelResourceBuilder& builder, const std::vector<std::int32_t>& parents) {
    for (std::size_t node_index = 0; node_index < parents.size(); ++node_index) {
        if (parents[node_index] == -1) {
            builder.add_root_node_index(static_cast<std::uint32_t>(node_index));
        }
    }
}

} // namespace

// Converts a parsed glTF document into a reusable model resource.
std::unique_ptr<ModelResource> import_gltf_model_resource(
    const GltfDocument& document, const GltfImportOptions& options, ModelResourceLoader& loader) {
    auto resource = std::make_unique<ModelResource>();
    import_gltf_model_resource_into(document, options, loader, *resource);
    return resource;
}

// Converts a parsed glTF document into an existing reusable model resource.
void import_gltf_model_resource_into(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceLoader& loader,
    ModelResource& resource) {
    if (options.m_model_name.empty()) {
        throw EngineError("GltfImportOptions requires a non-empty model name.");
    }
    if (!document.extensions_required().empty()) {
        std::string message = "glTF document requires unsupported extensions:";
        for (const std::string& extension : document.extensions_required()) {
            message += " " + extension;
        }
        throw EngineError(message);
    }
    if (document.nodes().empty()) {
        throw EngineError("glTF document has no nodes to import.");
    }

    const std::vector<std::int32_t> parent_indices = parent_indices_for_nodes(document);
    ModelResourceBuilder builder(options.m_model_name);
    add_root_nodes(builder, parent_indices);
    for (std::size_t skin_index = 0; skin_index < document.skins().size(); ++skin_index) {
        builder.add_skin(import_skin(document, static_cast<std::uint32_t>(skin_index)));
    }
    for (std::size_t animation_index = 0; animation_index < document.animations().size(); ++animation_index) {
        builder.add_animation_clip(gltf_importer_detail::import_animation_clip(
            document, options, static_cast<std::uint32_t>(animation_index)));
    }

    for (std::size_t node_index = 0; node_index < document.nodes().size(); ++node_index) {
        const GltfNode& node = document.nodes()[node_index];
        ModelNodeTemplate node_template;
        node_template.m_name = node.m_name;
        node_template.m_source_node_index = static_cast<std::uint32_t>(node_index);
        node_template.m_parent_node_index = parent_indices[node_index];
        node_template.m_local_transform = convert_node_transform(node);
        node_template.m_child_node_indices.reserve(node.m_child_node_indices.size());
        for (const std::int32_t child_index : node.m_child_node_indices) {
            node_template.m_child_node_indices.push_back(static_cast<std::uint32_t>(child_index));
        }
        builder.add_node(std::move(node_template));

        if (node.m_skin_index >= 0 && node.m_mesh_index < 0) {
            throw EngineError("glTF node references a skin but has no mesh.");
        }
        if (node.m_mesh_index >= 0) {
            Mesh& mesh = import_mesh(document, options, loader, static_cast<std::uint32_t>(node.m_mesh_index));
            MeshRendererTemplate mesh_renderer_template;
            mesh_renderer_template.m_node_index = static_cast<std::uint32_t>(node_index);
            mesh_renderer_template.m_mesh = &mesh;
            if (node.m_skin_index >= 0) {
                if (static_cast<std::size_t>(node.m_skin_index) >= document.skins().size()) {
                    throw EngineError("glTF node references a skin index outside the skin table.");
                }
                mesh_renderer_template.m_skin_template_index = static_cast<std::uint32_t>(node.m_skin_index);
                mesh_renderer_template.m_skin_vertex_influences = gltf_importer_detail::import_skin_vertex_influences(
                    document, static_cast<std::uint32_t>(node.m_mesh_index));
            }
            builder.add_mesh_renderer(std::move(mesh_renderer_template));
        }
    }

    builder.build_into(resource);
}

} // namespace ofg
