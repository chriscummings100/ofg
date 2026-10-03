// Entity tree node implementation for the OFG scene graph.
#include "scene/entity.h"


namespace ofg {

Entity::Entity(Scene* scene, EntityId id, Entity* parent) noexcept
    : m_scene(scene)
    , m_id(id)
    , m_parent(parent)
{
}

EntityId Entity::id() const noexcept
{
    return m_id;
}

LocalTransform& Entity::localTransform() noexcept
{
    return m_localTransform;
}

const LocalTransform& Entity::localTransform() const noexcept
{
    return m_localTransform;
}

Entity* Entity::parent() noexcept
{
    return m_parent;
}

const Entity* Entity::parent() const noexcept
{
    return m_parent;
}

Entity* Entity::firstChild() noexcept
{
    return m_firstChild;
}

const Entity* Entity::firstChild() const noexcept
{
    return m_firstChild;
}

Entity* Entity::nextSibling() noexcept
{
    return m_nextSibling;
}

const Entity* Entity::nextSibling() const noexcept
{
    return m_nextSibling;
}

void Entity::appendChild(Entity* child) noexcept
{
    if (m_firstChild == nullptr)
    {
        m_firstChild = child;
        m_lastChild = child;
        return;
    }
    m_lastChild->m_nextSibling = child;
    m_lastChild = child;
}

} // namespace ofg
