// Internal glTF importer resource cache and PBR material helpers.
#include "gltf_importer_resources.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/opaque_pbr_shader.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include "../render/shaders/opaque_uber.wgsl.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr std::int32_t _gltf_component_unsigned_byte = 5121;

enum class FallbackTexture {
    White,
    MetallicRoughness,
    Normal,
};

// Creates a material cache key for one glTF material index.
std::string material_cache_key(const std::string& source, std::int32_t material_index) {
    return source + "#material/" + std::to_string(material_index);
}

// Creates a texture cache key for one image and color-space interpretation.
std::string texture_cache_key(const std::string& source, std::int32_t image_index, TextureColorSpace color_space) {
    return source + "#image/" + std::to_string(image_index) +
           (color_space == TextureColorSpace::Srgb ? "/srgb" : "/linear");
}

// Returns whether a material references any texture coordinates.
bool material_uses_textures(const GltfMaterial& material) noexcept {
    return material.m_base_color_texture_index >= 0 || material.m_metallic_roughness_texture_index >= 0 ||
           material.m_normal_texture_index >= 0 || material.m_occlusion_texture_index >= 0 ||
           material.m_emissive_texture_index >= 0;
}

// Converts a decoded glTF image into tightly packed RGBA8 pixels.
std::vector<std::byte> image_rgba8_pixels(const GltfImage& image) {
    if (image.m_width <= 0 || image.m_height <= 0 || image.m_bytes.empty()) {
        throw EngineError("glTF image '" + image.m_name + "' has no decoded pixel data.");
    }
    if (image.m_bits_per_channel != 8) {
        throw EngineError("glTF image '" + image.m_name + "' must use 8-bit channels for RGBA8 texture import.");
    }
    if (image.m_pixel_type != _gltf_component_unsigned_byte) {
        throw EngineError("glTF image '" + image.m_name + "' must use unsigned-byte pixel data.");
    }
    if (image.m_component_count < 1 || image.m_component_count > 4) {
        throw EngineError("glTF image '" + image.m_name + "' has an unsupported channel count.");
    }

    const std::size_t pixel_count = static_cast<std::size_t>(image.m_width) * static_cast<std::size_t>(image.m_height);
    const std::size_t expected_bytes = pixel_count * static_cast<std::size_t>(image.m_component_count);
    if (image.m_bytes.size() != expected_bytes) {
        throw EngineError("glTF image '" + image.m_name + "' decoded byte count does not match its dimensions.");
    }

    std::vector<std::byte> rgba(pixel_count * 4U);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const std::size_t source = pixel * static_cast<std::size_t>(image.m_component_count);
        const std::size_t target = pixel * 4U;
        const std::uint8_t r = std::to_integer<std::uint8_t>(image.m_bytes[source]);
        const std::uint8_t g =
            image.m_component_count >= 2 ? std::to_integer<std::uint8_t>(image.m_bytes[source + 1U]) : r;
        const std::uint8_t b =
            image.m_component_count >= 3 ? std::to_integer<std::uint8_t>(image.m_bytes[source + 2U]) : r;
        const std::uint8_t a =
            image.m_component_count >= 4 ? std::to_integer<std::uint8_t>(image.m_bytes[source + 3U]) : 255U;
        rgba[target] = static_cast<std::byte>(r);
        rgba[target + 1U] = static_cast<std::byte>(g);
        rgba[target + 2U] = static_cast<std::byte>(b);
        rgba[target + 3U] = static_cast<std::byte>(a);
    }
    return rgba;
}

// Returns one context-owned fallback texture by role.
Texture& fallback_texture(ModelResourceImportContext& context, FallbackTexture fallback) {
    switch (fallback) {
    case FallbackTexture::White:
        return context.default_white_texture();
    case FallbackTexture::MetallicRoughness:
        return context.default_metallic_roughness_texture();
    case FallbackTexture::Normal:
        return context.default_normal_texture();
    }
    throw EngineError("Unknown glTF fallback texture role.");
}

// Returns a source texture, or a fallback texture when the glTF material omits it.
Texture& texture_for_index(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceImportContext& context,
    std::int32_t texture_index,
    TextureColorSpace color_space,
    FallbackTexture fallback) {
    if (texture_index < 0) {
        return fallback_texture(context, fallback);
    }
    if (static_cast<std::size_t>(texture_index) >= document.textures().size()) {
        throw EngineError("glTF material references a texture index outside the texture table.");
    }
    const GltfTexture& texture = document.textures()[static_cast<std::size_t>(texture_index)];
    if (texture.m_source_image_index < 0 ||
        static_cast<std::size_t>(texture.m_source_image_index) >= document.images().size()) {
        throw EngineError("glTF texture references an image index outside the image table.");
    }

    const GltfImage& image = document.images()[static_cast<std::size_t>(texture.m_source_image_index)];
    const std::string label = image.m_name.empty() ? options.m_model_name + " image " + std::to_string(texture_index)
                                                   : options.m_model_name + " " + image.m_name;
    return context.get_or_create_texture(
        texture_cache_key(
            gltf_importer_detail::source_key(document, options), texture.m_source_image_index, color_space),
        label,
        static_cast<std::uint32_t>(image.m_width),
        static_cast<std::uint32_t>(image.m_height),
        color_space,
        image_rgba8_pixels(image));
}

