// Mesh-renderer scene component implementation.
#include "ofg/scene/mesh_renderer.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/scene.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Returns a Vec3 from a MeshVertex array field.
math::Vec3 vertex_vec3(const std::array<float, 3>& value) noexcept {
    return math::vec3(value[0], value[1], value[2]);
}

// Returns a Vec3 from the xyz portion of a tangent field.
math::Vec3 tangent_vec3(const std::array<float, 4>& value) noexcept {
    return math::vec3(value[0], value[1], value[2]);
}

// Stores a Vec3 into a MeshVertex array field.
std::array<float, 3> pack_vec3(math::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

// Adds a weighted vector into an accumulator.
void add_weighted(math::Vec3& accumulator, math::Vec3 value, float weight) noexcept {
    accumulator = math::add(accumulator, math::mul(value, weight));
}

// Returns a normalized vector or the fallback if the result degenerates.
math::Vec3 normalize_or(math::Vec3 value, math::Vec3 fallback) {
    std::string error;
    const std::optional<math::Vec3> normalized = math::normalize(value, error);
    return normalized.has_value() ? *normalized : fallback;
}

// Returns a vector copy of a span.
template <typename T> std::vector<T> vector_from_span(std::span<const T> values) {
    return std::vector<T>(values.begin(), values.end());
}

// Returns an entity world transform, preferring the scene cache when supplied.
math::Mat4 cached_world_from_entity(const Entity& entity, std::span<const math::Mat4> world_from_entities) {
    if (world_from_entities.empty()) {
        return world_from_local(entity);
    }
    if (entity.id() >= world_from_entities.size()) {
        throw EngineError("Scene world-transform cache is missing a skinned entity.");
    }
    return world_from_entities[entity.id()];
}

} // namespace

// Binds this mesh renderer to one scene-owned entity.
MeshRenderer::MeshRenderer(Entity* entity) noexcept : Component(ComponentType::MeshRenderer, entity) {}

// Returns the non-owning mesh resource pointer used for draw extraction.
Mesh* MeshRenderer::mesh() const noexcept {
    if (m_skin_binding.has_value() && m_skin_binding->m_dynamic_skinned_mesh != nullptr) {
        return m_skin_binding->m_dynamic_skinned_mesh.get();
    }
    return m_mesh.get();
}

// Returns the non-owning bind-pose mesh resource pointer.
Mesh* MeshRenderer::bind_pose_mesh() const noexcept {
    return m_mesh.get();
}

// Replaces the non-owning mesh resource pointer used for draw extraction.
void MeshRenderer::set_mesh(Mesh* mesh) {
    m_mesh = mesh;
    if (m_skin_binding.has_value()) {
        initialize_skinning_resources(*m_skin_binding);
    }
}

// Returns mutable draw-scoped properties for setup-time authoring.
PropertyBag& MeshRenderer::properties() noexcept {
    return m_properties;
}

// Returns draw-scoped properties for renderer extraction.
const PropertyBag& MeshRenderer::properties() const noexcept {
    return m_properties;
}

// Returns mutable material overrides for setup-time authoring.
std::vector<MaterialOverride>& MeshRenderer::material_overrides() noexcept {
    return m_material_overrides;
}

// Returns material overrides for renderer extraction.
const std::vector<MaterialOverride>& MeshRenderer::material_overrides() const noexcept {
    return m_material_overrides;
}

// Replaces the material overrides in one move-aware operation.
void MeshRenderer::set_material_overrides(std::vector<MaterialOverride> material_overrides) {
    m_material_overrides = std::move(material_overrides);
}

// Returns the local-space point used as this renderer's sort origin.
math::Vec3 MeshRenderer::sort_origin_offset() const noexcept {
    return m_sort_origin_offset;
}

// Replaces the local-space point used as this renderer's sort origin.
void MeshRenderer::set_sort_origin_offset(math::Vec3 offset) noexcept {
    m_sort_origin_offset = offset;
}

// Returns whether this renderer should be emitted into render draw lists.
bool MeshRenderer::visible() const noexcept {
    return m_visible;
}

// Sets whether this renderer should be emitted into render draw lists.
void MeshRenderer::set_visible(bool visible) noexcept {
    m_visible = visible;
}

