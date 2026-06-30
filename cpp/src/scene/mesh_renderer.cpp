// Mesh-renderer scene component implementation.
#include "ofg/scene/mesh_renderer.hpp"

#include "ofg/scene/entity.hpp"

#include <utility>

namespace ofg {

// Binds this mesh renderer to one scene-owned entity.
MeshRenderer::MeshRenderer(Entity* entity) noexcept : Component(ComponentType::MeshRenderer, entity) {}

// Returns the non-owning mesh resource pointer used for draw extraction.
Mesh* MeshRenderer::mesh() const noexcept {
    return m_mesh;
}

// Replaces the non-owning mesh resource pointer used for draw extraction.
void MeshRenderer::set_mesh(Mesh* mesh) noexcept {
    m_mesh = mesh;
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

} // namespace ofg
