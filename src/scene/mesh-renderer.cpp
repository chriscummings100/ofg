// Explicit material slot resolution and value-copy instancing for mesh renderers.
#include "scene/mesh-renderer.h"
#include "core/engine-error.h"
#include <utility>

namespace ofg {
MeshRenderer::MeshRenderer(Entity* entity) noexcept
    : Component(entity)
{
}

void MeshRenderer::setMesh(std::shared_ptr<Mesh> mesh)
{
    if (mesh == m_mesh)
    {
        return;
    }
    std::vector<std::shared_ptr<Material>> overrides(mesh ? mesh->subMeshes().size() : 0);
    m_mesh = std::move(mesh);
    m_overrides = std::move(overrides);
}

void MeshRenderer::checkSlot(size_t index) const
{
    if (index >= m_overrides.size())
    {
        throw EngineError("Material slot is outside the mesh's submeshes.");
    }
}

void MeshRenderer::setMaterialOverride(size_t index, std::shared_ptr<Material> material)
{
    checkSlot(index);
    m_overrides[index] = std::move(material);
}

const std::shared_ptr<Material>& MeshRenderer::materialOverride(size_t index) const
{
    checkSlot(index);
    return m_overrides[index];
}

std::shared_ptr<Material> MeshRenderer::material(size_t index) const
{
    checkSlot(index);
    return m_overrides[index] ? m_overrides[index] : m_mesh->subMeshes()[index].material;
}

std::shared_ptr<Material> MeshRenderer::makeMaterialUnique(size_t index)
{
    auto unique = material(index)->clone();
    setMaterialOverride(index, unique);
    return unique;
}
} // namespace ofg
