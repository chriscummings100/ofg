// Base component contract implementation for scene-owned entity components.
#include "ofg/scene/component.hpp"

#include "ofg/scene/entity.hpp"

namespace ofg {

// Binds a component to one scene-owned entity.
Component::Component(ComponentType type, Entity* entity) noexcept : m_type(type), m_entity(entity) {}

// Reports this component's concrete component type.
ComponentType Component::type() const noexcept {
    return m_type;
}

// Returns the entity that owns this component.
Entity* Component::entity() noexcept {
    return m_entity.get();
}

// Returns the entity that owns this component.
const Entity* Component::entity() const noexcept {
    return m_entity.get();
}

} // namespace ofg
