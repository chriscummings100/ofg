// Passive renderable component: shared mesh plus nullable per-submesh material overrides.
#pragma once

#include "scene/component.h"
#include "resources/mesh.h"

namespace ofg {
class Scene;
class MeshRenderer : public Component
{
public:
    // Releases mesh/material ownership and entity observation.
    ~MeshRenderer() override = default;
    // Replaces the mesh and clears overrides; setting the same mesh preserves them.
    void setMesh(std::shared_ptr<Mesh> mesh);
    // Returns the shared mesh, or null for an empty renderer.
    const std::shared_ptr<Mesh>& mesh() const noexcept { return m_mesh; }
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
};
} // namespace ofg
