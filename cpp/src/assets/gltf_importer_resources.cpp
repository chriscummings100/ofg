// Internal glTF importer resource cache and PBR material helpers.
#include "gltf_importer_resources.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/opaque_pbr_shader.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include "../render/shaders/opaque_uber.wgsl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
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

// Resolves a glTF relative URI against the root model URI directory.
std::string resolve_model_relative_uri(std::string_view root_uri, std::string_view relative_uri) {
    if (!relative_uri.empty() && relative_uri.front() == '/') {
        return std::string(relative_uri.substr(1));
    }
    const std::size_t slash = root_uri.find_last_of('/');
    if (slash == std::string_view::npos) {
        return std::string(relative_uri);
    }
    return std::string(root_uri.substr(0, slash + 1U)) + std::string(relative_uri);
}

// Copies a loaded blob view into an AssetFile for the glTF parser provider.
AssetFile asset_file_from_blob(const BlobView& blob) {
    AssetFile file;
    file.m_path = blob.m_uri;
    file.m_bytes.assign(blob.m_bytes.begin(), blob.m_bytes.end());
    return file;
}

// Appends one dependency id/URI pair if it has not already been seen.
void append_unique_dependency(
    std::vector<BlobLoadId>& ids, std::vector<std::string>& uris, BlobLoadId id, std::string uri) {
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        return;
    }
    ids.push_back(id);
    uris.push_back(std::move(uri));
}

// Produces a stable loader-owned generated sub-resource identity.
std::string generated_resource_key(std::string_view source_uri, std::string_view fragment) {
    if (source_uri.empty()) {
        return std::string(fragment);
    }
    return std::string(source_uri) + "#" + std::string(fragment);
}

class ResourcesGltfResourceProvider final : public GltfResourceProvider {
public:
    // Creates a provider rooted at one model source URI.
    explicit ResourcesGltfResourceProvider(std::string root_uri) : m_root_uri(std::move(root_uri)) {}

    // Resolves a relative glTF resource through Resources blob loads.
    std::optional<AssetFile> load_relative(std::string_view uri) override {
        const std::string resolved_uri = resolve_model_relative_uri(m_root_uri, uri);
        const BlobLoadId id = Resources::load_blob(resolved_uri);
        append_unique_dependency(m_dependency_blob_ids, m_dependency_uris, id, resolved_uri);
        const BlobView blob = Resources::blob(id);
        if (!blob.is_loaded()) {
            return std::nullopt;
        }
        return asset_file_from_blob(blob);
    }

    // Returns dependency ids requested during parsing.
    [[nodiscard]] const std::vector<BlobLoadId>& dependency_blob_ids() const noexcept {
        return m_dependency_blob_ids;
    }

    // Returns dependency URIs requested during parsing.
    [[nodiscard]] const std::vector<std::string>& dependency_uris() const noexcept {
        return m_dependency_uris;
    }

private:
    std::string m_root_uri;
    std::vector<BlobLoadId> m_dependency_blob_ids;
    std::vector<std::string> m_dependency_uris;
};

// Creates a material cache key for one glTF material index.
std::string material_cache_key(const std::string& source, std::int32_t material_index) {
    return source + "#material/" + std::to_string(material_index);
}

