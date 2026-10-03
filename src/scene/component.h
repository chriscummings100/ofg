// Base component contract for scene-owned entity components.
//
// Concrete components and their typed Scene storage are added when needed.
// This base only observes an entity; it never owns or extends its lifetime.
#pragma once

#include "core/object.h"
#include "core/ptr.h"

namespace ofg {

class Entity;

class Component : public Object
{
public:
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    // Returns the associated entity, or null after it is destroyed.
    [[nodiscard]] Entity* entity() noexcept;
    // Returns the associated entity, or null after it is destroyed.
    [[nodiscard]] const Entity* entity() const noexcept;

protected:
    // Binds a component to one scene-owned entity.
    explicit Component(Entity* entity) noexcept;
    // Unregisters the entity observer and invalidates observers of this component.
    ~Component() override = default;

private:
    Ptr<Entity> m_entity;
};

} // namespace ofg
