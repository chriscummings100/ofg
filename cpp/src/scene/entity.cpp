// Entity tree node implementation for the OFG scene graph.
#include "ofg/scene/entity.hpp"

#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/player.hpp"
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

// Returns this entity's camera, if one exists.
Camera* Entity::camera() noexcept {
    return m_camera;
}

// Returns this entity's camera, if one exists.
const Camera* Entity::camera() const noexcept {
    return m_camera;
}

// Returns this entity's player, if one exists.
Player* Entity::player() noexcept {
    return m_player;
}

// Returns this entity's player, if one exists.
const Player* Entity::player() const noexcept {
    return m_player;
}

// Returns this entity's animation player, if one exists.
AnimationPlayer* Entity::animation_player() noexcept {
    return m_animation_player;
}

// Returns this entity's animation player, if one exists.
const AnimationPlayer* Entity::animation_player() const noexcept {
    return m_animation_player;
}

// Returns this entity's light, if one exists.
Light* Entity::light() noexcept {
    return m_light;
}

// Returns this entity's light, if one exists.
const Light* Entity::light() const noexcept {
    return m_light;
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
