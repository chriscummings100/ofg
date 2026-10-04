// Validated per-instance material, joint and morph bindings over shared immutable mesh/skin resources.
#include "scene/mesh-renderer.h"
#include "scene/entity.h"
#include "core/engine-error.h"
#include <cmath>
#include <unordered_set>
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
    std::vector<float> morphWeights = mesh ? mesh->deformation().defaultMorphWeights : std::vector<float>{};
    m_mesh = std::move(mesh);
    m_overrides = std::move(overrides);
    m_skin.reset();
    m_joints.clear();
    m_morphWeights = std::move(morphWeights);
}

void MeshRenderer::setSkin(std::shared_ptr<Skin> skin, std::vector<Ptr<Entity>> joints)
{
    if (!skin)
    {
        if (!joints.empty())
        {
            throw EngineError("An empty Skin requires an empty joint palette.");
        }
        m_skin.reset();
        m_joints.clear();
        return;
    }
    if (!skin->isLoaded() || !m_mesh || joints.size() != skin->desc().joints.size())
    {
        throw EngineError("Skin binding requires a ready Skin, a Mesh and matching joint palette size.");
    }
    std::unordered_set<const Entity*> uniqueJoints;
    for (const auto& joint : joints)
    {
        if (!joint || &joint->scene() != &entity()->scene() || !uniqueJoints.insert(joint.get()).second)
        {
            throw EngineError("Skin binding requires unique live joints in the renderer's scene.");
        }
    }

    const auto& influenceSets = m_mesh->deformation().influenceSets;
    if (influenceSets.empty())
    {
        throw EngineError("Skin binding requires Mesh joint influences.");
    }
    for (size_t vertex = 0; vertex < m_mesh->vertices().size(); ++vertex)
    {
        double totalWeight = 0;
        for (const auto& set : influenceSets)
        {
            const auto& influences = set[vertex];
            for (uint32_t joint : influences.joints)
            {
                if (joint >= joints.size())
                {
                    throw EngineError("Mesh joint index is outside the Skin palette.");
                }
            }
            totalWeight +=
                double(influences.weights.x) + influences.weights.y + influences.weights.z + influences.weights.w;
        }
        if (totalWeight <= 0)
        {
            throw EngineError("Every skinned vertex requires positive total joint weight.");
        }
    }
    m_skin = std::move(skin);
    m_joints = std::move(joints);
}

void MeshRenderer::setMorphWeights(std::vector<float> weights)
{
    if (!m_mesh || weights.size() != m_mesh->deformation().morphTargets.size())
    {
        throw EngineError("Morph weights require a Mesh and one value per target.");
    }
    for (float weight : weights)
    {
        if (!std::isfinite(weight))
        {
            throw EngineError("Morph weights must be finite.");
        }
    }
    m_morphWeights = std::move(weights);
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
