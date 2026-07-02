// Accessors and byte-view helpers for the OFG glTF document snapshot.
#include "ofg/assets/gltf_document.hpp"

#include "ofg/core/engine_error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ofg {
namespace {

constexpr std::int32_t _gltf_component_byte = 5120;
constexpr std::int32_t _gltf_component_unsigned_byte = 5121;
constexpr std::int32_t _gltf_component_short = 5122;
constexpr std::int32_t _gltf_component_unsigned_short = 5123;
constexpr std::int32_t _gltf_component_int = 5124;
constexpr std::int32_t _gltf_component_unsigned_int = 5125;
constexpr std::int32_t _gltf_component_float = 5126;
constexpr std::int32_t _gltf_component_double = 5130;
constexpr std::int32_t _gltf_type_vec2 = 2;
constexpr std::int32_t _gltf_type_vec3 = 3;
constexpr std::int32_t _gltf_type_vec4 = 4;
constexpr std::int32_t _gltf_type_mat2 = 34;
constexpr std::int32_t _gltf_type_mat3 = 35;
constexpr std::int32_t _gltf_type_mat4 = 36;
constexpr std::int32_t _gltf_type_scalar = 65;

// Returns the size in bytes of one accessor component.
std::size_t component_type_size(std::int32_t component_type) {
    switch (component_type) {
    case _gltf_component_byte:
    case _gltf_component_unsigned_byte:
        return 1;
    case _gltf_component_short:
    case _gltf_component_unsigned_short:
        return 2;
    case _gltf_component_int:
    case _gltf_component_unsigned_int:
    case _gltf_component_float:
        return 4;
    case _gltf_component_double:
        return 8;
    default:
        throw EngineError("glTF accessor has unsupported component type " + std::to_string(component_type) + ".");
    }
}

// Returns the number of components in one accessor element.
std::size_t accessor_type_component_count(std::int32_t type) {
    switch (type) {
    case _gltf_type_scalar:
        return 1;
    case _gltf_type_vec2:
        return 2;
    case _gltf_type_vec3:
        return 3;
    case _gltf_type_vec4:
    case _gltf_type_mat2:
        return 4;
    case _gltf_type_mat3:
        return 9;
    case _gltf_type_mat4:
        return 16;
    default:
        throw EngineError("glTF accessor has unsupported accessor type " + std::to_string(type) + ".");
    }
}

} // namespace

const std::string& GltfDocument::label() const noexcept {
    return m_label;
}

bool GltfDocument::is_binary() const noexcept {
    return m_is_binary;
}

std::int32_t GltfDocument::default_scene_index() const noexcept {
    return m_default_scene_index;
}

const std::string& GltfDocument::warnings() const noexcept {
    return m_warnings;
}

std::span<const GltfBuffer> GltfDocument::buffers() const noexcept {
    return m_buffers;
}

std::span<const GltfBufferView> GltfDocument::buffer_views() const noexcept {
    return m_buffer_views;
}

std::span<const GltfAccessor> GltfDocument::accessors() const noexcept {
    return m_accessors;
}

std::span<const GltfImage> GltfDocument::images() const noexcept {
    return m_images;
}

std::span<const GltfTexture> GltfDocument::textures() const noexcept {
    return m_textures;
}

std::span<const GltfMaterial> GltfDocument::materials() const noexcept {
    return m_materials;
}

std::span<const GltfMesh> GltfDocument::meshes() const noexcept {
    return m_meshes;
}

std::span<const GltfNode> GltfDocument::nodes() const noexcept {
    return m_nodes;
}

std::span<const GltfSkin> GltfDocument::skins() const noexcept {
    return m_skins;
}

std::span<const GltfAnimation> GltfDocument::animations() const noexcept {
    return m_animations;
}

std::span<const std::string> GltfDocument::extensions_used() const noexcept {
    return m_extensions_used;
}

std::span<const std::string> GltfDocument::extensions_required() const noexcept {
    return m_extensions_required;
}

std::size_t GltfDocument::scene_count() const noexcept {
    return m_scene_count;
}

std::size_t GltfDocument::node_count() const noexcept {
    return m_node_count;
}

std::size_t GltfDocument::mesh_count() const noexcept {
    return m_mesh_count;
}

std::size_t GltfDocument::material_count() const noexcept {
    return m_material_count;
}

std::size_t GltfDocument::skin_count() const noexcept {
    return m_skin_count;
}

std::size_t GltfDocument::animation_count() const noexcept {
    return m_animation_count;
}

GltfAccessorDataView GltfDocument::accessor_data(std::size_t accessor_index) const {
    if (accessor_index >= m_accessors.size()) {
        throw EngineError("glTF accessor index is out of range.");
    }
    const GltfAccessor& accessor = m_accessors[accessor_index];
    if (accessor.m_sparse_count != 0) {
        throw EngineError("glTF sparse accessor data view is not supported yet.");
    }
    if (accessor.m_buffer_view_index < 0 ||
        static_cast<std::size_t>(accessor.m_buffer_view_index) >= m_buffer_views.size()) {
        throw EngineError("glTF accessor has no buffer view.");
    }

    const GltfBufferView& buffer_view = m_buffer_views[static_cast<std::size_t>(accessor.m_buffer_view_index)];
    if (buffer_view.m_buffer_index < 0 || static_cast<std::size_t>(buffer_view.m_buffer_index) >= m_buffers.size()) {
        throw EngineError("glTF accessor references a missing buffer.");
    }

    const GltfBuffer& buffer = m_buffers[static_cast<std::size_t>(buffer_view.m_buffer_index)];
    const std::size_t component_size = component_type_size(accessor.m_component_type);
    const std::size_t element_size = component_size * accessor_type_component_count(accessor.m_type);
    const std::size_t stride = buffer_view.m_byte_stride == 0 ? element_size : buffer_view.m_byte_stride;
    if (stride < element_size) {
        throw EngineError("glTF accessor byte stride is smaller than one element.");
    }
    const std::size_t start = buffer_view.m_byte_offset + accessor.m_byte_offset;
    const std::size_t byte_count = accessor.m_count == 0 ? 0 : stride * (accessor.m_count - 1U) + element_size;
    if (start > buffer.m_bytes.size() || byte_count > buffer.m_bytes.size() - start) {
        throw EngineError("glTF accessor data range is outside its buffer.");
    }

    return GltfAccessorDataView{
        std::span<const std::byte>(buffer.m_bytes.data() + start, byte_count), stride, element_size};
}

} // namespace ofg
