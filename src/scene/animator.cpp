// Validates clip-to-instance bindings without evaluating tracks or changing transforms and weights.
#include "scene/animator.h"
#include "scene/entity.h"
#include "scene/mesh-renderer.h"
#include "core/engine-error.h"
#include <unordered_set>
#include <utility>

namespace ofg {
Animator::Animator(Entity* entity) noexcept
    : Component(entity)
{
}

void Animator::setBindings(std::vector<std::shared_ptr<Animation>> animations, std::vector<Ptr<Entity>> nodes)
{
    std::unordered_set<const Entity*> uniqueNodes;
    for (const auto& node : nodes)
    {
        if (!node)
        {
            continue;
        }
        if (!uniqueNodes.insert(node.get()).second)
        {
            throw EngineError("Animator source nodes must map to distinct entities.");
        }
        const Entity* ancestor = node.get();
        while (ancestor && ancestor != entity())
        {
            ancestor = ancestor->parent();
        }
        if (!ancestor)
        {
            throw EngineError("Animator targets must belong to its root subtree.");
        }
    }
    for (const auto& animation : animations)
    {
        if (!animation || !animation->isLoaded())
        {
            throw EngineError("Animator requires non-null ready Animation resources.");
        }
        for (const auto& track : animation->desc().tracks)
        {
            if (track.node >= nodes.size())
            {
                throw EngineError("Animation target is outside the source node mapping.");
            }
            if (nodes[track.node] && track.path == AnimationPath::Weights)
            {
                const auto renderer = nodes[track.node]->meshRenderer();
                if (!renderer || !renderer->mesh() ||
                    renderer->mesh()->deformation().morphTargets.size() != track.componentsPerValue)
                {
                    throw EngineError("Animation weight target requires a MeshRenderer with matching morph targets.");
                }
            }
        }
    }
    m_animations = std::move(animations);
    m_nodes = std::move(nodes);
}
} // namespace ofg