// Returns skin metadata owned by this renderer instance, if any.
SkinBinding* MeshRenderer::skin_binding() noexcept {
    return m_skin_binding.has_value() ? &*m_skin_binding : nullptr;
}

// Returns skin metadata owned by this renderer instance, if any.
const SkinBinding* MeshRenderer::skin_binding() const noexcept {
    return m_skin_binding.has_value() ? &*m_skin_binding : nullptr;
}

// Replaces skin metadata for this renderer instance.
void MeshRenderer::set_skin_binding(SkinBinding binding) {
    m_skin_binding = std::move(binding);
    initialize_skinning_resources(*m_skin_binding);
}

// Removes skin metadata from this renderer instance.
void MeshRenderer::clear_skin_binding() noexcept {
    m_skin_binding.reset();
}

// Updates the per-instance CPU-skinned mesh, if this renderer has skin metadata.
void MeshRenderer::update_skinning() {
    update_skinning_impl(std::span<const math::Mat4>{});
}

// Updates the per-instance CPU-skinned mesh using a scene-owned world-transform cache.
void MeshRenderer::update_skinning(std::span<const math::Mat4> world_from_entities) {
    update_skinning_impl(world_from_entities);
}

// Shared implementation for explicit calls and scene-update cached calls.
void MeshRenderer::update_skinning_impl(std::span<const math::Mat4> world_from_entities) {
    if (!m_skin_binding.has_value()) {
        return;
    }
    SkinBinding& binding = *m_skin_binding;
    if (entity() == nullptr) {
        throw EngineError("MeshRenderer skinning requires an owning entity.");
    }
    if (binding.m_bind_pose_mesh == nullptr || binding.m_dynamic_skinned_mesh == nullptr) {
        initialize_skinning_resources(binding);
    }
    Mesh* bind_pose_mesh = binding.m_bind_pose_mesh.get();
    Mesh* dynamic_mesh = binding.m_dynamic_skinned_mesh.get();
    if (bind_pose_mesh == nullptr || dynamic_mesh == nullptr) {
        throw EngineError("MeshRenderer skinning requires valid bind-pose and dynamic meshes.");
    }

    std::string inverse_error;
    const std::optional<math::Mat4> mesh_from_world =
        math::inverse_affine(cached_world_from_entity(*entity(), world_from_entities), inverse_error);
    if (!mesh_from_world.has_value()) {
        throw EngineError("Skinned mesh entity transform is not invertible.");
    }
    for (std::size_t joint_index = 0; joint_index < binding.m_joints_in_skin_order.size(); ++joint_index) {
        Entity* joint = binding.m_joints_in_skin_order[joint_index].get();
        if (joint == nullptr) {
            throw EngineError("MeshRenderer skinning joint entity has been destroyed.");
        }
        binding.m_mesh_from_joint_matrices[joint_index] =
            math::mul(math::mul(*mesh_from_world, cached_world_from_entity(*joint, world_from_entities)),
                binding.m_inverse_bind_matrices[joint_index]);
    }

    const std::span<const MeshVertex> bind_vertices = bind_pose_mesh->vertices();
    for (std::size_t vertex_index = 0; vertex_index < bind_vertices.size(); ++vertex_index) {
        const MeshVertex& source = bind_vertices[vertex_index];
        const SkinVertexInfluence& influence = binding.m_vertex_influences[vertex_index];
        math::Vec3 position = math::vec3(0.0f, 0.0f, 0.0f);
        math::Vec3 normal = math::vec3(0.0f, 0.0f, 0.0f);
        math::Vec3 tangent = math::vec3(0.0f, 0.0f, 0.0f);
        const math::Vec3 source_position = vertex_vec3(source.m_position);
        const math::Vec3 source_normal = vertex_vec3(source.m_normal);
        const math::Vec3 source_tangent = tangent_vec3(source.m_tangent);

        for (std::size_t influence_index = 0; influence_index < influence.m_weights.size(); ++influence_index) {
            const float weight = influence.m_weights[influence_index];
            if (weight <= 0.0f) {
                continue;
            }
            const std::uint32_t joint_index = influence.m_joint_indices[influence_index];
            if (joint_index >= binding.m_mesh_from_joint_matrices.size()) {
                throw EngineError("MeshRenderer skinning influence references a missing joint.");
            }
            const math::Mat4 mesh_from_joint = binding.m_mesh_from_joint_matrices[joint_index];
            add_weighted(position, math::transform_point(mesh_from_joint, source_position), weight);
            add_weighted(normal, math::transform_direction(mesh_from_joint, source_normal), weight);
            add_weighted(tangent, math::transform_direction(mesh_from_joint, source_tangent), weight);
        }

        MeshVertex& target = binding.m_skinned_vertices[vertex_index];
        target = source;
        target.m_position = pack_vec3(position);
        target.m_normal = pack_vec3(normalize_or(normal, source_normal));
        const math::Vec3 normalized_tangent = normalize_or(tangent, source_tangent);
        target.m_tangent = {normalized_tangent.x, normalized_tangent.y, normalized_tangent.z, source.m_tangent[3]};
    }

    const std::uint64_t upload_before = dynamic_mesh->vertex_upload_bytes();
    dynamic_mesh->update_vertices_in_place(binding.m_skinned_vertices);
    binding.m_counters.m_vertices_skinned += static_cast<std::uint64_t>(binding.m_skinned_vertices.size());
    binding.m_counters.m_vertex_upload_bytes += dynamic_mesh->vertex_upload_bytes() - upload_before;
    binding.m_counters.m_dynamic_vertex_buffer_create_count = dynamic_mesh->vertex_buffer_create_count();
}

