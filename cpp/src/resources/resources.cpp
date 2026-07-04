// Static resource-system facade for high-level OFG assets.
//
// This file owns the private singleton behind the public Resources API. The
// instance owns stable resource vectors directly so Game can orchestrate
// resource startup and teardown without a separate arena owner.
#include "ofg/resources/resources.hpp"

#include "ofg/assets/model_resource.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resource.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace ofg {

std::unique_ptr<Resources> Resources::s_resources;

namespace {

// Normalizes host-visible asset URIs into relative, portable cache keys.
std::string normalize_blob_uri(std::string_view uri) {
    while (!uri.empty() && uri.front() == '/') {
        uri.remove_prefix(1);
    }
    if (uri.empty()) {
        throw EngineError("Resources::load_blob requires a non-empty relative asset URI.");
    }
    if (uri.find('\\') != std::string_view::npos) {
        throw EngineError("Blob asset URI '" + std::string(uri) + "' must use forward slashes.");
    }
    if (uri.find("..") != std::string_view::npos) {
        throw EngineError("Blob asset URI '" + std::string(uri) + "' cannot contain '..'.");
    }
    if (uri.find(':') != std::string_view::npos) {
        throw EngineError("Blob asset URI '" + std::string(uri) + "' must be relative, not a URL or drive path.");
    }
    if (uri.find('?') != std::string_view::npos || uri.find('#') != std::string_view::npos) {
        throw EngineError("Blob asset URI '" + std::string(uri) + "' cannot contain query strings or fragments.");
    }

    std::size_t segment_start = 0;
    while (segment_start <= uri.size()) {
        const std::size_t segment_end = uri.find('/', segment_start);
        const std::string_view segment = uri.substr(segment_start,
            segment_end == std::string_view::npos ? std::string_view::npos : segment_end - segment_start);
        if (segment.empty() || segment == ".") {
            throw EngineError("Blob asset URI '" + std::string(uri) +
                              "' contains an empty or current-directory path "
                              "segment.");
        }
        if (segment_end == std::string_view::npos) {
            break;
        }
        segment_start = segment_end + 1;
    }

    return std::string(uri);
}

// Removes a host-facing pending request once it leaves the queued state.
void remove_pending_blob_load(std::vector<PendingBlobLoad>& pending, BlobLoadId id) {
    std::erase_if(pending, [id](const PendingBlobLoad& request) { return request.m_id == id; });
}

// Returns a stable model name derived from the URI filename stem.
std::string model_name_from_uri(std::string_view uri) {
    const std::size_t slash = uri.find_last_of('/');
    const std::size_t name_start = slash == std::string_view::npos ? 0U : slash + 1U;
    std::string_view name = uri.substr(name_start);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot > 0U) {
        name = name.substr(0, dot);
    }
    if (name.empty()) {
        return "model";
    }
    return std::string(name);
}

// Builds the first model-resource cache key from URI and import-affecting options.
std::string model_resource_cache_key(std::string_view uri, std::string_view model_name) {
    return std::string(uri) + "\n" + std::string(model_name);
}

} // namespace

// Converts a blob load status into a stable diagnostic string.
const char* blob_load_status_name(BlobLoadStatus status) noexcept {
    switch (status) {
    case BlobLoadStatus::Missing:
        return "missing";
    case BlobLoadStatus::Queued:
        return "queued";
    case BlobLoadStatus::Loading:
        return "loading";
    case BlobLoadStatus::Loaded:
        return "loaded";
    case BlobLoadStatus::Failed:
        return "failed";
    }
    return "unknown";
}

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

// Requests a binary asset blob by normalized relative URI, returning a stable request id.
BlobLoadId Resources::load_blob(std::string_view uri) {
    return require_resources("Resources::load_blob").load_blob_impl(uri);
}

// Returns the current state and bytes for an existing blob request id.
BlobView Resources::blob(BlobLoadId id) {
    return require_resources("Resources::blob").blob_impl(id);
}

// Returns the current state and bytes for a normalized relative URI, or Missing if unknown.
BlobView Resources::blob_by_uri(std::string_view uri) {
    return require_resources("Resources::blob_by_uri").blob_by_uri_impl(uri);
}

// Returns queued blob requests that still need to be serviced by the host.
std::span<const PendingBlobLoad> Resources::pending_blob_loads() {
    return require_resources("Resources::pending_blob_loads").m_pending_blob_loads;
}

// Marks a queued blob request as being actively loaded by the host.
void Resources::mark_blob_loading(BlobLoadId id) {
    require_resources("Resources::mark_blob_loading").mark_blob_loading_impl(id);
}

// Completes an active blob request with bytes supplied by the host.
void Resources::complete_blob_load(BlobLoadId id, std::span<const std::byte> bytes) {
    require_resources("Resources::complete_blob_load").complete_blob_load_impl(id, bytes);
}

// Fails an active blob request with a host-supplied diagnostic message.
void Resources::fail_blob_load(BlobLoadId id, std::string message) {
    require_resources("Resources::fail_blob_load").fail_blob_load_impl(id, std::move(message));
}