// Builds the PBR material property bag for one glTF material.
PropertyBag material_properties(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceImportContext& context,
    const GltfMaterial* material) {
    PropertyBag properties;
    if (material == nullptr) {
        properties.set("base_color_factor", math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        properties.set("pbr_factors", math::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        properties.set("base_color_texture", &context.default_white_texture());
        properties.set("metallic_roughness_texture", &context.default_metallic_roughness_texture());
        properties.set("normal_texture", &context.default_normal_texture());
        return properties;
    }

    Texture& base_color = texture_for_index(document,
        options,
        context,
        material->m_base_color_texture_index,
        TextureColorSpace::Srgb,
        FallbackTexture::White);
    Texture& metallic_roughness = texture_for_index(document,
        options,
        context,
        material->m_metallic_roughness_texture_index,
        TextureColorSpace::Linear,
        FallbackTexture::MetallicRoughness);
    Texture& normal = texture_for_index(document,
        options,
        context,
        material->m_normal_texture_index,
        TextureColorSpace::Linear,
        FallbackTexture::Normal);

    properties.set("base_color_factor",
        math::vec4(static_cast<float>(material->m_base_color_factor[0]),
            static_cast<float>(material->m_base_color_factor[1]),
            static_cast<float>(material->m_base_color_factor[2]),
            static_cast<float>(material->m_base_color_factor[3])));
    properties.set("pbr_factors",
        math::vec4(static_cast<float>(material->m_metallic_factor),
            static_cast<float>(material->m_roughness_factor),
            static_cast<float>(material->m_normal_scale),
            material->m_normal_texture_index >= 0 ? 1.0f : 0.0f));
    properties.set("base_color_texture", &base_color);
    properties.set("metallic_roughness_texture", &metallic_roughness);
    properties.set("normal_texture", &normal);
    return properties;
}

} // namespace

namespace gltf_importer_detail {

// Returns a stable source key for cache entries.
std::string source_key(const GltfDocument& document, const GltfImportOptions& options) {
    return options.m_source_uri.empty() ? document.label() : options.m_source_uri;
}

// Returns whether a primitive material requires texture coordinates.
bool primitive_requires_uvs(const GltfDocument& document, const GltfPrimitive& primitive) {
    if (primitive.m_material_index < 0) {
        return false;
    }
    if (static_cast<std::size_t>(primitive.m_material_index) >= document.materials().size()) {
        throw EngineError("glTF primitive references a material index outside the material table.");
    }
    return material_uses_textures(document.materials()[static_cast<std::size_t>(primitive.m_material_index)]);
}

// Returns whether the primitive's material references a normal texture.
bool primitive_uses_normal_texture(const GltfDocument& document, const GltfPrimitive& primitive) {
    if (primitive.m_material_index < 0) {
        return false;
    }
    if (static_cast<std::size_t>(primitive.m_material_index) >= document.materials().size()) {
        throw EngineError("glTF primitive references a material index outside the material table.");
    }
    return document.materials()[static_cast<std::size_t>(primitive.m_material_index)].m_normal_texture_index >= 0;
}

// Returns a PBR material for a primitive, creating a cached material if needed.
Material& material_for_primitive(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceImportContext& context,
    const GltfPrimitive& primitive) {
    const std::string source = source_key(document, options);
    if (primitive.m_material_index < 0) {
        return context.get_or_create_material(source + "#material/default",
            options.m_model_name + " default material",
            material_properties(document, options, context, nullptr));
    }
    if (static_cast<std::size_t>(primitive.m_material_index) >= document.materials().size()) {
        throw EngineError("glTF primitive references a material index outside the material table.");
    }
    const GltfMaterial& material = document.materials()[static_cast<std::size_t>(primitive.m_material_index)];
    const std::string label = material.m_name.empty()
                                  ? options.m_model_name + " material " + std::to_string(primitive.m_material_index)
                                  : options.m_model_name + " " + material.m_name;
    return context.get_or_create_material(material_cache_key(source, primitive.m_material_index),
        label,
        material_properties(document, options, context, &material));
}

} // namespace gltf_importer_detail

// Creates a CPU-only or GPU-backed import context.
ModelResourceImportContext::ModelResourceImportContext(GpuContext gpu) : m_gpu(std::move(gpu)) {}

// Releases context-owned resources after model resources that point into them.
ModelResourceImportContext::~ModelResourceImportContext() = default;

// Returns the number of cached mesh resources.
std::size_t ModelResourceImportContext::mesh_count() const noexcept {
    return m_meshes.size();
}

// Returns the number of cached material resources.
std::size_t ModelResourceImportContext::material_count() const noexcept {
    return m_materials.size();
}

// Returns the number of cached texture resources.
std::size_t ModelResourceImportContext::texture_count() const noexcept {
    std::size_t count = m_textures.size();
    count += m_default_white_texture == nullptr ? 0U : 1U;
    count += m_default_metallic_roughness_texture == nullptr ? 0U : 1U;
    count += m_default_normal_texture == nullptr ? 0U : 1U;
    return count;
}

// Returns the shared shader used by imported PBR materials.
Shader& ModelResourceImportContext::pbr_shader() {
    if (m_pbr_shader == nullptr) {
        m_pbr_shader = std::make_unique<Shader>(m_gpu, "OFG model import PBR shader");
        m_pbr_shader->init_from_wgsl(
            render::shaders::opaque_uber_wgsl, opaque_pbr_shader_layout(), {PipelineDefinition{"model import PBR"}});
    }
    return *m_pbr_shader;
}

// Returns a default white base-color texture for materials without a source texture.
Texture& ModelResourceImportContext::default_white_texture() {
    if (m_default_white_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255)};
        m_default_white_texture = std::make_unique<Texture>(m_gpu, "OFG model import white texture");
        m_default_white_texture->init_from_rgba8_pixels(
            1, 1, TextureColorSpace::Srgb, std::move(pixels), MipMapPolicy::None);
    }
    return *m_default_white_texture;
}

