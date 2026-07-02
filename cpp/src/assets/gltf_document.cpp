// glTF/GLB parsing through tinygltf with an OFG-owned public representation.
#include "ofg/assets/gltf_document.hpp"

#include "ofg/core/engine_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tiny_gltf.h"

namespace ofg {
namespace {

constexpr std::uint32_t _glb_magic = 0x46546C67U;

struct ProviderFsState {
    GltfResourceProvider* m_provider{nullptr};
    std::unordered_map<std::string, AssetFile> m_cache;
};

// Converts bytes from an unsigned-char vector into std::byte storage.
std::vector<std::byte> to_bytes(const std::vector<unsigned char>& bytes) {
    std::vector<std::byte> result;
    result.reserve(bytes.size());
    for (const unsigned char value : bytes) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

// Converts caller bytes into the representation tinygltf expects.
std::vector<unsigned char> to_unsigned_bytes(std::span<const std::byte> bytes) {
    std::vector<unsigned char> result;
    result.reserve(bytes.size());
    for (const std::byte value : bytes) {
        result.push_back(static_cast<unsigned char>(value));
    }
    return result;
}

// Converts one std::byte vector into tinygltf callback output bytes.
std::vector<unsigned char> to_unsigned_bytes(const std::vector<std::byte>& bytes) {
    std::vector<unsigned char> result;
    result.reserve(bytes.size());
    for (const std::byte value : bytes) {
        result.push_back(static_cast<unsigned char>(value));
    }
    return result;
}

// Reads one little-endian u32 from a byte span.
std::uint32_t read_le_u32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    if (bytes.size() < offset + 4U) {
        return 0;
    }
    const auto b0 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 0U]));
    const auto b1 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1U]));
    const auto b2 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2U]));
    const auto b3 = static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3U]));
    return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
}

// Returns whether the source bytes begin with the GLB magic value.
bool is_glb_bytes(std::span<const std::byte> bytes) noexcept {
    return read_le_u32(bytes, 0) == _glb_magic;
}

// Finds or loads a provider resource for a tinygltf filesystem callback.
const AssetFile* load_provider_resource(ProviderFsState& state, const std::string& path) {
    const auto found = state.m_cache.find(path);
    if (found != state.m_cache.end()) {
        return &found->second;
    }
    if (state.m_provider == nullptr) {
        return nullptr;
    }
    std::optional<AssetFile> loaded = state.m_provider->load_relative(path);
    if (!loaded.has_value()) {
        return nullptr;
    }
    const auto inserted = state.m_cache.emplace(path, std::move(*loaded));
    return &inserted.first->second;
}

// tinygltf callback: checks whether a provider-backed resource exists.
bool provider_file_exists(const std::string& abs_filename, void* user_data) {
    auto& state = *static_cast<ProviderFsState*>(user_data);
    return load_provider_resource(state, abs_filename) != nullptr;
}

// tinygltf callback: keeps paths in provider-relative form.
std::string provider_expand_file_path(const std::string& filepath, void*) {
    return filepath;
}

// tinygltf callback: reads a whole provider-backed resource.
bool provider_read_whole_file(
    std::vector<unsigned char>* out, std::string* err, const std::string& filepath, void* user_data) {
    auto& state = *static_cast<ProviderFsState*>(user_data);
    const AssetFile* loaded = load_provider_resource(state, filepath);
    if (loaded == nullptr) {
        if (err != nullptr) {
            *err += "OFG glTF resource provider could not load '" + filepath + "'.\n";
        }
        return false;
    }
    *out = to_unsigned_bytes(loaded->m_bytes);
    return true;
}

// tinygltf callback: OFG model loading is read-only.
bool provider_write_whole_file(
    std::string* err, const std::string& filepath, const std::vector<unsigned char>&, void*) {
    if (err != nullptr) {
        *err += "OFG glTF resource provider is read-only and cannot write '" + filepath + "'.\n";
    }
    return false;
}