// Reports CPU skinning counters for this renderer instance.
SkinningCounters MeshRenderer::skinning_counters() const noexcept {
    if (!m_skin_binding.has_value()) {
        return SkinningCounters{};
    }
    SkinningCounters counters = m_skin_binding->m_counters;
    if (m_skin_binding->m_dynamic_skinned_mesh != nullptr) {
        counters.m_dynamic_vertex_buffer_create_count =
            m_skin_binding->m_dynamic_skinned_mesh->vertex_buffer_create_count();
    }
    return counters;
}

// Creates or refreshes per-instance dynamic skinning resources.
void MeshRenderer::initialize_skinning_resources(SkinBinding& binding) {
    Mesh* bind_pose_mesh = m_mesh.get();
    if (bind_pose_mesh == nullptr) {
        throw EngineError("MeshRenderer skinning requires a bind-pose mesh.");
    }
    if (binding.m_joints_in_skin_order.empty()) {
        throw EngineError("MeshRenderer skinning requires at least one joint.");
    }
    if (binding.m_inverse_bind_matrices.size() != binding.m_joints_in_skin_order.size()) {
        throw EngineError("MeshRenderer skinning inverse bind matrix count must match joint count.");
    }
    if (binding.m_vertex_influences.size() != bind_pose_mesh->vertices().size()) {
        throw EngineError("MeshRenderer skinning influence count must match bind-pose vertex count.");
    }
    for (const SkinVertexInfluence& influence : binding.m_vertex_influences) {
        for (std::uint32_t joint_index : influence.m_joint_indices) {
            if (joint_index >= binding.m_joints_in_skin_order.size()) {
                throw EngineError("MeshRenderer skinning influence references a joint outside the skin binding.");
            }
        }
    }

    binding.m_bind_pose_mesh = bind_pose_mesh;
    binding.m_skinned_vertices = vector_from_span(bind_pose_mesh->vertices());
    binding.m_mesh_from_joint_matrices.assign(binding.m_joints_in_skin_order.size(), math::mat4_identity());
    auto dynamic_mesh = std::make_unique<Mesh>(bind_pose_mesh->gpu_context(), bind_pose_mesh->label() + " skinned");
    dynamic_mesh->init_dynamic_vertices(vector_from_span(bind_pose_mesh->vertices()),
        vector_from_span(bind_pose_mesh->indices()),
        vector_from_span(bind_pose_mesh->submeshes()));
    binding.m_counters = SkinningCounters{};
    binding.m_counters.m_dynamic_vertex_buffer_create_count = dynamic_mesh->vertex_buffer_create_count();
    binding.m_dynamic_skinned_mesh = std::move(dynamic_mesh);
}

} // namespace ofg
