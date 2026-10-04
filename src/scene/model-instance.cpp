// Builds complete model hierarchies in staging storage, then commits stable allocations to a Scene.
#include "scene/scene.h"
#include "core/engine-error.h"
#include <limits>

namespace ofg {
ModelInstance Scene::instantiateModel(std::shared_ptr<Model> model, Entity* parent, std::optional<uint32_t> sceneIndex)
{
    if (!containsCurrentEntity(parent))
    {
        throw EngineError("Model instance requires a live parent from this scene.");
    }
    if (!model)
    {
        throw EngineError("Model instance requires a Model.");
    }
    const auto& data = model->data();
    const uint32_t selected = sceneIndex.value_or(data.defaultScene.value_or(0));
    if (selected >= data.scenes.size())
    {
        throw EngineError("Model scene index is out of range (or the model has no scenes).");
    }

    Scene staging;
    ModelInstance instance;
    instance.model = std::move(model);
    instance.root = staging.getRoot();
    instance.nodes.resize(data.nodes.size());
    std::vector<std::pair<uint32_t, Entity*>> pending;
    // Reverse pushes preserve source sibling order with a simple depth-first stack.
    for (auto root = data.scenes[selected].roots.rbegin(); root != data.scenes[selected].roots.rend(); ++root)
    {
        pending.emplace_back(*root, staging.getRoot());
    }
    while (!pending.empty())
    {
        const auto [index, parentEntity] = pending.back();
        pending.pop_back();
        const auto& node = data.nodes[index];
        auto* entity = staging.createEntity(parentEntity);
        entity->setName(node.name);
        entity->setLocalTransform(node.localTransform);
        instance.nodes[index] = entity;
        for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
        {
            pending.emplace_back(*child, entity);
        }
    }

    // All source nodes now have stable instance addresses, independent of skin reference order.
    for (size_t index = 0; index < data.nodes.size(); ++index)
    {
        auto* entity = instance.nodes[index].get();
        const auto& node = data.nodes[index];
        if (!entity || !node.mesh)
        {
            continue;
        }
        auto* renderer = staging.createMeshRenderer(entity);
        renderer->setMesh(data.meshes[*node.mesh]);
        if (node.morphWeights)
        {
            renderer->setMorphWeights(*node.morphWeights);
        }
        if (node.skin)
        {
            const auto& skin = data.skins[*node.skin];
            std::vector<Ptr<Entity>> joints;
            joints.reserve(skin->desc().joints.size());
            for (const auto& joint : skin->desc().joints)
            {
                joints.push_back(instance.nodes[joint.node]);
            }
            renderer->setSkin(skin, std::move(joints));
        }
    }
    bool animated = false;
    for (const auto& animation : data.animations)
    {
        for (const auto& track : animation->desc().tracks)
        {
            animated |= bool(instance.nodes[track.node]);
        }
    }
    if (animated)
    {
        auto* animator = staging.createAnimator(staging.getRoot());
        animator->setBindings(data.animations, instance.nodes);
        instance.animator = animator;
    }

    // Reserve every destination before publication. The remaining pointer/storage moves cannot allocate.
    if (staging.m_entities.size() > std::numeric_limits<EntityId>::max() - m_nextEntityId)
    {
        throw EngineError("Model instance exceeds the scene entity ID domain.");
    }
    m_entities.reserve(m_entities.size() + staging.m_entities.size());
    m_meshRenderers.reserve(m_meshRenderers.size() + staging.m_meshRenderers.size());
    m_animators.reserve(m_animators.size() + staging.m_animators.size());
    for (auto& entity : staging.m_entities)
    {
        entity->m_scene = this;
        entity->m_id = m_nextEntityId++;
        m_entities.push_back(std::move(entity));
    }
    for (auto& renderer : staging.m_meshRenderers)
    {
        m_meshRenderers.push_back(std::move(renderer));
    }
    for (auto& animator : staging.m_animators)
    {
        m_animators.push_back(std::move(animator));
    }
    instance.root->m_parent = parent;
    parent->appendChild(instance.root.get());
    instance.root->invalidateWorldTransform();
    return instance;
}
} // namespace ofg
