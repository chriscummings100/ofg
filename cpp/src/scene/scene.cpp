// Entity/component scene graph implementation.
#include "ofg/scene/scene.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace ofg {

// Binds a component to one scene-owned entity.
Component::Component(ComponentType type, Entity* entity) noexcept : m_type(type), m_entity(entity) {}

// Reports this component's concrete component type.
ComponentType Component::type() const noexcept {
    return m_type;
}

// Returns the entity that owns this component.
Entity* Component::entity() noexcept {
    return m_entity;
}

// Returns the entity that owns this component.
const Entity* Component::entity() const noexcept {
    return m_entity;
}

// Binds this mesh renderer to one scene-owned entity.
MeshRenderer::MeshRenderer(Entity* entity) noexcept : Component(ComponentType::MeshRenderer, entity) {}

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

// Creates a scene with a single root entity.
Scene::Scene() {
    create_root_entity();
}

// Moves scene storage and rebinds moved entity owner pointers.
Scene::Scene(Scene&& other) noexcept
    : m_main_view(other.m_main_view), m_entities(std::move(other.m_entities)),
      m_mesh_renderers(std::move(other.m_mesh_renderers)), m_root(other.m_root),
      m_next_entity_id(other.m_next_entity_id), m_generation(other.m_generation) {
    rebind_entities_after_move();
    other.m_root = nullptr;
    other.m_next_entity_id = 0;
}

// Moves scene storage and rebinds moved entity owner pointers.
Scene& Scene::operator=(Scene&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    m_main_view = other.m_main_view;
    m_entities = std::move(other.m_entities);
    m_mesh_renderers = std::move(other.m_mesh_renderers);
    m_root = other.m_root;
    m_next_entity_id = other.m_next_entity_id;
    m_generation = other.m_generation;
    rebind_entities_after_move();
    other.m_root = nullptr;
    other.m_next_entity_id = 0;
    return *this;
}

// Returns the scene root entity.
Entity* Scene::get_root() noexcept {
    return m_root;
}

// Returns the scene root entity.
const Entity* Scene::get_root() const noexcept {
    return m_root;
}

// Finds an entity by id, or nullptr for an invalid id.
Entity* Scene::get_entity(EntityId id) noexcept {
    if (id >= m_entities.size()) {
        return nullptr;
    }
    return m_entities[id].get();
}

// Finds an entity by id, or nullptr for an invalid id.
const Entity* Scene::get_entity(EntityId id) const noexcept {
    if (id >= m_entities.size()) {
        return nullptr;
    }
    return m_entities[id].get();
}

// Creates a child entity under a parent from this scene.
Entity* Scene::create_entity(Entity* parent) {
    if (!contains_current_entity(parent)) {
        throw EngineError("Scene::create_entity requires a non-null parent from the same scene.");
    }

    EntityId id = m_next_entity_id;
    m_next_entity_id += 1;
    m_entities.push_back(std::unique_ptr<Entity>(new Entity(this, id, parent)));
    Entity* entity = m_entities.back().get();
    parent->append_child(entity);
    return entity;
}

// Reports the number of entities in this scene, including the root.
std::size_t Scene::entity_count() const noexcept {
    return m_entities.size();
}

// Reports the number of mesh renderer components in creation order.
std::size_t Scene::mesh_renderer_count() const noexcept {
    return m_mesh_renderers.size();
}

// Returns one mesh renderer by creation-order index.
MeshRenderer* Scene::get_mesh_renderer(std::size_t index) noexcept {
    if (index >= m_mesh_renderers.size()) {
        return nullptr;
    }
    return m_mesh_renderers[index].get();
}

// Returns one mesh renderer by creation-order index.
const MeshRenderer* Scene::get_mesh_renderer(std::size_t index) const noexcept {
    if (index >= m_mesh_renderers.size()) {
        return nullptr;
    }
    return m_mesh_renderers[index].get();
}

// Returns the generation token invalidated by clear().
std::uint32_t Scene::generation() const noexcept {
    return m_generation;
}

// Returns the scene's main render view.
const RenderView& Scene::main_view() const noexcept {
    return m_main_view;
}

// Replaces the scene's main render view.
void Scene::set_main_view(RenderView main_view) noexcept {
    m_main_view = main_view;
}

// Clears all entities/components and creates a fresh root.
void Scene::clear() {
    m_mesh_renderers.clear();
    m_entities.clear();
    m_root = nullptr;
    m_next_entity_id = 0;
    m_main_view = render_view_from_matrix(math::mat4_identity());
    m_generation += 1;
    create_root_entity();
}

// Creates a component in scene-owned storage for an entity.
Component* Scene::create_component(Entity& entity, ComponentType type) {
    if (!contains_current_entity(&entity)) {
        throw EngineError("Scene component creation requires an entity from the same scene.");
    }

    switch (type) {
    case ComponentType::MeshRenderer:
        if (entity.m_mesh_renderer != nullptr) {
            throw EngineError("Entity already has a MeshRenderer component.");
        }
        m_mesh_renderers.push_back(std::make_unique<MeshRenderer>(&entity));
        entity.m_mesh_renderer = m_mesh_renderers.back().get();
        return entity.m_mesh_renderer;
    }

    throw EngineError("Scene cannot create an unknown component type.");
}

// Returns whether an entity pointer belongs to the current scene generation.
bool Scene::contains_current_entity(const Entity* entity) const noexcept {
    if (entity == nullptr || entity->m_scene != this) {
        return false;
    }
    const Entity* current = get_entity(entity->m_id);
    return current == entity;
}

// Creates the root entity for the current generation.
void Scene::create_root_entity() {
    const EntityId id = m_next_entity_id;
    m_next_entity_id += 1;
    m_entities.push_back(std::unique_ptr<Entity>(new Entity(this, id, nullptr)));
    m_root = m_entities.back().get();
}

// Rebinds moved entity owner pointers to this scene.
void Scene::rebind_entities_after_move() noexcept {
    m_root = m_entities.empty() ? nullptr : m_entities[0].get();
    for (std::unique_ptr<Entity>& entity : m_entities) {
        entity->m_scene = this;
    }
}

// Builds a matrix that transforms local points into the parent entity space.
math::Mat4 parent_from_local(const LocalTransform& transform) noexcept {
    const math::Mat4 translation = math::mat4_translation(transform.m_position);
    const math::Mat4 rotation = math::mat4_from_quat(transform.m_rotation);
    const math::Mat4 scale = math::mat4_scale(transform.m_scale);
    return math::mul(math::mul(translation, rotation), scale);
}

// Builds a matrix that transforms local points into world/root space.
math::Mat4 world_from_local(const Entity& entity) noexcept {
    const math::Mat4 local = parent_from_local(entity.local_transform());
    const Entity* parent = entity.parent();
    if (parent == nullptr) {
        return local;
    }
    return math::mul(world_from_local(*parent), local);
}

} // namespace ofg
