// Scene-owned entity hierarchy and transform composition.
#include "scene/scene.h"
#include "core/engine-error.h"
#include "math/transform.h"

#include <utility>

namespace ofg {

Scene::Scene()
{
    createRootEntity();
}

Scene::Scene(Scene&& other) noexcept
    : m_entities(std::move(other.m_entities))
    , m_root(other.m_root)
    , m_nextEntityId(other.m_nextEntityId)
    , m_generation(other.m_generation)
{
    rebindEntitiesAfterMove();
    other.m_root = nullptr;
    other.m_nextEntityId = 0;
}

Scene& Scene::operator=(Scene&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_entities = std::move(other.m_entities);
    m_root = other.m_root;
    m_nextEntityId = other.m_nextEntityId;
    m_generation = other.m_generation;
    rebindEntitiesAfterMove();
    other.m_root = nullptr;
    other.m_nextEntityId = 0;
    return *this;
}

Entity* Scene::getRoot() noexcept
{
    return m_root;
}

const Entity* Scene::getRoot() const noexcept
{
    return m_root;
}

Entity* Scene::getEntity(EntityId id) noexcept
{
    if (id >= m_entities.size())
    {
        return nullptr;
    }
    return m_entities[id].get();
}

const Entity* Scene::getEntity(EntityId id) const noexcept
{
    if (id >= m_entities.size())
    {
        return nullptr;
    }
    return m_entities[id].get();
}

Entity* Scene::createEntity(Entity* parent)
{
    if (!containsCurrentEntity(parent))
    {
        throw EngineError("Scene::createEntity requires a non-null parent from the same scene.");
    }

    EntityId id = m_nextEntityId;
    m_entities.push_back(std::unique_ptr<Entity>(new Entity(this, id, parent)));
    m_nextEntityId += 1;
    Entity* entity = m_entities.back().get();
    parent->appendChild(entity);
    return entity;
}

std::size_t Scene::entityCount() const noexcept
{
    return m_entities.size();
}

std::uint32_t Scene::generation() const noexcept
{
    return m_generation;
}

void Scene::clear()
{
    m_entities.clear();
    m_root = nullptr;
    m_nextEntityId = 0;
    m_generation += 1;
    createRootEntity();
}

bool Scene::containsCurrentEntity(const Entity* entity) const noexcept
{
    if (entity == nullptr || entity->m_scene != this)
    {
        return false;
    }
    const Entity* current = getEntity(entity->m_id);
    return current == entity;
}

void Scene::createRootEntity()
{
    const EntityId id = m_nextEntityId;
    m_entities.push_back(std::unique_ptr<Entity>(new Entity(this, id, nullptr)));
    m_nextEntityId += 1;
    m_root = m_entities.back().get();
}

void Scene::rebindEntitiesAfterMove() noexcept
{
    m_root = m_entities.empty() ? nullptr : m_entities[0].get();
    for (std::unique_ptr<Entity>& entity : m_entities)
    {
        entity->m_scene = this;
    }
}

math::Mat4 parentFromLocal(const LocalTransform& transform) noexcept
{
    const math::Mat4 translation = math::mat4Translation(transform.position);
    const math::Mat4 rotation = math::mat4FromQuat(transform.rotation);
    const math::Mat4 scale = math::mat4Scale(transform.scale);
    return math::mul(math::mul(translation, rotation), scale);
}

math::Mat4 worldFromLocal(const Entity& entity) noexcept
{
    const math::Mat4 local = parentFromLocal(entity.localTransform());
    const Entity* parent = entity.parent();
    if (parent == nullptr)
    {
        return local;
    }
    return math::mul(worldFromLocal(*parent), local);
}

} // namespace ofg
