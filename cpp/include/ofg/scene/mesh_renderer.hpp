// Mesh-renderer scene component.
//
// MeshRenderer stores non-owning resource pointers and draw data used by the
// renderer when it converts the active Scene into a transient DrawList.
#pragma once

#include "ofg/math/vec.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/scene/component.hpp"

#include <vector>

namespace ofg {

class Entity;
class Mesh;

class MeshRenderer : public Component {
public:
    // Binds this mesh renderer to one scene-owned entity.
    explicit MeshRenderer(Entity* entity) noexcept;

    // Returns the non-owning mesh resource pointer used for draw extraction.
    [[nodiscard]] Mesh* mesh() const noexcept;
    // Replaces the non-owning mesh resource pointer used for draw extraction.
    void set_mesh(Mesh* mesh) noexcept;

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

private:
    Mesh* m_mesh{nullptr};
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin_offset{0.0f, 0.0f, 0.0f};
};

} // namespace ofg
