// Validates direct name/hierarchy correspondence before attaching shared clips to a different rig.
#include "scene/animation-binding.h"
#include "core/engine-error.h"
#include <unordered_map>
#include <unordered_set>
#include <limits>

namespace ofg {
std::vector<Ptr<Entity>> mapAnimationNodesByName(const ModelData& source, const ModelInstance& target)
{
    if (!target.root)
    {
        throw EngineError("Animation mapping requires a live target placement root.");
    }
    std::unordered_map<std::string, Entity*> targets;
    for (const auto& node : target.nodes)
    {
        if (node && !node->name().empty() && !targets.emplace(node->name(), node.get()).second)
        {
            throw EngineError("Duplicate animation target node name: '" + node->name() + "'.");
        }
    }
    std::unordered_set<std::string> sourceNames;
    std::vector<Ptr<Entity>> mapping(source.nodes.size());
    constexpr auto noParent = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> parents(source.nodes.size(), noParent);
    for (size_t index = 0; index < source.nodes.size(); ++index)
    {
        const auto& node = source.nodes[index];
        if (!node.name.empty())
        {
            if (!sourceNames.insert(node.name).second)
            {
                throw EngineError("Duplicate animation source node name: '" + node.name + "'.");
            }
            if (auto found = targets.find(node.name); found != targets.end())
            {
                mapping[index] = found->second;
            }
        }
        for (uint32_t child : node.children)
        {
            if (child >= parents.size() || parents[child] != noParent)
            {
                throw EngineError("Animation source hierarchy has an invalid or multiply-parented child.");
            }
            parents[child] = uint32_t(index);
        }
    }
    for (const auto& clip : source.animations)
    {
        if (!clip || !clip->isLoaded())
        {
            throw EngineError("Animation mapping requires ready source clips.");
        }
        for (const auto& track : clip->desc().tracks)
        {
            if (track.node >= mapping.size())
            {
                throw EngineError("Animation '" + clip->desc().name + "' target index is outside source nodes.");
            }
            if (!mapping[track.node])
            {
                throw EngineError(
                    "Animation '" + clip->desc().name + "' cannot map node " + std::to_string(track.node) + " ('" +
                    source.nodes[track.node].name + "')."
                );
            }
        }
    }
    for (size_t index = 0; index < mapping.size(); ++index)
    {
        if (!mapping[index])
        {
            continue;
        }
        const auto parent = parents[index];
        const Entity* expected = parent == noParent ? target.root.get() : mapping[parent].get();
        if (!expected || mapping[index]->parent() != expected)
        {
            throw EngineError("Animation hierarchy mismatch at '" + source.nodes[index].name + "'.");
        }
    }
    return mapping;
}
} // namespace ofg
