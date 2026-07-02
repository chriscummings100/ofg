// Base component contract for scene-owned entity components.
//
// Components are allocated and owned by Scene storage. Entity keeps typed
// non-owning pointers to components for the simple v1 API, while Renderer and
// later systems can iterate Scene's flat component containers.
#pragma once

#include "ofg/core/object.hpp"
#include "ofg/core/ptr.hpp"

namespace ofg {

class Entity;

enum class ComponentType {
    MeshRenderer,
    Camera,
    Player,
    PlayerAnimationController,
    AnimationPlayer,
};

class Component : public Object {
public:
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    // Reports this component's concrete component type.
    [[nodiscard]] ComponentType type() const noexcept;
    // Returns the entity that owns this component.
    [[nodiscard]] Entity* entity() noexcept;
    // Returns the entity that owns this component.
    [[nodiscard]] const Entity* entity() const noexcept;

protected:
    // Binds a component to one scene-owned entity.
    Component(ComponentType type, Entity* entity) noexcept;
    ~Component() override = default;

private:
    ComponentType m_type;
    Ptr<Entity> m_entity;
};

} // namespace ofg
