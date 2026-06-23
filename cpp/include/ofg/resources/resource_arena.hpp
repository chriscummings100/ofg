// Stable resource owner for the first OFG renderer slice.
//
// ResourceArena gives resources stable addresses and bulk teardown without
// introducing lookup, removal, async loading, or a large manager API.
#pragma once

#include <memory>
#include <span>
#include <vector>

namespace ofg {

class Material;
class Mesh;
class Shader;
class Texture;

class ResourceArena {
public:
    ResourceArena();
    ResourceArena(const ResourceArena&) = delete;
    ResourceArena& operator=(const ResourceArena&) = delete;
    ResourceArena(ResourceArena&&) noexcept;
    ResourceArena& operator=(ResourceArena&&) noexcept;
    ~ResourceArena();

    // Adds a texture and returns its stable reference.
    Texture& add_texture(Texture texture);
    // Adds a shader and returns its stable reference.
    Shader& add_shader(Shader shader);
    // Adds a material and returns its stable reference.
    Material& add_material(Material material);
    // Adds a mesh and returns its stable reference.
    Mesh& add_mesh(Mesh mesh);
    // Clears all resources in reverse dependency-friendly order.
    void clear();

    // Returns owned textures for diagnostics and tests.
    [[nodiscard]] std::span<const std::unique_ptr<Texture>> textures() const noexcept;
    // Returns owned shaders for diagnostics and tests.
    [[nodiscard]] std::span<const std::unique_ptr<Shader>> shaders() const noexcept;
    // Returns owned materials for diagnostics and tests.
    [[nodiscard]] std::span<const std::unique_ptr<Material>> materials() const noexcept;
    // Returns owned meshes for diagnostics and tests.
    [[nodiscard]] std::span<const std::unique_ptr<Mesh>> meshes() const noexcept;

private:
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<Shader>> m_shaders;
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<std::unique_ptr<Mesh>> m_meshes;
};

} // namespace ofg