// tinygltf callback: returns the size of a provider-backed resource.
bool provider_get_file_size(size_t* filesize_out, std::string* err, const std::string& abs_filename, void* user_data) {
    auto& state = *static_cast<ProviderFsState*>(user_data);
    const AssetFile* loaded = load_provider_resource(state, abs_filename);
    if (loaded == nullptr) {
        if (err != nullptr) {
            *err += "OFG glTF resource provider could not stat '" + abs_filename + "'.\n";
        }
        return false;
    }
    *filesize_out = loaded->m_bytes.size();
    return true;
}

// Throws a readable parse error after tinygltf reports failure.
void throw_parse_error(const std::string& label, const std::string& err, const std::string& warn) {
    std::string message = "Failed to parse glTF document '" + label + "'.";
    if (!err.empty()) {
        message += " Error: " + err;
    }
    if (!warn.empty()) {
        message += " Warning: " + warn;
    }
    throw EngineError(message);
}

// Converts a tinygltf image into OFG-owned data.
GltfImage convert_image(const tinygltf::Image& image) {
    GltfImage result;
    result.m_name = image.name;
    result.m_uri = image.uri;
    result.m_mime_type = image.mimeType;
    result.m_buffer_view_index = image.bufferView;
    result.m_width = image.width;
    result.m_height = image.height;
    result.m_component_count = image.component;
    result.m_bits_per_channel = image.bits;
    result.m_pixel_type = image.pixel_type;
    result.m_bytes = to_bytes(image.image);
    return result;
}

// Converts a tinygltf material into import/audit metadata.
GltfMaterial convert_material(const tinygltf::Material& material) {
    GltfMaterial result;
    result.m_name = material.name;
    if (material.pbrMetallicRoughness.baseColorFactor.size() == result.m_base_color_factor.size()) {
        for (std::size_t index = 0; index < result.m_base_color_factor.size(); ++index) {
            result.m_base_color_factor[index] = material.pbrMetallicRoughness.baseColorFactor[index];
        }
    }
    result.m_metallic_factor = material.pbrMetallicRoughness.metallicFactor;
    result.m_roughness_factor = material.pbrMetallicRoughness.roughnessFactor;
    result.m_normal_scale = material.normalTexture.scale;
    result.m_base_color_texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
    result.m_metallic_roughness_texture_index = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
    result.m_normal_texture_index = material.normalTexture.index;
    result.m_occlusion_texture_index = material.occlusionTexture.index;
    result.m_emissive_texture_index = material.emissiveTexture.index;
    return result;
}

// Converts a tinygltf primitive into source-index metadata.
GltfPrimitive convert_primitive(const tinygltf::Primitive& primitive) {
    GltfPrimitive result;
    result.m_mode = primitive.mode;
    result.m_material_index = primitive.material;
    result.m_indices_accessor_index = primitive.indices;
    result.m_morph_target_count = primitive.targets.size();
    result.m_attributes.reserve(primitive.attributes.size());
    for (const auto& [semantic, accessor_index] : primitive.attributes) {
        result.m_attributes.push_back(GltfAttribute{semantic, accessor_index});
    }
    return result;
}

// Converts a tinygltf mesh and its primitives.
GltfMesh convert_mesh(const tinygltf::Mesh& mesh) {
    GltfMesh result;
    result.m_name = mesh.name;
    result.m_primitives.reserve(mesh.primitives.size());
    for (const tinygltf::Primitive& primitive : mesh.primitives) {
        result.m_primitives.push_back(convert_primitive(primitive));
    }
    return result;
}

