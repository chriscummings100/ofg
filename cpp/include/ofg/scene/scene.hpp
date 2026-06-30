// Entity/component scene graph passed from Game into Renderer.
//
// Scene owns a root entity, a tree of child entities, and pointer-stable
// component storage. Renderer iterates MeshRenderer components by index and
// resolves each owning entity's world transform into a transient draw list.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/property_bag.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {

class Mesh;
class Scene;

using EntityId = std::uint32_t;

enum class ComponentType {
    MeshRenderer,
};

struct LocalTransform {
    math::Vec3 m_position{0.0f, 0.0f, 0.0f};
    math::Quat m_rotation{math::quat_identity()};
    math::Vec3 m_scale{1.0f, 1.0f, 1.0f};
};

class Entity;

class Component {
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
    ~Component() = default;

private:
    ComponentType m_type;
    Entity* m_entity{nullptr};
};

class MeshRenderer final : public Component {
public:
    // Binds this mesh renderer to one scene-owned entity.
    explicit MeshRenderer(Entity* entity) noexcept;

    Mesh* m_mesh{nullptr};
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin_offset{0.0f, 0.0f, 0.0f};
};

class Entity {
public:
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) = delete;
    Entity& operator=(Entity&&) = delete;

    // Returns this entity's stable id within its owning scene generation.
    [[nodiscard]] EntityId id() const noexcept;
    // Returns the mutable local transform from this entity into its parent.
    [[nodiscard]] LocalTransform& local_transform() noexcept;
    // Returns the local transform from this entity into its parent.
    [[nodiscard]] const LocalTransform& local_transform() const noexcept;

    // Returns this entity's parent, or nullptr for the root.
    [[nodiscard]] Entity* parent() noexcept;
    // Returns this entity's parent, or nullptr for the root.
    [[nodiscard]] const Entity* parent() const noexcept;
    // Returns this entity's first child in creation order.
    [[nodiscard]] Entity* first_child() noexcept;
    // Returns this entity's first child in creation order.
    [[nodiscard]] const Entity* first_child() const noexcept;
    // Returns this entity's next sibling in creation order.
    [[nodiscard]] Entity* next_sibling() noexcept;
    // Returns this entity's next sibling in creation order.
    [[nodiscard]] const Entity* next_sibling() const noexcept;

    // Creates a component of the requested type on this entity.
    [[nodiscard]] Component* create_component(ComponentType type);
    // Returns this entity's mesh renderer, if one exists.
    [[nodiscard]] MeshRenderer* mesh_renderer() noexcept;
    // Returns this entity's mesh renderer, if one exists.
    [[nodiscard]] const MeshRenderer* mesh_renderer() const noexcept;

private:
    friend class Scene;

    // Creates an entity owned by one scene generation.
    Entity(Scene* scene, EntityId id, Entity* parent) noexcept;
    // Appends a child entity in stable sibling order.
    void append_child(Entity* child) noexcept;

    Scene* m_scene{nullptr};
    EntityId m_id{0};
    LocalTransform m_local_transform;
    Entity* m_parent{nullptr};
    Entity* m_first_child{nullptr};
    Entity* m_last_child{nullptr};
    Entity* m_next_sibling{nullptr};
    MeshRenderer* m_mesh_renderer{nullptr};
};

class Scene {
public:
    Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) noexcept;
    Scene& operator=(Scene&& other) noexcept;

    // Returns the scene root entity.
    [[nodiscard]] Entity* get_root() noexcept;
    // Returns the scene root entity.
    [[nodiscard]] const Entity* get_root() const noexcept;
    // Finds an entity by id, or nullptr for an invalid id.
    [[nodiscard]] Entity* get_entity(EntityId id) noexcept;
    // Finds an entity by id, or nullptr for an invalid id.
    [[nodiscard]] const Entity* get_entity(EntityId id) const noexcept;
    // Creates a child entity under a parent from this scene.
    [[nodiscard]] Entity* create_entity(Entity* parent);

    // Reports the number of entities in this scene, including the root.
    [[nodiscard]] std::size_t entity_count() const noexcept;
    // Reports the number of mesh renderer components in creation order.
    [[nodiscard]] std::size_t mesh_renderer_count() const noexcept;
    // Returns one mesh renderer by creation-order index.
    [[nodiscard]] MeshRenderer* get_mesh_renderer(std::size_t index) noexcept;
    // Returns one mesh renderer by creation-order index.
    [[nodiscard]] const MeshRenderer* get_mesh_renderer(std::size_t index) const noexcept;
    // Returns the generation token invalidated by clear().
    [[nodiscard]] std::uint32_t generation() const noexcept;

    // Returns the scene's main render view.
    [[nodiscard]] const RenderView& main_view() const noexcept;
    // Replaces the scene's main render view.
    void set_main_view(RenderView main_view) noexcept;
    // Clears all entities/components and creates a fresh root.
    void clear();

private:
    friend class Entity;

    // Creates a component in scene-owned storage for an entity.
    [[nodiscard]] Component* create_component(Entity& entity, ComponentType type);
    // Returns whether an entity pointer belongs to the current scene generation.
    [[nodiscard]] bool contains_current_entity(const Entity* entity) const noexcept;
    // Creates the root entity for the current generation.
    void create_root_entity();
    // Rebinds moved entity owner pointers to this scene.
    void rebind_entities_after_move() noexcept;

    RenderView m_main_view{render_view_from_matrix(math::mat4_identity())};
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::vector<std::unique_ptr<MeshRenderer>> m_mesh_renderers;
    Entity* m_root{nullptr};
    EntityId m_next_entity_id{0};
    std::uint32_t m_generation{0};
};

// Builds a matrix that transforms local points into the parent entity space.
[[nodiscard]] math::Mat4 parent_from_local(const LocalTransform& transform) noexcept;

// Builds a matrix that transforms local points into world/root space.
[[nodiscard]] math::Mat4 world_from_local(const Entity& entity) noexcept;

} // namespace ofg
