// Passive renderable component: shared assets and independent material, joint and morph bindings.
#pragma once

#include "scene/component.h"
#include "resources/mesh.h"
#include "resources/skin.h"

namespace ofg {
class Scene;
class MeshRenderer : public Component
{
public:
    // Releases shared assets and joint/entity observation.
    ~MeshRenderer() override = default;
    // Replaces the mesh, clears overrides/skin bindings and copies morph defaults; same mesh preserves state.
    void setMesh(std::shared_ptr<Mesh> mesh);
    // Returns the shared mesh, or null for an empty renderer.
    const std::shared_ptr<Mesh>& mesh() const noexcept { return m_mesh; }
    // Binds a ready Skin and unique live same-scene joints in palette order; requires a compatible Mesh.
    // Rejects absent influences, invalid palette indices and zero total vertex weight. Failure preserves bindings.
    // A null Skin with an empty palette clears the binding. No deformation is performed yet.
    void setSkin(std::shared_ptr<Skin> skin, std::vector<Ptr<Entity>> joints);
    // Returns the shared immutable Skin, or null for an unskinned renderer.
    const std::shared_ptr<Skin>& skin() const noexcept { return m_skin; }
    // Returns this instance's joint observers in Skin palette order.
    std::span<const Ptr<Entity>> joints() const noexcept { return m_joints; }
    // Sets finite per-instance weights matching the Mesh's target count; does not deform geometry yet.
    void setMorphWeights(std::vector<float> weights);
    // Returns current instance weights; changing these never edits the shared Mesh defaults.
    std::span<const float> morphWeights() const noexcept { return m_morphWeights; }
    // Sets a slot override; null restores the mesh default. Invalid slots throw.
    void setMaterialOverride(size_t index, std::shared_ptr<Material> material);
    // Returns only the explicit override, which may be null; invalid slots throw.
    const std::shared_ptr<Material>& materialOverride(size_t index) const;
    // Resolves an override or the mesh default; invalid slots throw.
    std::shared_ptr<Material> material(size_t index) const;
    // Clones the effective slot material, installs the clone and returns it.
    std::shared_ptr<Material> makeMaterialUnique(size_t index);

private:
    friend class Scene;
    // Creates an empty renderer bound to its scene entity.
    explicit MeshRenderer(Entity* entity) noexcept;
    // Rejects an index without a corresponding mesh submesh.
    void checkSlot(size_t index) const;
    std::shared_ptr<Mesh> m_mesh;
    std::vector<std::shared_ptr<Material>> m_overrides;
    std::shared_ptr<Skin> m_skin;
    std::vector<Ptr<Entity>> m_joints;
    std::vector<float> m_morphWeights;
};
} // namespace ofg
