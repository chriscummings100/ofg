// Entity tree node implementation for the OFG scene graph.
#include "ofg/scene/entity.hpp"

#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"

namespace ofg {

// Creates an entity owned by one scene generation.
Entity::Entity(Scene* scene, EntityId id, Entity* parent) noexcept : m_scene(scene), m_id(id), m_parent(parent) {}

// Returns this entity's stable id within its owning scene generation.
EntityId Entity::id() const noexcept {
    return m_id;
}

// Returns the mutable local transform from this entity into its parent.
LocalTransform& Entity::local_transform() noexcept {
    return m_local_transform;
}

// Returns the local transform from this entity into its parent.
const LocalTransform& Entity::local_transform() const noexcept {
    return m_local_transform;
}

// Returns this entity's parent, or nullptr for the root.
Entity* Entity::parent() noexcept {
    return m_parent;
}

// Returns this entity's parent, or nullptr for the root.
const Entity* Entity::parent() const noexcept {
    return m_parent;
}

// Returns this entity's first child in creation order.
Entity* Entity::first_child() noexcept {
    return m_first_child;
}

// Returns this entity's first child in creation order.
const Entity* Entity::first_child() const noexcept {
    return m_first_child;
}

// Returns this entity's next sibling in creation order.
Entity* Entity::next_sibling() noexcept {
    return m_next_sibling;
}

// Returns this entity's next sibling in creation order.
const Entity* Entity::next_sibling() const noexcept {
    return m_next_sibling;
}

// Creates a component of the requested type on this entity.
Component* Entity::create_component(ComponentType type) {
    return m_scene->create_component(*this, type);
}

// Returns this entity's mesh renderer, if one exists.
MeshRenderer* Entity::mesh_renderer() noexcept {
    return m_mesh_renderer;
}

// Returns this entity's mesh renderer, if one exists.
const MeshRenderer* Entity::mesh_renderer() const noexcept {
    return m_mesh_renderer;
}

// Appends a child entity in stable sibling order.
void Entity::append_child(Entity* child) noexcept {
    if (m_first_child == nullptr) {
        m_first_child = child;
        m_last_child = child;
        return;
    }
    m_last_child->m_next_sibling = child;
    m_last_child = child;
}

} // namespace ofg
