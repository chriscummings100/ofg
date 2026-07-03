// Static resource-system facade for high-level OFG assets.
//
// Resources owns stable storage for texture, shader, material, and mesh assets
// for one WebGPU device lifetime. Public create_* methods allocate high-level
// resource objects; explicit resource methods fill them with CPU/GPU data.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/assets/gltf_document.hpp"
#include "ofg/core/ptr.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ofg {

class Material;
class Mesh;
class ModelResource;
class ModelResourceImportContext;
class Shader;
class Texture;

using BlobLoadId = std::uint32_t;

inline constexpr BlobLoadId invalid_blob_load_id = 0;

enum class BlobLoadStatus {
    Missing,
    Queued,
    Loading,
    Loaded,
    Failed,
};

enum class ResourcesLifecycleState {
    Uninitialized,
    Created,
    Preparing,
    Ready,
    Releasing,
    Released,
    Failed,
};

// Converts a blob load status into a stable diagnostic string.
[[nodiscard]] const char* blob_load_status_name(BlobLoadStatus status) noexcept;

// Converts a Resources lifecycle state into a stable diagnostic string.
[[nodiscard]] const char* resources_lifecycle_state_name(ResourcesLifecycleState state) noexcept;

struct PendingBlobLoad {
    BlobLoadId m_id{invalid_blob_load_id};
    std::string m_uri;
};

struct BlobView {
    BlobLoadId m_id{invalid_blob_load_id};
    std::string m_uri;
    BlobLoadStatus m_status{BlobLoadStatus::Missing};
    std::span<const std::byte> m_bytes;
    std::string m_error;

    // Reports whether this view currently exposes loaded bytes.
    [[nodiscard]] bool is_loaded() const noexcept {
        return m_status == BlobLoadStatus::Loaded;
    }
};

struct ModelResourceLoadOptions {
    std::string m_model_name;
};

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
    // Requests a binary asset blob by normalized relative URI, returning a stable request id.
    [[nodiscard]] static BlobLoadId load_blob(std::string_view uri);
    // Returns the current state and bytes for an existing blob request id.
    [[nodiscard]] static BlobView blob(BlobLoadId id);
    // Returns the current state and bytes for a normalized relative URI, or Missing if unknown.
    [[nodiscard]] static BlobView blob_by_uri(std::string_view uri);
    // Returns queued blob requests that still need to be serviced by the host.
    [[nodiscard]] static std::span<const PendingBlobLoad> pending_blob_loads();
    // Marks a queued blob request as being actively loaded by the host.
    static void mark_blob_loading(BlobLoadId id);
    // Completes an active blob request with bytes supplied by the host.
    static void complete_blob_load(BlobLoadId id, std::span<const std::byte> bytes);
    // Fails an active blob request with a host-supplied diagnostic message.
    static void fail_blob_load(BlobLoadId id, std::string message);
    // Requests a model resource by URI and returns its stable observable object.
    [[nodiscard]] static Ptr<ModelResource> load_model_resource(
        std::string_view uri, ModelResourceLoadOptions options = {});
    // Advances asynchronous resource loads by one scheduler pass.
    static void advance_loads();
    // Returns owned textures for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Texture>> textures();
    // Returns owned shaders for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Shader>> shaders();
    // Returns owned materials for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Material>> materials();
    // Returns owned meshes for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<Mesh>> meshes();
    // Returns owned model resources for diagnostics and tests.
    [[nodiscard]] static std::span<const std::unique_ptr<ModelResource>> model_resources();

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
    // Requests a normalized relative URI as a host-loaded blob.
    [[nodiscard]] BlobLoadId load_blob_impl(std::string_view uri);
    // Returns a view over an existing blob request.
    [[nodiscard]] BlobView blob_impl(BlobLoadId id) const;
    // Returns a view over a blob request found by URI, or Missing if unknown.
    [[nodiscard]] BlobView blob_by_uri_impl(std::string_view uri) const;
    // Moves a queued blob request into the loading state.
    void mark_blob_loading_impl(BlobLoadId id);
    // Stores bytes for an active blob request and marks it loaded.
    void complete_blob_load_impl(BlobLoadId id, std::span<const std::byte> bytes);
    // Stores a failure message for an active blob request and marks it failed.
    void fail_blob_load_impl(BlobLoadId id, std::string message);
    // Requests a model resource by URI and returns its stable observable object.
    [[nodiscard]] Ptr<ModelResource> load_model_resource_impl(std::string_view uri, ModelResourceLoadOptions options);
    // Advances asynchronous resource loads by one scheduler pass.
    void advance_loads_impl();
    // Advances one model resource load record by at most one major state.
    void advance_model_load(std::size_t load_index);
    // Removes terminal model loads from the in-progress list.
    void remove_terminal_model_loads();
    // Returns the model import cache owned by Resources.
    [[nodiscard]] ModelResourceImportContext& model_import_context();
    // Clears all resources in reverse dependency-friendly order.
    void clear_resources();
    // Throws if resource allocation is no longer allowed.
    void require_live_for_create(const char* operation) const;
    // Updates this instance lifecycle state.
    void set_state(ResourcesLifecycleState state) noexcept;

    // Returns the live singleton or throws a clear lifecycle error.
    [[nodiscard]] static Resources& require_resources(const char* operation);

    struct BlobLoadRecord {
        BlobLoadId m_id{invalid_blob_load_id};
        std::string m_uri;
        BlobLoadStatus m_status{BlobLoadStatus::Missing};
        std::vector<std::byte> m_bytes;
        std::string m_error;
    };

    struct ModelResourceLoadRecord {
        ModelResource* m_resource{nullptr};
        std::string m_cache_key;
        std::string m_uri;
        std::string m_model_name;
        BlobLoadId m_root_blob_id{invalid_blob_load_id};
        std::vector<BlobLoadId> m_dependency_blob_ids;
        std::vector<std::string> m_dependency_uris;
        std::optional<GltfDocument> m_pending_document;
    };

    // Creates a caller-owned snapshot over a blob record's current state.
    [[nodiscard]] BlobView make_blob_view(const BlobLoadRecord& record) const;

    static std::unique_ptr<Resources> s_resources;

    GpuContext m_gpu;
    ResourcesLifecycleState m_state{ResourcesLifecycleState::Uninitialized};
    BlobLoadId m_next_blob_load_id{1};
    std::vector<BlobLoadRecord> m_blob_loads;
    std::unordered_map<std::string, std::size_t> m_blob_load_indices_by_uri;
    std::vector<PendingBlobLoad> m_pending_blob_loads;
    std::unique_ptr<ModelResourceImportContext> m_model_import_context;
    std::vector<std::unique_ptr<ModelResource>> m_model_resources;
    std::vector<ModelResourceLoadRecord> m_model_loads;
    std::unordered_map<std::string, std::size_t> m_model_load_indices_by_key;
    std::vector<std::size_t> m_in_progress_model_load_indices;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<Shader>> m_shaders;
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<std::unique_ptr<Mesh>> m_meshes;
};

} // namespace ofg
