// Static resource-system facade for high-level OFG assets.
//
// This file owns the private singleton behind the public Resources API. The
// instance owns stable resource vectors directly so Game can orchestrate
// resource startup and teardown without a separate arena owner.
#include "ofg/resources/resources.hpp"

#include "ofg/assets/gltf_importer.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resource.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <algorithm>
#include <cstddef>
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

    const BlobLoadId root_blob_id = load_blob_impl(uri);
    const BlobView root_blob = blob_impl(root_blob_id);
    std::string model_name = std::move(options.m_model_name);
    if (model_name.empty()) {
        model_name = model_name_from_uri(root_blob.m_uri);
    }
    const std::string cache_key = model_resource_cache_key(root_blob.m_uri, model_name);
    const auto existing = m_model_load_indices_by_key.find(cache_key);
    if (existing != m_model_load_indices_by_key.end()) {
        return Ptr<ModelResource>{m_model_loads[existing->second].m_resource};
    }

    auto resource = std::make_unique<ModelResource>();
    ModelResource* resource_ptr = resource.get();
    resource_ptr->set_source_uri(root_blob.m_uri);
    resource_ptr->clear_resource_error();
    resource_ptr->set_resource_state(ResourceState::Queued);
    m_model_resources.push_back(std::move(resource));

    const std::size_t load_index = m_model_loads.size();
    ModelResourceLoadRecord record;
    record.m_resource = resource_ptr;
    record.m_cache_key = cache_key;
    record.m_uri = root_blob.m_uri;
    record.m_model_name = std::move(model_name);
    record.m_root_blob_id = root_blob_id;
    m_model_loads.push_back(std::move(record));
    m_model_load_indices_by_key.emplace(cache_key, load_index);
    m_in_progress_model_load_indices.push_back(load_index);
    return Ptr<ModelResource>{resource_ptr};
}

// Advances asynchronous resource loads by one scheduler pass.
void Resources::advance_loads_impl() {
    for (const std::size_t load_index : m_in_progress_model_load_indices) {
        advance_model_load(load_index);
    }
    remove_terminal_model_loads();
}

// Advances one model resource load record by at most one major state.
void Resources::advance_model_load(std::size_t load_index) {
    if (load_index >= m_model_loads.size()) {
        throw EngineError("Resources model load scheduler contains an invalid load index.");
    }
    ModelResourceLoadRecord& load = m_model_loads[load_index];
    ModelResource* resource = load.m_resource;
    if (resource == nullptr) {
        throw EngineError("Resources model load scheduler contains a null model resource.");
    }

    switch (resource->state()) {
    case ResourceState::Queued:
        resource->set_resource_state(ResourceState::LoadingRootBlob);
        return;
    case ResourceState::LoadingRootBlob: {
        const BlobView root_blob = blob_impl(load.m_root_blob_id);
        if (root_blob.m_status == BlobLoadStatus::Failed) {
            resource->set_resource_failed(
                "Model resource root blob '" + root_blob.m_uri + "' failed: " + root_blob.m_error);
            return;
        }
        if (root_blob.m_status == BlobLoadStatus::Loaded) {
            resource->set_resource_state(ResourceState::DiscoveringDependencies);
        }
        return;
    }
    case ResourceState::DiscoveringDependencies: {
        const BlobView root_blob = blob_impl(load.m_root_blob_id);
        if (!root_blob.is_loaded()) {
            resource->set_resource_state(ResourceState::LoadingRootBlob);
            return;
        }

        ResourcesGltfResourceProvider provider(load.m_uri);
        try {
            load.m_pending_document = load_gltf_document(load.m_uri, root_blob.m_bytes, provider);
        } catch (const std::exception& error) {
            if (!provider.dependency_blob_ids().empty()) {
                bool waiting_for_dependencies = false;
                for (const BlobLoadId dependency_id : provider.dependency_blob_ids()) {
                    const BlobView dependency = blob_impl(dependency_id);
                    if (dependency.m_status == BlobLoadStatus::Failed) {
                        resource->set_resource_failed("Model resource '" + load.m_uri + "' dependency '" +
                                                      dependency.m_uri + "' failed: " + dependency.m_error);
                        return;
                    }
                    if (dependency.m_status != BlobLoadStatus::Loaded) {
                        waiting_for_dependencies = true;
                    }
                }
                if (waiting_for_dependencies) {
                    load.m_dependency_blob_ids = provider.dependency_blob_ids();
                    load.m_dependency_uris = provider.dependency_uris();
                    resource->set_resource_state(ResourceState::WaitingForDependencies);
                    return;
                }
            }
            resource->set_resource_failed(
                "Model resource '" + load.m_uri + "' failed during dependency discovery: " + error.what());
            return;
        }

        load.m_dependency_blob_ids = provider.dependency_blob_ids();
        load.m_dependency_uris = provider.dependency_uris();
        resource->set_resource_state(ResourceState::Importing);
        return;
    }
    case ResourceState::WaitingForDependencies: {
        bool all_loaded = true;
        for (std::size_t index = 0; index < load.m_dependency_blob_ids.size(); ++index) {
            const BlobView dependency = blob_impl(load.m_dependency_blob_ids[index]);
            if (dependency.m_status == BlobLoadStatus::Failed) {
                resource->set_resource_failed("Model resource '" + load.m_uri + "' dependency '" + dependency.m_uri +
                                              "' failed: " + dependency.m_error);
                return;
            }
            if (dependency.m_status != BlobLoadStatus::Loaded) {
                all_loaded = false;
            }
        }
        if (all_loaded) {
            resource->set_resource_state(ResourceState::DiscoveringDependencies);
        }
        return;
    }
    case ResourceState::Importing:
        if (!load.m_pending_document.has_value()) {
            resource->set_resource_failed("Model resource '" + load.m_uri + "' has no parsed document to import.");
            return;
        }
        try {
            import_gltf_model_resource_into(*load.m_pending_document,
                GltfImportOptions{load.m_model_name, load.m_uri},
                model_import_context(),
                *resource);
            load.m_pending_document.reset();
            resource->clear_resource_error();
            resource->set_resource_state(ResourceState::Loaded);
        } catch (const std::exception& error) {
            load.m_pending_document.reset();
            resource->set_resource_failed("Model resource '" + load.m_uri + "' failed during import: " + error.what());
        }
        return;
    case ResourceState::Loaded:
    case ResourceState::Failed:
    case ResourceState::Unloaded:
        return;
    }
}

// Removes terminal model loads from the in-progress list.
void Resources::remove_terminal_model_loads() {
    std::erase_if(m_in_progress_model_load_indices, [this](std::size_t load_index) {
        if (load_index >= m_model_loads.size()) {
            return true;
        }
        const ModelResource* resource = m_model_loads[load_index].m_resource;
        return resource == nullptr || resource->is_terminal();
    });
}

// Returns the model import cache owned by Resources.
ModelResourceImportContext& Resources::model_import_context() {
    if (m_model_import_context == nullptr) {
        m_model_import_context = std::make_unique<ModelResourceImportContext>(m_gpu);
    }
    return *m_model_import_context;
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
    m_in_progress_model_load_indices.clear();
    m_model_load_indices_by_key.clear();
    m_model_loads.clear();
    m_model_resources.clear();
    m_model_import_context.reset();
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
