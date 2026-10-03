// Entity tree node for the OFG scene graph.
//
// Entity owns local transform data and tree links, while Scene owns entity
// allocation and lookup. Entity pointers are stable for
// one Scene generation and invalidated by Scene::clear().
#pragma once

#include "core/object.h"
#include "math/quat.h"
#include "math/vec.h"

#include <cstdint>

namespace ofg {

class Scene;

using EntityId = std::uint32_t;

struct LocalTransform
{
    math::Vec3 position{0.0f, 0.0f, 0.0f};
    math::Quat rotation{math::quatIdentity()};
    math::Vec3 scale{1.0f, 1.0f, 1.0f};
};

class Entity : public Object
{
public:
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) = delete;
    Entity& operator=(Entity&&) = delete;

    // Returns this entity's stable id within its owning scene generation.
    [[nodiscard]] EntityId id() const noexcept;
    // Returns the mutable local transform from this entity into its parent.
    [[nodiscard]] LocalTransform& localTransform() noexcept;
    // Returns the local transform from this entity into its parent.
    [[nodiscard]] const LocalTransform& localTransform() const noexcept;

    // Returns this entity's parent, or nullptr for the root.
    [[nodiscard]] Entity* parent() noexcept;
    // Returns this entity's parent, or nullptr for the root.
    [[nodiscard]] const Entity* parent() const noexcept;
    // Returns this entity's first child in creation order.
    [[nodiscard]] Entity* firstChild() noexcept;
    // Returns this entity's first child in creation order.
    [[nodiscard]] const Entity* firstChild() const noexcept;
    // Returns this entity's next sibling in creation order.
    [[nodiscard]] Entity* nextSibling() noexcept;
    // Returns this entity's next sibling in creation order.
    [[nodiscard]] const Entity* nextSibling() const noexcept;

private:
    friend class Scene;

    // Creates an entity owned by one scene generation.
    Entity(Scene* scene, EntityId id, Entity* parent) noexcept;
    // Appends a child entity in stable sibling order.
    void appendChild(Entity* child) noexcept;

    Scene* m_scene{nullptr};
    EntityId m_id{0};
    LocalTransform m_localTransform;
    Entity* m_parent{nullptr};
    Entity* m_firstChild{nullptr};
    Entity* m_lastChild{nullptr};
    Entity* m_nextSibling{nullptr};
};

} // namespace ofg
