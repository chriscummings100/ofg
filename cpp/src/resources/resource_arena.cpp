// Stable resource owner for the first OFG renderer slice.
#include "ofg/resources/resource_arena.hpp"

#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <memory>
#include <span>
#include <utility>

namespace ofg {

// Creates an empty stable resource owner.
ResourceArena::ResourceArena() = default;

// Moves owned resources without changing pointed-to resource lifetimes.
ResourceArena::ResourceArena(ResourceArena&&) noexcept = default;

// Releases current resources, then moves ownership from another arena.
ResourceArena& ResourceArena::operator=(ResourceArena&&) noexcept = default;

// Releases all owned resources.
ResourceArena::~ResourceArena() = default;

// Adds a texture and returns its stable reference.
Texture& ResourceArena::add_texture(Texture texture) {
    m_textures.push_back(std::make_unique<Texture>(std::move(texture)));
    return *m_textures.back();
}

// Adds a shader and returns its stable reference.
Shader& ResourceArena::add_shader(Shader shader) {
    m_shaders.push_back(std::make_unique<Shader>(std::move(shader)));
    return *m_shaders.back();
}

// Adds a material and returns its stable reference.
Material& ResourceArena::add_material(Material material) {
    m_materials.push_back(std::make_unique<Material>(std::move(material)));
    return *m_materials.back();
}

// Adds a mesh and returns its stable reference.
Mesh& ResourceArena::add_mesh(Mesh mesh) {
    m_meshes.push_back(std::make_unique<Mesh>(std::move(mesh)));
    return *m_meshes.back();
}

// Clears all resources in reverse dependency-friendly order.
void ResourceArena::clear() {
    m_meshes.clear();
    m_materials.clear();
    m_shaders.clear();
    m_textures.clear();
}

// Returns owned textures for diagnostics and tests.
std::span<const std::unique_ptr<Texture>> ResourceArena::textures() const noexcept {
    return m_textures;
}

// Returns owned shaders for diagnostics and tests.
std::span<const std::unique_ptr<Shader>> ResourceArena::shaders() const noexcept {
    return m_shaders;
}

// Returns owned materials for diagnostics and tests.
std::span<const std::unique_ptr<Material>> ResourceArena::materials() const noexcept {
    return m_materials;
}

// Returns owned meshes for diagnostics and tests.
std::span<const std::unique_ptr<Mesh>> ResourceArena::meshes() const noexcept {
    return m_meshes;
}

} // namespace ofg