// Creates a texture cache key for one image and color-space interpretation.
std::string texture_cache_key(const std::string& source, std::int32_t image_index, TextureColorSpace color_space) {
    return source + "#texture/" + std::to_string(image_index) +
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

// Returns one loader-cached fallback texture by role.
Texture& fallback_texture(ModelResourceLoader& loader, FallbackTexture fallback) {
    switch (fallback) {
    case FallbackTexture::White:
        return loader.default_white_texture();
    case FallbackTexture::MetallicRoughness:
        return loader.default_metallic_roughness_texture();
    case FallbackTexture::Normal:
        return loader.default_normal_texture();
    }
    throw EngineError("Unknown glTF fallback texture role.");
}

// Returns a source texture, or a fallback texture when the glTF material omits it.
Texture& texture_for_index(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceLoader& loader,
    std::int32_t texture_index,
    TextureColorSpace color_space,
    FallbackTexture fallback) {
    if (texture_index < 0) {
        return fallback_texture(loader, fallback);
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
    return loader.get_or_create_texture(
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
    ModelResourceLoader& loader,
    const GltfMaterial* material) {
    PropertyBag properties;
    if (material == nullptr) {
        properties.set("base_color_factor", math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        properties.set("pbr_factors", math::vec4(1.0f, 1.0f, 1.0f, 0.0f));
        properties.set("base_color_texture", &loader.default_white_texture());
        properties.set("metallic_roughness_texture", &loader.default_metallic_roughness_texture());
        properties.set("normal_texture", &loader.default_normal_texture());
        return properties;
    }

    Texture& base_color = texture_for_index(document,
        options,
        loader,
        material->m_base_color_texture_index,
        TextureColorSpace::Srgb,
        FallbackTexture::White);
    Texture& metallic_roughness = texture_for_index(document,
        options,
        loader,
        material->m_metallic_roughness_texture_index,
        TextureColorSpace::Linear,
        FallbackTexture::MetallicRoughness);
    Texture& normal = texture_for_index(document,
        options,
        loader,
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
    ModelResourceLoader& loader,
    const GltfPrimitive& primitive) {
    const std::string source = source_key(document, options);
    if (primitive.m_material_index < 0) {
        return loader.get_or_create_material(source + "#material/default",
            options.m_model_name + " default material",
            material_properties(document, options, loader, nullptr));
    }
    if (static_cast<std::size_t>(primitive.m_material_index) >= document.materials().size()) {
        throw EngineError("glTF primitive references a material index outside the material table.");
    }
    const GltfMaterial& material = document.materials()[static_cast<std::size_t>(primitive.m_material_index)];
    const std::string label = material.m_name.empty()
                                  ? options.m_model_name + " material " + std::to_string(primitive.m_material_index)
                                  : options.m_model_name + " " + material.m_name;
    return loader.get_or_create_material(material_cache_key(source, primitive.m_material_index),
        label,
        material_properties(document, options, loader, &material));
}

} // namespace gltf_importer_detail

// Creates a loader usable as a temporary importer cache only.
ModelResourceLoader::ModelResourceLoader() = default;

// Creates a streaming loader for one normalized source model URI.
ModelResourceLoader::ModelResourceLoader(std::string source_uri, std::string model_name)
    : m_source_uri(std::move(source_uri)), m_model_name(std::move(model_name)) {
    if (m_source_uri.empty()) {
        throw EngineError("ModelResourceLoader requires a non-empty source URI.");
    }
    if (m_model_name.empty()) {
        throw EngineError("ModelResourceLoader requires a non-empty model name.");
    }
    m_root_blob_id = Resources::load_blob(m_source_uri);
    m_source_uri = Resources::blob(m_root_blob_id).m_uri;
}

// Releases temporary model loading and import cache state.
ModelResourceLoader::~ModelResourceLoader() = default;

// Advances streaming model loading by at most one major state.
void ModelResourceLoader::update(ModelResource& target) {
    if (m_root_blob_id == invalid_blob_load_id) {
        target.set_resource_failed("ModelResourceLoader has no root blob request for model loading.");
        return;
    }

    switch (target.state()) {
    case ResourceState::Queued:
        target.set_resource_state(ResourceState::LoadingRootBlob);
        return;
    case ResourceState::LoadingRootBlob: {
        const BlobView root_blob = Resources::blob(m_root_blob_id);
        if (root_blob.m_status == BlobLoadStatus::Failed) {
            target.set_resource_failed(
                "Model resource root blob '" + root_blob.m_uri + "' failed: " + root_blob.m_error);
            return;
        }
        if (root_blob.m_status == BlobLoadStatus::Loaded) {
            target.set_resource_state(ResourceState::DiscoveringDependencies);
        }
        return;
    }
    case ResourceState::DiscoveringDependencies: {
        const BlobView root_blob = Resources::blob(m_root_blob_id);
        if (!root_blob.is_loaded()) {
            target.set_resource_state(ResourceState::LoadingRootBlob);
            return;
        }

        ResourcesGltfResourceProvider provider(m_source_uri);
        try {
            m_pending_document = load_gltf_document(m_source_uri, root_blob.m_bytes, provider);
        } catch (const std::exception& error) {
            if (!provider.dependency_blob_ids().empty()) {
                bool waiting_for_dependencies = false;
                for (const BlobLoadId dependency_id : provider.dependency_blob_ids()) {
                    const BlobView dependency = Resources::blob(dependency_id);
                    if (dependency.m_status == BlobLoadStatus::Failed) {
                        target.set_resource_failed("Model resource '" + m_source_uri + "' dependency '" +
                                                   dependency.m_uri + "' failed: " + dependency.m_error);
                        return;
                    }
                    if (dependency.m_status != BlobLoadStatus::Loaded) {
                        waiting_for_dependencies = true;
                    }
                }
                if (waiting_for_dependencies) {
                    m_dependency_blob_ids = provider.dependency_blob_ids();
                    m_dependency_uris = provider.dependency_uris();
                    target.set_resource_state(ResourceState::WaitingForDependencies);
                    return;
                }
            }
            target.set_resource_failed(
                "Model resource '" + m_source_uri + "' failed during dependency discovery: " + error.what());
            return;
        }

        m_dependency_blob_ids = provider.dependency_blob_ids();
        m_dependency_uris = provider.dependency_uris();
        target.set_resource_state(ResourceState::Importing);
        return;
    }
    case ResourceState::WaitingForDependencies: {
        bool all_loaded = true;
        for (const BlobLoadId dependency_id : m_dependency_blob_ids) {
            const BlobView dependency = Resources::blob(dependency_id);
            if (dependency.m_status == BlobLoadStatus::Failed) {
                target.set_resource_failed("Model resource '" + m_source_uri + "' dependency '" + dependency.m_uri +
                                           "' failed: " + dependency.m_error);
                return;
            }
            if (dependency.m_status != BlobLoadStatus::Loaded) {
                all_loaded = false;
            }
        }
        if (all_loaded) {
            target.set_resource_state(ResourceState::DiscoveringDependencies);
        }
        return;
    }
    case ResourceState::Importing:
        if (!m_pending_document.has_value()) {
            target.set_resource_failed("Model resource '" + m_source_uri + "' has no parsed document to import.");
            return;
        }
        try {
            import_gltf_model_resource_into(
                *m_pending_document, GltfImportOptions{m_model_name, m_source_uri}, *this, target);
            m_pending_document.reset();
            target.clear_resource_error();
            target.set_resource_state(ResourceState::Loaded);
        } catch (const std::exception& error) {
            m_pending_document.reset();
            target.set_resource_failed("Model resource '" + m_source_uri + "' failed during import: " + error.what());
        }
        return;
    case ResourceState::Loaded:
    case ResourceState::Failed:
    case ResourceState::Unloaded:
        return;
    }
}

// Returns the number of cached mesh resources.
std::size_t ModelResourceLoader::mesh_count() const noexcept {
    return m_meshes.size();
}

// Returns the number of cached material resources.
std::size_t ModelResourceLoader::material_count() const noexcept {
    return m_materials.size();
}

// Returns the number of cached texture resources.
std::size_t ModelResourceLoader::texture_count() const noexcept {
    std::size_t count = m_textures.size();
    count += m_default_white_texture == nullptr ? 0U : 1U;
    count += m_default_metallic_roughness_texture == nullptr ? 0U : 1U;
    count += m_default_normal_texture == nullptr ? 0U : 1U;
    return count;
}

// Returns the shared shader used by imported PBR materials.
Shader& ModelResourceLoader::pbr_shader() {
    if (m_pbr_shader == nullptr) {
        Shader& shader = Resources::create_shader(generated_resource_key(m_source_uri, "shader/pbr"));
        shader.init_from_wgsl(
            render::shaders::opaque_uber_wgsl, opaque_pbr_shader_layout(), {PipelineDefinition{"model import PBR"}});
        m_pbr_shader = &shader;
    }
    return *m_pbr_shader;
}

// Returns a default white base-color texture for materials without a source texture.
Texture& ModelResourceLoader::default_white_texture() {
    if (m_default_white_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255)};
        Texture& texture = Resources::create_texture(generated_resource_key(m_source_uri, "texture/default-white"));
        texture.init_from_rgba8_pixels(1, 1, TextureColorSpace::Srgb, std::move(pixels), MipMapPolicy::None);
        m_default_white_texture = &texture;
    }
    return *m_default_white_texture;
}

// Returns a neutral metallic-roughness texture for materials without a source texture.
Texture& ModelResourceLoader::default_metallic_roughness_texture() {
    if (m_default_metallic_roughness_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(0),
            static_cast<std::byte>(255)};
        Texture& texture =
            Resources::create_texture(generated_resource_key(m_source_uri, "texture/default-metallic-roughness"));
        texture.init_from_rgba8_pixels(1, 1, TextureColorSpace::Linear, std::move(pixels), MipMapPolicy::None);
        m_default_metallic_roughness_texture = &texture;
    }
    return *m_default_metallic_roughness_texture;
}

// Returns a flat normal texture for materials without a source texture.
Texture& ModelResourceLoader::default_normal_texture() {
    if (m_default_normal_texture == nullptr) {
        std::vector<std::byte> pixels{static_cast<std::byte>(128),
            static_cast<std::byte>(128),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255)};
        Texture& texture = Resources::create_texture(generated_resource_key(m_source_uri, "texture/default-normal"));
        texture.init_from_rgba8_pixels(1, 1, TextureColorSpace::Linear, std::move(pixels), MipMapPolicy::None);
        m_default_normal_texture = &texture;
    }
    return *m_default_normal_texture;
}

