// Entity tree node implementation for the OFG scene graph.
#include "scene/entity.h"
#include "scene/scene.h"
#include "core/engine-error.h"
#include <cmath>


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

const LocalTransform& Entity::localTransform() const noexcept
{
    return m_localTransform;
}

void Entity::setLocalTransform(const LocalTransform& transform)
{
    for (float value :
         {transform.position.x,
          transform.position.y,
          transform.position.z,
          transform.scale.x,
          transform.scale.y,
          transform.scale.z})
    {
        if (!std::isfinite(value))
        {
            throw EngineError("Entity position and scale must be finite.");
        }
    }
    std::string error;
    auto rotation = math::normalize(transform.rotation, error);
    if (!rotation)
    {
        throw EngineError(error);
    }
    m_localTransform = transform;
    m_localTransform.rotation = *rotation;
    invalidateWorldTransform();
}

void Entity::setLocalPosition(math::Vec3 position)
{
    auto transform = m_localTransform;
    transform.position = position;
    setLocalTransform(transform);
}

void Entity::setLocalRotation(math::Quat rotation)
{
    auto transform = m_localTransform;
    transform.rotation = rotation;
    setLocalTransform(transform);
}

void Entity::setLocalScale(math::Vec3 scale)
{
    auto transform = m_localTransform;
    transform.scale = scale;
    setLocalTransform(transform);
}

void Entity::invalidateWorldTransform() noexcept
{
    if (m_worldDirty)
    {
        return;
    }
    m_worldDirty = true;
    for (Entity* child = m_firstChild; child; child = child->m_nextSibling)
    {
        child->invalidateWorldTransform();
    }
}

const math::Mat4& Entity::worldTransform() const noexcept
{
    if (m_worldDirty)
    {
        const auto local = parentFromLocal(m_localTransform);
        m_worldTransform = m_parent ? math::mul(m_parent->worldTransform(), local) : local;
        m_worldDirty = false;
    }
    return m_worldTransform;
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
