// Mesh-renderer scene component.
//
// MeshRenderer stores non-owning resource pointers and draw data used by the
// renderer when it converts the active Scene into a transient DrawList.
#pragma once

#include "ofg/animation/skinning.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ofg {

class Entity;
struct SkinBinding {
    std::string m_name;
    std::uint32_t m_source_skin_index{0};
    Ptr<Mesh> m_bind_pose_mesh;
    std::unique_ptr<Mesh> m_dynamic_skinned_mesh;
    std::vector<Ptr<Entity>> m_joints_in_skin_order;
    std::vector<math::Mat4> m_inverse_bind_matrices;
    std::vector<SkinVertexInfluence> m_vertex_influences;
    std::vector<MeshVertex> m_skinned_vertices;
    std::vector<math::Mat4> m_mesh_from_joint_matrices;
    Ptr<Entity> m_skeleton_root;
    SkinningCounters m_counters;
};

class MeshRenderer : public Component {
public:
    // Binds this mesh renderer to one scene-owned entity.
    explicit MeshRenderer(Entity* entity) noexcept;

    // Returns the non-owning mesh resource pointer used for draw extraction.
    [[nodiscard]] Mesh* mesh() const noexcept;
    // Returns the non-owning bind-pose mesh resource pointer.
    [[nodiscard]] Mesh* bind_pose_mesh() const noexcept;
    // Replaces the non-owning mesh resource pointer used for draw extraction.
    void set_mesh(Mesh* mesh);

    // Returns mutable draw-scoped properties for setup-time authoring.
    [[nodiscard]] PropertyBag& properties() noexcept;
    // Returns draw-scoped properties for renderer extraction.
    [[nodiscard]] const PropertyBag& properties() const noexcept;

    // Returns mutable material overrides for setup-time authoring.
    [[nodiscard]] std::vector<MaterialOverride>& material_overrides() noexcept;
    // Returns material overrides for renderer extraction.
    [[nodiscard]] const std::vector<MaterialOverride>& material_overrides() const noexcept;
    // Replaces the material overrides in one move-aware operation.
    void set_material_overrides(std::vector<MaterialOverride> material_overrides);

    // Returns the local-space point used as this renderer's sort origin.
    [[nodiscard]] math::Vec3 sort_origin_offset() const noexcept;
    // Replaces the local-space point used as this renderer's sort origin.
    void set_sort_origin_offset(math::Vec3 offset) noexcept;
    // Returns whether this renderer should be emitted into render draw lists.
    [[nodiscard]] bool visible() const noexcept;
    // Sets whether this renderer should be emitted into render draw lists.
    void set_visible(bool visible) noexcept;

    // Returns skin metadata owned by this renderer instance, if any.
    [[nodiscard]] SkinBinding* skin_binding() noexcept;
    // Returns skin metadata owned by this renderer instance, if any.
    [[nodiscard]] const SkinBinding* skin_binding() const noexcept;
    // Replaces skin metadata for this renderer instance.
    void set_skin_binding(SkinBinding binding);
    // Removes skin metadata from this renderer instance.
    void clear_skin_binding() noexcept;
    // Updates the per-instance CPU-skinned mesh, if this renderer has skin metadata.
    void update_skinning();
    // Updates the per-instance CPU-skinned mesh using a scene-owned world-transform cache.
    void update_skinning(std::span<const math::Mat4> world_from_entities);
    // Reports CPU skinning counters for this renderer instance.
    [[nodiscard]] SkinningCounters skinning_counters() const noexcept;

private:
    // Creates or refreshes per-instance dynamic skinning resources.
    void initialize_skinning_resources(SkinBinding& binding);
    // Shared implementation for explicit calls and scene-update cached calls.
    void update_skinning_impl(std::span<const math::Mat4> world_from_entities);

    Ptr<Mesh> m_mesh;
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin_offset{0.0f, 0.0f, 0.0f};
    bool m_visible{true};
    std::optional<SkinBinding> m_skin_binding;
};

} // namespace ofg