// Returns or creates one material cache entry from validated PBR properties.
Material& ModelResourceLoader::get_or_create_material(
    std::string cache_key, std::string label, PropertyBag properties) {
    (void)label;
    const auto found = m_materials.find(cache_key);
    if (found != m_materials.end() && found->second != nullptr) {
        return *found->second;
    }

    Material& material = Resources::create_material(cache_key);
    material.init(pbr_shader(), std::move(properties));
    m_materials[std::move(cache_key)] = &material;
    return material;
}

// Returns or creates one texture cache entry.
Texture& ModelResourceLoader::get_or_create_texture(std::string cache_key,
    std::string label,
    std::uint32_t width,
    std::uint32_t height,
    TextureColorSpace color_space,
    std::vector<std::byte> pixels) {
    (void)label;
    const auto found = m_textures.find(cache_key);
    if (found != m_textures.end() && found->second != nullptr) {
        return *found->second;
    }

    Texture& texture = Resources::create_texture(cache_key);
    texture.init_from_rgba8_pixels(width, height, color_space, std::move(pixels), MipMapPolicy::GenerateCpuFullChain);
    m_textures[std::move(cache_key)] = &texture;
    return texture;
}

// Returns or creates one mesh cache entry.
Mesh& ModelResourceLoader::get_or_create_mesh(std::string cache_key,
    std::string label,
    std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<SubMesh> submeshes) {
    (void)label;
    const auto found = m_meshes.find(cache_key);
    if (found != m_meshes.end() && found->second != nullptr) {
        return *found->second;
    }

    Mesh& mesh = Resources::create_mesh(cache_key);
    mesh.init(std::move(vertices), std::move(indices), std::move(submeshes));
    m_meshes[std::move(cache_key)] = &mesh;
    return mesh;
}

} // namespace ofg
