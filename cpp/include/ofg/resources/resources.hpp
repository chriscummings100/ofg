// Static resource-system facade for high-level OFG assets.
//
// Resources owns stable storage for texture, shader, material, and mesh assets
// for one WebGPU device lifetime. Public create_* methods allocate high-level
// resource objects; explicit resource methods fill them with CPU/GPU data.
#pragma once

#include "ofg/game/gpu_context.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ofg {

class Material;
class Mesh;
class Shader;
class Texture;

enum class ResourcesLifecycleState {
    Uninitialized,
    Created,
    Preparing,
    Ready,
    Releasing,
    Released,
    Failed,
};

// Converts a Resources lifecycle state into a stable diagnostic string.
[[nodiscard]] const char* resources_lifecycle_state_name(ResourcesLifecycleState state) noexcept;

class Resources {
public:
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) = delete;
    Resources& operator=(Resources&&) = delete;
    ~Resources();

    // Creates the resource singleton for one borrowed WebGPU device lifetime.
    static void create(GpuContext gpu);
    // Advances resource-system preparation and reports whether it is ready.
    [[nodiscard]] static bool prepare();
    // Advances resource teardown and reports whether all resources are released.
    [[nodiscard]] static bool release();
    // Destroys the resource singleton after release has completed.
    static void destroy() noexcept;
    // Returns the current resource-system lifecycle state.
    [[nodiscard]] static ResourcesLifecycleState state() noexcept;
    // Returns the active borrowed WebGPU context.
    [[nodiscard]] static GpuContext gpu_context();
    // Allocates and stores a labeled texture resource.
    [[nodiscard]] static Texture& create_texture(std::string label);
    // Allocates and stores a labeled shader resource.
    [[nodiscard]] static Shader& create_shader(std::string label);
    // Allocates and stores a labeled material resource.
    [[nodiscard]] static Material& create_material(std::string label);
    // Allocates and stores a labeled mesh resource.
    [[nodiscard]] static Mesh& create_mesh(std::string label);
    // Returns owned textures for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Texture>> textures();
    // Returns owned shaders for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Shader>> shaders();
    // Returns owned materials for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Material>> materials();
    // Returns owned meshes for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Mesh>> meshes();

private:
    // Stores the borrowed GPU context and stable resource storage.
    explicit Resources(GpuContext gpu);

    // Advances the resource-system preparation state machine.
    [[nodiscard]] bool prepare_impl();
    // Advances the resource-system release state machine.
    [[nodiscard]] bool release_impl();
    // Allocates and stores a labeled texture resource.
    [[nodiscard]] Texture& create_texture_impl(std::string label);
    // Allocates and stores a labeled shader resource.
    [[nodiscard]] Shader& create_shader_impl(std::string label);
    // Allocates and stores a labeled material resource.
    [[nodiscard]] Material& create_material_impl(std::string label);
    // Allocates and stores a labeled mesh resource.
    [[nodiscard]] Mesh& create_mesh_impl(std::string label);
    // Adds a texture and returns its stable reference.
    [[nodiscard]] Texture& add_texture(Texture texture);
    // Adds a shader and returns its stable reference.
    [[nodiscard]] Shader& add_shader(Shader shader);
    // Adds a material and returns its stable reference.
    [[nodiscard]] Material& add_material(Material material);
    // Adds a mesh and returns its stable reference.
    [[nodiscard]] Mesh& add_mesh(Mesh mesh);
    // Clears all resources in reverse dependency-friendly order.
    void clear_resources();
    // Throws if resource allocation is no longer allowed.
    void require_live_for_create(const char* operation) const;
    // Updates this instance lifecycle state.
    void set_state(ResourcesLifecycleState state) noexcept;

    // Returns the live singleton or throws a clear lifecycle error.
    [[nodiscard]] static Resources& require_resources(const char* operation);

    static std::unique_ptr<Resources> s_resources;

    GpuContext m_gpu;
    ResourcesLifecycleState m_state{ResourcesLifecycleState::Uninitialized};
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<Shader>> m_shaders;
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<std::unique_ptr<Mesh>> m_meshes;
};

} // namespace ofg
