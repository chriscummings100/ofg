// Base component contract implementation for scene-owned entity components.
#include "scene/component.h"

#include "scene/entity.h"

namespace ofg {

Component::Component(Entity* entity) noexcept
    : m_entity(entity)
{
}

Entity* Component::entity() noexcept
{
    return m_entity.get();
}

const Entity* Component::entity() const noexcept
{
    return m_entity.get();
}

} // namespace ofg
