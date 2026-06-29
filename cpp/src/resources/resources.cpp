// Static resource-system facade for high-level OFG assets.
//
// This file owns the private singleton behind the public Resources API. The
// instance owns stable resource vectors directly so Game can orchestrate
// resource startup and teardown without a separate arena owner.
#include "ofg/resources/resources.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <memory>
#include <span>
#include <string>
#include <utility>

namespace ofg {

std::unique_ptr<Resources> Resources::s_resources;

// Converts a Resources lifecycle state into a stable diagnostic string.
const char* resources_lifecycle_state_name(ResourcesLifecycleState state) noexcept {
    switch (state) {
    case ResourcesLifecycleState::Uninitialized:
        return "uninitialized";
    case ResourcesLifecycleState::Created:
        return "created";
    case ResourcesLifecycleState::Preparing:
        return "preparing";
    case ResourcesLifecycleState::Ready:
        return "ready";
    case ResourcesLifecycleState::Releasing:
        return "releasing";
    case ResourcesLifecycleState::Released:
        return "released";
    case ResourcesLifecycleState::Failed:
        return "failed";
    }
    return "unknown";
}

// Stores the borrowed GPU context and stable resource storage.
Resources::Resources(GpuContext gpu) : m_gpu(std::move(gpu)) {}

// Releases owned resources before the borrowed device goes away.
Resources::~Resources() = default;

// Creates the resource singleton for one borrowed WebGPU device lifetime.
void Resources::create(GpuContext gpu) {
    if (s_resources != nullptr) {
        throw EngineError("Resources::create cannot be called while a Resources singleton is live.");
    }
    if (gpu.m_device == nullptr || gpu.m_queue == nullptr) {
        throw EngineError("Resources requires a WebGPU device and queue.");
    }

    s_resources = std::unique_ptr<Resources>(new Resources(std::move(gpu)));
    s_resources->set_state(ResourcesLifecycleState::Created);
}

// Advances resource-system preparation and reports whether it is ready.
bool Resources::prepare() {
    return require_resources("Resources::prepare").prepare_impl();
}

// Advances resource teardown and reports whether all resources are released.
bool Resources::release() {
    if (s_resources == nullptr) {
        return true;
    }
    return s_resources->release_impl();
}

// Destroys the resource singleton after release has completed.
void Resources::destroy() noexcept {
    s_resources.reset();
}

// Returns the current resource-system lifecycle state.
ResourcesLifecycleState Resources::state() noexcept {
    if (s_resources != nullptr) {
        return s_resources->m_state;
    }
    return ResourcesLifecycleState::Uninitialized;
}

// Returns the active borrowed WebGPU context.
GpuContext Resources::gpu_context() {
    return require_resources("Resources::gpu_context").m_gpu;
}

// Allocates and stores a labeled texture resource.
Texture& Resources::create_texture(std::string label) {
    return require_resources("Resources::create_texture").create_texture_impl(std::move(label));
}

// Allocates and stores a labeled shader resource.
Shader& Resources::create_shader(std::string label) {
    return require_resources("Resources::create_shader").create_shader_impl(std::move(label));
}

// Allocates and stores a labeled material resource.
Material& Resources::create_material(std::string label) {
    return require_resources("Resources::create_material").create_material_impl(std::move(label));
}

// Allocates and stores a labeled mesh resource.
Mesh& Resources::create_mesh(std::string label) {
    return require_resources("Resources::create_mesh").create_mesh_impl(std::move(label));
}

// Returns owned textures for diagnostics and tests.
std::span<const std::unique_ptr<Texture>> Resources::textures() {
    return require_resources("Resources::textures").m_textures;
}

// Returns owned shaders for diagnostics and tests.
std::span<const std::unique_ptr<Shader>> Resources::shaders() {
    return require_resources("Resources::shaders").m_shaders;
}

// Returns owned materials for diagnostics and tests.
std::span<const std::unique_ptr<Material>> Resources::materials() {
    return require_resources("Resources::materials").m_materials;
}

// Returns owned meshes for diagnostics and tests.
std::span<const std::unique_ptr<Mesh>> Resources::meshes() {
    return require_resources("Resources::meshes").m_meshes;
}

// Advances the resource-system preparation state machine.
bool Resources::prepare_impl() {
    switch (m_state) {
    case ResourcesLifecycleState::Ready:
        return true;
    case ResourcesLifecycleState::Created:
        set_state(ResourcesLifecycleState::Preparing);
        [[fallthrough]];
    case ResourcesLifecycleState::Preparing:
        set_state(ResourcesLifecycleState::Ready);
        return true;
    case ResourcesLifecycleState::Failed:
        throw EngineError("Resources::prepare cannot continue while Resources is failed.");
    case ResourcesLifecycleState::Releasing:
    case ResourcesLifecycleState::Released:
        throw EngineError("Resources::prepare cannot run after Resources release has started.");
    case ResourcesLifecycleState::Uninitialized:
        throw EngineError("Resources::prepare requires Resources::create first.");
    }
    throw EngineError("Resources::prepare cannot run in an unknown lifecycle state.");
}

// Advances the resource-system release state machine.
bool Resources::release_impl() {
    switch (m_state) {
    case ResourcesLifecycleState::Released:
        return true;
    case ResourcesLifecycleState::Created:
    case ResourcesLifecycleState::Preparing:
    case ResourcesLifecycleState::Ready:
    case ResourcesLifecycleState::Failed:
        set_state(ResourcesLifecycleState::Releasing);
        [[fallthrough]];
    case ResourcesLifecycleState::Releasing:
        clear_resources();
        m_gpu = GpuContext{};
        set_state(ResourcesLifecycleState::Released);
        return true;
    case ResourcesLifecycleState::Uninitialized:
        return true;
    }
    throw EngineError("Resources::release cannot run in an unknown lifecycle state.");
}

// Allocates and stores a labeled texture resource.
Texture& Resources::create_texture_impl(std::string label) {
    require_live_for_create("Resources::create_texture");
    return add_texture(Texture(m_gpu, std::move(label)));
}

// Allocates and stores a labeled shader resource.
Shader& Resources::create_shader_impl(std::string label) {
    require_live_for_create("Resources::create_shader");
    return add_shader(Shader(m_gpu, std::move(label)));
}

// Allocates and stores a labeled material resource.
Material& Resources::create_material_impl(std::string label) {
    require_live_for_create("Resources::create_material");
    return add_material(Material(m_gpu, std::move(label)));
}

// Allocates and stores a labeled mesh resource.
Mesh& Resources::create_mesh_impl(std::string label) {
    require_live_for_create("Resources::create_mesh");
    return add_mesh(Mesh(m_gpu, std::move(label)));
}

// Adds a texture and returns its stable reference.
Texture& Resources::add_texture(Texture texture) {
    m_textures.push_back(std::make_unique<Texture>(std::move(texture)));
    return *m_textures.back();
}

// Adds a shader and returns its stable reference.
Shader& Resources::add_shader(Shader shader) {
    m_shaders.push_back(std::make_unique<Shader>(std::move(shader)));
    return *m_shaders.back();
}

// Adds a material and returns its stable reference.
Material& Resources::add_material(Material material) {
    m_materials.push_back(std::make_unique<Material>(std::move(material)));
    return *m_materials.back();
}

// Adds a mesh and returns its stable reference.
Mesh& Resources::add_mesh(Mesh mesh) {
    m_meshes.push_back(std::make_unique<Mesh>(std::move(mesh)));
    return *m_meshes.back();
}

// Clears all resources in reverse dependency-friendly order.
void Resources::clear_resources() {
    m_meshes.clear();
    m_materials.clear();
    m_shaders.clear();
    m_textures.clear();
}

// Throws if resource allocation is no longer allowed.
void Resources::require_live_for_create(const char* operation) const {
    if (m_state == ResourcesLifecycleState::Releasing || m_state == ResourcesLifecycleState::Released ||
        m_state == ResourcesLifecycleState::Failed) {
        throw EngineError(std::string(operation) + " requires a live Resources singleton before release.");
    }
}

// Updates this instance lifecycle state.
void Resources::set_state(ResourcesLifecycleState state) noexcept {
    m_state = state;
}

// Returns the live singleton or throws a clear lifecycle error.
Resources& Resources::require_resources(const char* operation) {
    if (s_resources == nullptr) {
        throw EngineError(std::string(operation) + " requires Resources::create first.");
    }
    return *s_resources;
}

} // namespace ofg
