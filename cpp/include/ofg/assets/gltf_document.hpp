// OFG-owned glTF parse summary and accessor data views.
//
// This header intentionally exposes no tinygltf types. The import pipeline uses
// it as the narrow boundary between source-format parsing and OFG model-resource
// construction.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ofg {

struct AssetFile {
    std::string m_path;
    std::vector<std::byte> m_bytes;
};

class GltfResourceProvider {
public:
    GltfResourceProvider(const GltfResourceProvider&) = delete;
    GltfResourceProvider& operator=(const GltfResourceProvider&) = delete;
    GltfResourceProvider(GltfResourceProvider&&) = delete;
    GltfResourceProvider& operator=(GltfResourceProvider&&) = delete;
    virtual ~GltfResourceProvider() = default;

    // Loads one URI referenced by the primary glTF document.
    [[nodiscard]] virtual std::optional<AssetFile> load_relative(std::string_view uri) = 0;

protected:
    GltfResourceProvider() = default;
};

class FilesystemGltfResourceProvider : public GltfResourceProvider {
public:
    // Resolves referenced files relative to base_directory.
    explicit FilesystemGltfResourceProvider(std::filesystem::path base_directory);

    // Loads one sibling resource from the configured base directory.
    [[nodiscard]] std::optional<AssetFile> load_relative(std::string_view uri) override;

private:
    std::filesystem::path m_base_directory;
};

struct GltfBuffer {
    std::string m_name;
    std::vector<std::byte> m_bytes;
};

struct GltfBufferView {
    std::string m_name;
    std::int32_t m_buffer_index{-1};
    std::size_t m_byte_offset{0};
    std::size_t m_byte_length{0};
    std::size_t m_byte_stride{0};
};

struct GltfAccessor {
    std::string m_name;
    std::int32_t m_buffer_view_index{-1};
    std::size_t m_byte_offset{0};
    std::size_t m_count{0};
    std::int32_t m_component_type{0};
    std::int32_t m_type{0};
    bool m_normalized{false};
    std::size_t m_sparse_count{0};
};

struct GltfImage {
    std::string m_name;
    std::string m_uri;
    std::string m_mime_type;
    std::int32_t m_buffer_view_index{-1};
    std::int32_t m_width{0};
    std::int32_t m_height{0};
    std::int32_t m_component_count{0};
    std::int32_t m_bits_per_channel{0};
    std::int32_t m_pixel_type{0};
    std::vector<std::byte> m_bytes;
};

struct GltfTexture {
    std::string m_name;
    std::int32_t m_source_image_index{-1};
};

struct GltfMaterial {
    std::string m_name;
    std::array<double, 4> m_base_color_factor{1.0, 1.0, 1.0, 1.0};
    double m_metallic_factor{1.0};
    double m_roughness_factor{1.0};
    double m_normal_scale{1.0};
    std::int32_t m_base_color_texture_index{-1};
    std::int32_t m_metallic_roughness_texture_index{-1};
    std::int32_t m_normal_texture_index{-1};
    std::int32_t m_occlusion_texture_index{-1};
    std::int32_t m_emissive_texture_index{-1};
};

struct GltfAttribute {
    std::string m_semantic;
    std::int32_t m_accessor_index{-1};
};

struct GltfPrimitive {
    std::int32_t m_mode{-1};
    std::int32_t m_material_index{-1};
    std::int32_t m_indices_accessor_index{-1};
    std::size_t m_morph_target_count{0};
    std::vector<GltfAttribute> m_attributes;
};

struct GltfMesh {
    std::string m_name;
    std::vector<GltfPrimitive> m_primitives;
};

struct GltfNode {
    std::string m_name;
    std::int32_t m_mesh_index{-1};
    std::int32_t m_skin_index{-1};
    std::vector<std::int32_t> m_child_node_indices;
    bool m_has_translation{false};
    std::array<double, 3> m_translation{};
    bool m_has_rotation{false};
    std::array<double, 4> m_rotation{0.0, 0.0, 0.0, 1.0};
    bool m_has_scale{false};
    std::array<double, 3> m_scale{1.0, 1.0, 1.0};
    bool m_has_matrix{false};
    std::array<double, 16> m_matrix{};
};

