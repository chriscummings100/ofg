// Entity/component scene graph passed from Game into Renderer.
//
// Scene owns a root entity, a tree of child entities, and pointer-stable
// component storage. Renderer iterates MeshRenderer components by index and
// resolves each owning entity's world transform into a transient draw list.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/mesh_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {

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