// Returns a neutral metallic-roughness texture for materials without a source texture.
Texture& ModelResourceImportContext::default_metallic_roughness_texture() {
    if (m_default_metallic_roughness_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(0),
            static_cast<std::byte>(255)};
        m_default_metallic_roughness_texture =
            std::make_unique<Texture>(m_gpu, "OFG model import neutral metallic-roughness texture");
        m_default_metallic_roughness_texture->init_from_rgba8_pixels(
            1, 1, TextureColorSpace::Linear, std::move(pixels), MipMapPolicy::None);
    }
    return *m_default_metallic_roughness_texture;
}

// Returns a flat normal texture for materials without a source texture.
Texture& ModelResourceImportContext::default_normal_texture() {
    if (m_default_normal_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(128),
            static_cast<std::byte>(128),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255)};
        m_default_normal_texture = std::make_unique<Texture>(m_gpu, "OFG model import flat normal texture");
        m_default_normal_texture->init_from_rgba8_pixels(
            1, 1, TextureColorSpace::Linear, std::move(pixels), MipMapPolicy::None);
    }
    return *m_default_normal_texture;
}

// Returns or creates one material cache entry from validated PBR properties.
Material& ModelResourceImportContext::get_or_create_material(
    std::string cache_key, std::string label, PropertyBag properties) {
    const auto found = m_materials.find(cache_key);
    if (found != m_materials.end()) {
        return *found->second;
    }

    auto material = std::make_unique<Material>(m_gpu, std::move(label));
    material->init(pbr_shader(), std::move(properties));
    const auto inserted = m_materials.emplace(std::move(cache_key), std::move(material));
    return *inserted.first->second;
}

// Returns or creates one texture cache entry.
Texture& ModelResourceImportContext::get_or_create_texture(std::string cache_key,
    std::string label,
    std::uint32_t width,
    std::uint32_t height,
    TextureColorSpace color_space,
    std::vector<std::byte> pixels) {
    const auto found = m_textures.find(cache_key);
    if (found != m_textures.end()) {
        return *found->second;
    }

    auto texture = std::make_unique<Texture>(m_gpu, std::move(label));
    texture->init_from_rgba8_pixels(width, height, color_space, std::move(pixels), MipMapPolicy::GenerateCpuFullChain);
    const auto inserted = m_textures.emplace(std::move(cache_key), std::move(texture));
    return *inserted.first->second;
}

// Returns or creates one mesh cache entry.
Mesh& ModelResourceImportContext::get_or_create_mesh(std::string cache_key,
    std::string label,
    std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<SubMesh> submeshes) {
    const auto found = m_meshes.find(cache_key);
    if (found != m_meshes.end()) {
        return *found->second;
    }

    auto mesh = std::make_unique<Mesh>(m_gpu, std::move(label));
    mesh->init(std::move(vertices), std::move(indices), std::move(submeshes));
    const auto inserted = m_meshes.emplace(std::move(cache_key), std::move(mesh));
    return *inserted.first->second;
}

} // namespace ofg