// Converts a tinygltf node without materializing transforms yet.
GltfNode convert_node(const tinygltf::Node& node) {
    GltfNode result;
    result.m_name = node.name;
    result.m_mesh_index = node.mesh;
    result.m_skin_index = node.skin;
    result.m_child_node_indices.reserve(node.children.size());
    for (const int child_index : node.children) {
        result.m_child_node_indices.push_back(child_index);
    }
    result.m_has_translation = node.translation.size() == 3;
    if (result.m_has_translation) {
        result.m_translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    result.m_has_rotation = node.rotation.size() == 4;
    if (result.m_has_rotation) {
        result.m_rotation = {node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
    }
    result.m_has_scale = node.scale.size() == 3;
    if (result.m_has_scale) {
        result.m_scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    result.m_has_matrix = node.matrix.size() == 16;
    if (result.m_has_matrix) {
        for (std::size_t index = 0; index < result.m_matrix.size(); ++index) {
            result.m_matrix[index] = node.matrix[index];
        }
    }
    return result;
}

// Converts a tinygltf skin into joint index metadata.
GltfSkin convert_skin(const tinygltf::Skin& skin) {
    GltfSkin result;
    result.m_name = skin.name;
    result.m_skeleton_node_index = skin.skeleton;
    result.m_inverse_bind_matrices_accessor_index = skin.inverseBindMatrices;
    result.m_joint_node_indices.reserve(skin.joints.size());
    for (const int joint_index : skin.joints) {
        result.m_joint_node_indices.push_back(joint_index);
    }
    return result;
}

// Converts tinygltf animation channels and samplers.
GltfAnimation convert_animation(const tinygltf::Animation& animation) {
    GltfAnimation result;
    result.m_name = animation.name;
    result.m_samplers.reserve(animation.samplers.size());
    for (const tinygltf::AnimationSampler& sampler : animation.samplers) {
        result.m_samplers.push_back(GltfAnimationSampler{sampler.input, sampler.output, sampler.interpolation});
    }
    result.m_channels.reserve(animation.channels.size());
    for (const tinygltf::AnimationChannel& channel : animation.channels) {
        result.m_channels.push_back(GltfAnimationChannel{channel.sampler, channel.target_node, channel.target_path});
    }
    return result;
}

// Reads a whole native file into bytes.
std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw EngineError("Could not open glTF file '" + path.string() + "'.");
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        throw EngineError("Could not determine glTF file size for '" + path.string() + "'.");
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    if (!file) {
        throw EngineError("Could not read glTF file '" + path.string() + "'.");
    }
    return bytes;
}

} // namespace

FilesystemGltfResourceProvider::FilesystemGltfResourceProvider(std::filesystem::path base_directory)
    : m_base_directory(std::move(base_directory)) {}

std::optional<AssetFile> FilesystemGltfResourceProvider::load_relative(std::string_view uri) {
    const std::filesystem::path uri_path{std::string(uri)};
    const std::filesystem::path path = uri_path.is_absolute() ? uri_path : m_base_directory / uri_path;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    AssetFile result;
    result.m_path = path.string();
    result.m_bytes.resize(static_cast<std::size_t>(size));
    if (!result.m_bytes.empty()) {
        file.read(reinterpret_cast<char*>(result.m_bytes.data()), size);
    }
    if (!file) {
        return std::nullopt;
    }
    return result;
}

GltfDocument load_gltf_document(
    std::string label, std::span<const std::byte> primary_bytes, GltfResourceProvider& resources) {
    if (label.empty()) {
        throw EngineError("glTF document label must not be empty.");
    }
    if (primary_bytes.empty()) {
        throw EngineError("glTF document '" + label + "' has no source bytes.");
    }

    ProviderFsState fs_state{&resources, {}};
    tinygltf::FsCallbacks callbacks;
    callbacks.FileExists = provider_file_exists;
    callbacks.ExpandFilePath = provider_expand_file_path;
    callbacks.ReadWholeFile = provider_read_whole_file;
    callbacks.WriteWholeFile = provider_write_whole_file;
    callbacks.GetFileSizeInBytes = provider_get_file_size;
    callbacks.user_data = &fs_state;

    tinygltf::TinyGLTF loader;
    std::string fs_error;
    if (!loader.SetFsCallbacks(std::move(callbacks), &fs_error)) {
        throw EngineError("Failed to configure glTF resource callbacks: " + fs_error);
    }
    tinygltf::Model model;
    std::string err;
    std::string warn;
    const std::vector<unsigned char> source = to_unsigned_bytes(primary_bytes);
    const bool is_binary = is_glb_bytes(primary_bytes);
    const bool parsed = is_binary ? loader.LoadBinaryFromMemory(
                                        &model, &err, &warn, source.data(), static_cast<unsigned int>(source.size()))
                                  : loader.LoadASCIIFromString(&model,
                                        &err,
                                        &warn,
                                        reinterpret_cast<const char*>(source.data()),
                                        static_cast<unsigned int>(source.size()),
                                        "");
    if (!parsed) {
        throw_parse_error(label, err, warn);
    }

    GltfDocument document;
    document.m_label = std::move(label);
    document.m_is_binary = is_binary;
    document.m_default_scene_index = model.defaultScene;
    document.m_warnings = std::move(warn);
    document.m_scene_count = model.scenes.size();
    document.m_node_count = model.nodes.size();
    document.m_mesh_count = model.meshes.size();
    document.m_material_count = model.materials.size();
    document.m_skin_count = model.skins.size();
    document.m_animation_count = model.animations.size();

    document.m_buffers.reserve(model.buffers.size());
    for (const tinygltf::Buffer& buffer : model.buffers) {
        document.m_buffers.push_back(GltfBuffer{buffer.name, to_bytes(buffer.data)});
    }

    document.m_buffer_views.reserve(model.bufferViews.size());
    for (const tinygltf::BufferView& buffer_view : model.bufferViews) {
        document.m_buffer_views.push_back(GltfBufferView{buffer_view.name,
            buffer_view.buffer,
            buffer_view.byteOffset,
            buffer_view.byteLength,
            buffer_view.byteStride});
    }

    document.m_accessors.reserve(model.accessors.size());
    for (const tinygltf::Accessor& accessor : model.accessors) {
        document.m_accessors.push_back(GltfAccessor{accessor.name,
            accessor.bufferView,
            accessor.byteOffset,
            accessor.count,
            accessor.componentType,
            accessor.type,
            accessor.normalized,
            accessor.sparse.isSparse ? accessor.sparse.count : 0U});
    }

    document.m_images.reserve(model.images.size());
    for (const tinygltf::Image& image : model.images) {
        document.m_images.push_back(convert_image(image));
    }

    document.m_textures.reserve(model.textures.size());
    for (const tinygltf::Texture& texture : model.textures) {
        document.m_textures.push_back(GltfTexture{texture.name, texture.source});
    }

    document.m_materials.reserve(model.materials.size());
    for (const tinygltf::Material& material : model.materials) {
        document.m_materials.push_back(convert_material(material));
    }

    document.m_meshes.reserve(model.meshes.size());
    for (const tinygltf::Mesh& mesh : model.meshes) {
        document.m_meshes.push_back(convert_mesh(mesh));
    }

    document.m_nodes.reserve(model.nodes.size());
    for (const tinygltf::Node& node : model.nodes) {
        document.m_nodes.push_back(convert_node(node));
    }

    document.m_skins.reserve(model.skins.size());
    for (const tinygltf::Skin& skin : model.skins) {
        document.m_skins.push_back(convert_skin(skin));
    }

    document.m_animations.reserve(model.animations.size());
    for (const tinygltf::Animation& animation : model.animations) {
        document.m_animations.push_back(convert_animation(animation));
    }

    document.m_extensions_used = model.extensionsUsed;
    document.m_extensions_required = model.extensionsRequired;

    return document;
}

GltfDocument load_gltf_document_from_path(const std::filesystem::path& path) {
    std::vector<std::byte> bytes = read_file_bytes(path);
    FilesystemGltfResourceProvider provider(path.parent_path());
    return load_gltf_document(path.filename().string(), bytes, provider);
}

} // namespace ofg