struct GltfSkin {
    std::string m_name;
    std::int32_t m_skeleton_node_index{-1};
    std::int32_t m_inverse_bind_matrices_accessor_index{-1};
    std::vector<std::int32_t> m_joint_node_indices;
};

struct GltfAnimationSampler {
    std::int32_t m_input_accessor_index{-1};
    std::int32_t m_output_accessor_index{-1};
    std::string m_interpolation;
};

struct GltfAnimationChannel {
    std::int32_t m_sampler_index{-1};
    std::int32_t m_target_node_index{-1};
    std::string m_target_path;
};

struct GltfAnimation {
    std::string m_name;
    std::vector<GltfAnimationSampler> m_samplers;
    std::vector<GltfAnimationChannel> m_channels;
};

struct GltfAccessorDataView {
    std::span<const std::byte> m_data;
    std::size_t m_stride{0};
    std::size_t m_element_size{0};
};

class GltfDocument {
public:
    // Returns the caller-supplied document label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns whether the source bytes were a GLB binary container.
    [[nodiscard]] bool is_binary() const noexcept;
    // Returns the default scene index, or -1 when the file has no default.
    [[nodiscard]] std::int32_t default_scene_index() const noexcept;
    // Returns warnings reported by tinygltf while parsing.
    [[nodiscard]] const std::string& warnings() const noexcept;

    [[nodiscard]] std::span<const GltfBuffer> buffers() const noexcept;
    [[nodiscard]] std::span<const GltfBufferView> buffer_views() const noexcept;
    [[nodiscard]] std::span<const GltfAccessor> accessors() const noexcept;
    [[nodiscard]] std::span<const GltfImage> images() const noexcept;
    [[nodiscard]] std::span<const GltfTexture> textures() const noexcept;
    [[nodiscard]] std::span<const GltfMaterial> materials() const noexcept;
    [[nodiscard]] std::span<const GltfMesh> meshes() const noexcept;
    [[nodiscard]] std::span<const GltfNode> nodes() const noexcept;
    [[nodiscard]] std::span<const GltfSkin> skins() const noexcept;
    [[nodiscard]] std::span<const GltfAnimation> animations() const noexcept;
    [[nodiscard]] std::span<const std::string> extensions_used() const noexcept;
    [[nodiscard]] std::span<const std::string> extensions_required() const noexcept;

    [[nodiscard]] std::size_t scene_count() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t mesh_count() const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] std::size_t skin_count() const noexcept;
    [[nodiscard]] std::size_t animation_count() const noexcept;

    // Resolves one non-sparse accessor into bytes, stride, and element size.
    [[nodiscard]] GltfAccessorDataView accessor_data(std::size_t accessor_index) const;

private:
    friend GltfDocument load_gltf_document(
        std::string label, std::span<const std::byte> primary_bytes, GltfResourceProvider& resources);

    std::string m_label;
    bool m_is_binary{false};
    std::int32_t m_default_scene_index{-1};
    std::string m_warnings;
    std::vector<GltfBuffer> m_buffers;
    std::vector<GltfBufferView> m_buffer_views;
    std::vector<GltfAccessor> m_accessors;
    std::vector<GltfImage> m_images;
    std::vector<GltfTexture> m_textures;
    std::vector<GltfMaterial> m_materials;
    std::vector<GltfMesh> m_meshes;
    std::vector<GltfNode> m_nodes;
    std::vector<GltfSkin> m_skins;
    std::vector<GltfAnimation> m_animations;
    std::vector<std::string> m_extensions_used;
    std::vector<std::string> m_extensions_required;
    std::size_t m_scene_count{0};
    std::size_t m_node_count{0};
    std::size_t m_mesh_count{0};
    std::size_t m_material_count{0};
    std::size_t m_skin_count{0};
    std::size_t m_animation_count{0};
};

// Parses glTF JSON or GLB bytes into an OFG-owned document summary.
[[nodiscard]] GltfDocument load_gltf_document(
    std::string label, std::span<const std::byte> primary_bytes, GltfResourceProvider& resources);

// Loads and parses a native filesystem glTF/GLB file and sibling resources.
[[nodiscard]] GltfDocument load_gltf_document_from_path(const std::filesystem::path& path);

} // namespace ofg
