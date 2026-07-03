// glTF-to-ModelResource importer boundary.
//
// This module converts OFG-owned GltfDocument data into reusable ModelResource
// templates and shared CPU/GPU resource objects. tinygltf remains private to the
// parser layer.
#pragma once

#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ofg {

struct GltfImportOptions;

class ModelResourceImportContext {
public:
    // Creates a CPU-only or GPU-backed import context.
    explicit ModelResourceImportContext(GpuContext gpu = {});

    ModelResourceImportContext(const ModelResourceImportContext&) = delete;
    ModelResourceImportContext& operator=(const ModelResourceImportContext&) = delete;
    ModelResourceImportContext(ModelResourceImportContext&&) = delete;
    ModelResourceImportContext& operator=(ModelResourceImportContext&&) = delete;
    ~ModelResourceImportContext();

    // Returns the number of cached mesh resources.
    [[nodiscard]] std::size_t mesh_count() const noexcept;
    // Returns the number of cached material resources.
    [[nodiscard]] std::size_t material_count() const noexcept;
    // Returns the number of cached texture resources.
    [[nodiscard]] std::size_t texture_count() const noexcept;
    // Returns or creates one material cache entry from validated PBR properties.
    [[nodiscard]] Material& get_or_create_material(std::string cache_key, std::string label, PropertyBag properties);
    // Returns or creates one texture cache entry.
    [[nodiscard]] Texture& get_or_create_texture(std::string cache_key,
        std::string label,
        std::uint32_t width,
        std::uint32_t height,
        TextureColorSpace color_space,
        std::vector<std::byte> pixels);
    // Returns the shared shader used by imported PBR materials.
    [[nodiscard]] Shader& pbr_shader();
    // Returns a default white base-color texture for materials without a source texture.
    [[nodiscard]] Texture& default_white_texture();
    // Returns a neutral metallic-roughness texture for materials without a source texture.
    [[nodiscard]] Texture& default_metallic_roughness_texture();
    // Returns a flat normal texture for materials without a source texture.
    [[nodiscard]] Texture& default_normal_texture();
    // Returns or creates one mesh cache entry.
    [[nodiscard]] Mesh& get_or_create_mesh(std::string cache_key,
        std::string label,
        std::vector<MeshVertex> vertices,
        std::vector<std::uint32_t> indices,
        std::vector<SubMesh> submeshes);

private:
    friend std::unique_ptr<ModelResource> import_gltf_model_resource(
        const GltfDocument& document, const GltfImportOptions& options, ModelResourceImportContext& context);

    GpuContext m_gpu;
    std::unique_ptr<Shader> m_pbr_shader;
    std::unique_ptr<Texture> m_default_white_texture;
    std::unique_ptr<Texture> m_default_metallic_roughness_texture;
    std::unique_ptr<Texture> m_default_normal_texture;
    std::unordered_map<std::string, std::unique_ptr<Material>> m_materials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
    std::unordered_map<std::string, std::unique_ptr<Mesh>> m_meshes;
};

struct GltfImportOptions {
    std::string m_model_name;
    std::string m_source_uri;
};

// Converts a parsed glTF document into a reusable model resource.
[[nodiscard]] std::unique_ptr<ModelResource> import_gltf_model_resource(
    const GltfDocument& document, const GltfImportOptions& options, ModelResourceImportContext& context);

// Converts a parsed glTF document into an existing reusable model resource.
void import_gltf_model_resource_into(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceImportContext& context,
    ModelResource& resource);

} // namespace ofg