// Requests a model resource by URI and returns its stable observable object.
Ptr<ModelResource> Resources::load_model_resource(std::string_view uri, ModelResourceLoadOptions options) {
    return require_resources("Resources::load_model_resource").load_model_resource_impl(uri, std::move(options));
}

// Advances asynchronous resource loads by one scheduler pass.
void Resources::advance_loads() {
    require_resources("Resources::advance_loads").advance_loads_impl();
}

// Returns the number of resources still scheduled for loading diagnostics.
std::size_t Resources::loading_resource_count() {
    return require_resources("Resources::loading_resource_count").m_loading_resources.size();
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

// Returns owned model resources for diagnostics and tests.
std::span<const std::unique_ptr<ModelResource>> Resources::model_resources() {
    return require_resources("Resources::model_resources").m_model_resources;
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
    m_textures.push_back(std::make_unique<Texture>(m_gpu, std::move(label)));
    return *m_textures.back();
}

// Allocates and stores a labeled shader resource.
Shader& Resources::create_shader_impl(std::string label) {
    require_live_for_create("Resources::create_shader");
    m_shaders.push_back(std::make_unique<Shader>(m_gpu, std::move(label)));
    return *m_shaders.back();
}

// Allocates and stores a labeled material resource.
Material& Resources::create_material_impl(std::string label) {
    require_live_for_create("Resources::create_material");
    m_materials.push_back(std::make_unique<Material>(m_gpu, std::move(label)));
    return *m_materials.back();
}

// Allocates and stores a labeled mesh resource.
Mesh& Resources::create_mesh_impl(std::string label) {
    require_live_for_create("Resources::create_mesh");
    m_meshes.push_back(std::make_unique<Mesh>(m_gpu, std::move(label)));
    return *m_meshes.back();
}

// Requests a normalized relative URI as a host-loaded blob.
BlobLoadId Resources::load_blob_impl(std::string_view uri) {
    require_live_for_create("Resources::load_blob");

    std::string normalized_uri = normalize_blob_uri(uri);
    const auto existing = m_blob_load_indices_by_uri.find(normalized_uri);
    if (existing != m_blob_load_indices_by_uri.end()) {
        return m_blob_loads[existing->second].m_id;
    }

    if (m_next_blob_load_id == invalid_blob_load_id) {
        throw EngineError("Resources::load_blob ran out of blob load ids.");
    }

    const BlobLoadId id = m_next_blob_load_id++;
    const std::size_t record_index = m_blob_loads.size();
    BlobLoadRecord record;
    record.m_id = id;
    record.m_uri = std::move(normalized_uri);
    record.m_status = BlobLoadStatus::Queued;
    m_blob_loads.push_back(std::move(record));
    const std::string& stored_uri = m_blob_loads.back().m_uri;
    m_blob_load_indices_by_uri.emplace(stored_uri, record_index);
    m_pending_blob_loads.push_back(PendingBlobLoad{id, stored_uri});
    return id;
}

// Returns a view over an existing blob request.
BlobView Resources::blob_impl(BlobLoadId id) const {
    if (id == invalid_blob_load_id) {
        throw EngineError("Resources::blob requires a valid blob load id.");
    }
    const auto record = std::find_if(
        m_blob_loads.begin(), m_blob_loads.end(), [id](const BlobLoadRecord& load) { return load.m_id == id; });
    if (record == m_blob_loads.end()) {
        throw EngineError("Resources::blob received an unknown blob load id.");
    }
    return make_blob_view(*record);
}

// Returns a view over a blob request found by URI, or Missing if unknown.
BlobView Resources::blob_by_uri_impl(std::string_view uri) const {
    const std::string normalized_uri = normalize_blob_uri(uri);
    const auto existing = m_blob_load_indices_by_uri.find(normalized_uri);
    if (existing == m_blob_load_indices_by_uri.end()) {
        return BlobView{invalid_blob_load_id, normalized_uri, BlobLoadStatus::Missing, {}, {}};
    }
    return make_blob_view(m_blob_loads[existing->second]);
}

// Moves a queued blob request into the loading state.
void Resources::mark_blob_loading_impl(BlobLoadId id) {
    if (id == invalid_blob_load_id) {
        throw EngineError("Resources::mark_blob_loading requires a valid blob load id.");
    }
    auto record = std::find_if(
        m_blob_loads.begin(), m_blob_loads.end(), [id](const BlobLoadRecord& load) { return load.m_id == id; });
    if (record == m_blob_loads.end()) {
        throw EngineError("Resources::mark_blob_loading received an unknown blob load id.");
    }
    if (record->m_status != BlobLoadStatus::Queued) {
        throw EngineError("Resources::mark_blob_loading requires a queued blob request, not one in state '" +
                          std::string(blob_load_status_name(record->m_status)) + "'.");
    }

    record->m_status = BlobLoadStatus::Loading;
    remove_pending_blob_load(m_pending_blob_loads, id);
}

// Stores bytes for an active blob request and marks it loaded.
void Resources::complete_blob_load_impl(BlobLoadId id, std::span<const std::byte> bytes) {
    if (id == invalid_blob_load_id) {
        throw EngineError("Resources::complete_blob_load requires a valid blob load id.");
    }
    auto record = std::find_if(
        m_blob_loads.begin(), m_blob_loads.end(), [id](const BlobLoadRecord& load) { return load.m_id == id; });
    if (record == m_blob_loads.end()) {
        throw EngineError("Resources::complete_blob_load received an unknown blob load id.");
    }
    if (record->m_status != BlobLoadStatus::Loading) {
        throw EngineError("Resources::complete_blob_load requires a loading blob request, not one in state '" +
                          std::string(blob_load_status_name(record->m_status)) + "'.");
    }

    record->m_bytes.assign(bytes.begin(), bytes.end());
    record->m_error.clear();
    record->m_status = BlobLoadStatus::Loaded;
}

// Stores a failure message for an active blob request and marks it failed.
void Resources::fail_blob_load_impl(BlobLoadId id, std::string message) {
    if (id == invalid_blob_load_id) {
        throw EngineError("Resources::fail_blob_load requires a valid blob load id.");
    }
    auto record = std::find_if(
        m_blob_loads.begin(), m_blob_loads.end(), [id](const BlobLoadRecord& load) { return load.m_id == id; });
    if (record == m_blob_loads.end()) {
        throw EngineError("Resources::fail_blob_load received an unknown blob load id.");
    }
    if (record->m_status != BlobLoadStatus::Loading) {
        throw EngineError("Resources::fail_blob_load requires a loading blob request, not one in state '" +
                          std::string(blob_load_status_name(record->m_status)) + "'.");
    }

    record->m_bytes.clear();
    record->m_error = message.empty() ? "Blob load failed." : std::move(message);
    record->m_status = BlobLoadStatus::Failed;
}

// Requests a model resource by URI and returns its stable observable object.
Ptr<ModelResource> Resources::load_model_resource_impl(std::string_view uri, ModelResourceLoadOptions options) {
    require_live_for_create("Resources::load_model_resource");

    const std::string normalized_uri = normalize_blob_uri(uri);
    std::string model_name = std::move(options.m_model_name);
    if (model_name.empty()) {
        model_name = model_name_from_uri(normalized_uri);
    }
    const std::string cache_key = model_resource_cache_key(normalized_uri, model_name);
    const auto existing = m_model_resource_indices_by_key.find(cache_key);
    if (existing != m_model_resource_indices_by_key.end()) {
        return Ptr<ModelResource>{m_model_resources[existing->second].get()};
    }

    auto resource = std::make_unique<ModelResource>();
    ModelResource* resource_ptr = resource.get();
    resource_ptr->begin_loading(normalized_uri, std::move(model_name));
    const std::size_t resource_index = m_model_resources.size();
    m_model_resources.push_back(std::move(resource));
    m_model_resource_indices_by_key.emplace(cache_key, resource_index);
    enqueue_loading(*resource_ptr);
    return Ptr<ModelResource>{resource_ptr};
}

// Adds a resource to the generic loading scheduler if it still needs work.
void Resources::enqueue_loading(Resource& resource) {
    if (resource.is_terminal()) {
        return;
    }
    if (std::find(m_loading_resources.begin(), m_loading_resources.end(), &resource) != m_loading_resources.end()) {
        return;
    }
    m_loading_resources.push_back(&resource);
}

// Advances asynchronous resource loads by one scheduler pass.
void Resources::advance_loads_impl() {
    for (Resource* resource : m_loading_resources) {
        if (resource == nullptr || resource->is_terminal()) {
            continue;
        }
        try {
            resource->update_loading();
        } catch (const std::exception& error) {
            resource->set_resource_failed(error.what());
        } catch (...) {
            resource->set_resource_failed("Resource loading failed with an unknown exception.");
        }
    }
    remove_terminal_loading_resources();
}

// Removes terminal resources from the generic loading scheduler.
void Resources::remove_terminal_loading_resources() {
    std::erase_if(
        m_loading_resources, [](const Resource* resource) { return resource == nullptr || resource->is_terminal(); });
}

// Creates a caller-owned snapshot over a blob record's current state.
BlobView Resources::make_blob_view(const BlobLoadRecord& record) const {
    return BlobView{record.m_id,
        record.m_uri,
        record.m_status,
        std::span<const std::byte>{record.m_bytes.data(), record.m_bytes.size()},
        record.m_error};
}

// Clears all resources in reverse dependency-friendly order.
void Resources::clear_resources() {
    m_loading_resources.clear();
    m_model_resource_indices_by_key.clear();
    m_model_resources.clear();
    m_meshes.clear();
    m_materials.clear();
    m_shaders.clear();
    m_textures.clear();
    m_pending_blob_loads.clear();
    m_blob_load_indices_by_uri.clear();
    m_blob_loads.clear();
    m_next_blob_load_id = 1;
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
