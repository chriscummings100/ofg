// Entity/component scene graph passed from Game into Renderer.
//
// Scene owns a root entity, a tree of child entities, and pointer-stable
// component storage. Renderer iterates MeshRenderer components by index and
// resolves each owning entity's world transform into a transient draw list.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/player_animation_controller.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {

struct SceneUpdateContext;

struct DirectionalLight {
    math::Vec3 m_direction{0.0f, -1.0f, 0.0f};
    math::Vec3 m_color{1.0f, 1.0f, 1.0f};
    float m_intensity{1.0f};
};

struct AmbientLight {
    math::Vec3 m_color{1.0f, 1.0f, 1.0f};
    float m_intensity{0.08f};
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
    // Reports the number of camera components in creation order.
    [[nodiscard]] std::size_t camera_count() const noexcept;
    // Returns one camera by creation-order index.
    [[nodiscard]] Camera* get_camera(std::size_t index) noexcept;
    // Returns one camera by creation-order index.
    [[nodiscard]] const Camera* get_camera(std::size_t index) const noexcept;
    // Returns the explicit main camera or the first camera when none is selected.
    [[nodiscard]] Camera* main_camera() noexcept;
    // Returns the explicit main camera or the first camera when none is selected.
    [[nodiscard]] const Camera* main_camera() const noexcept;
    // Replaces the explicit main camera selection, or clears it for first-camera fallback.
    void set_main_camera(Camera* camera);
    // Returns the main directional light used by the first PBR renderer path.
    [[nodiscard]] const DirectionalLight& main_light() const noexcept;
    // Replaces the main directional light after normalizing its direction.
    void set_main_light(DirectionalLight light);
    // Returns the ambient light term used by the first PBR renderer path.
    [[nodiscard]] const AmbientLight& ambient_light() const noexcept;
    // Replaces the ambient light term.
    void set_ambient_light(AmbientLight light);
    // Reports the number of player components in creation order.
    [[nodiscard]] std::size_t player_count() const noexcept;
    // Returns one player by creation-order index.
    [[nodiscard]] Player* get_player(std::size_t index) noexcept;
    // Returns one player by creation-order index.
    [[nodiscard]] const Player* get_player(std::size_t index) const noexcept;
    // Reports the number of player animation controller components in creation order.
    [[nodiscard]] std::size_t player_animation_controller_count() const noexcept;
    // Returns one player animation controller by creation-order index.
    [[nodiscard]] PlayerAnimationController* get_player_animation_controller(std::size_t index) noexcept;
    // Returns one player animation controller by creation-order index.
    [[nodiscard]] const PlayerAnimationController* get_player_animation_controller(std::size_t index) const noexcept;
    // Reports the number of animation-player components in creation order.
    [[nodiscard]] std::size_t animation_player_count() const noexcept;
    // Returns one animation player by creation-order index.
    [[nodiscard]] AnimationPlayer* get_animation_player(std::size_t index) noexcept;
    // Returns one animation player by creation-order index.
    [[nodiscard]] const AnimationPlayer* get_animation_player(std::size_t index) const noexcept;
    // Updates scene-owned gameplay and camera components in deterministic order.
    void update(const SceneUpdateContext& context);
    // Returns the generation token invalidated by clear().
    [[nodiscard]] std::uint32_t generation() const noexcept;

    // Clears all entities/components and creates a fresh root.
    void clear();

private:
    friend class Entity;

    // Creates a component in scene-owned storage for an entity.
    [[nodiscard]] Component* create_component(Entity& entity, ComponentType type);
    // Returns whether an entity pointer belongs to the current scene generation.
    [[nodiscard]] bool contains_current_entity(const Entity* entity) const noexcept;
    // Returns whether a camera pointer belongs to current scene-owned storage.
    [[nodiscard]] bool contains_current_camera(const Camera* camera) const noexcept;
    // Creates the root entity for the current generation.
    void create_root_entity();
    // Rebinds moved entity owner pointers to this scene.
    void rebind_entities_after_move() noexcept;

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::vector<std::unique_ptr<MeshRenderer>> m_mesh_renderers;
    std::vector<std::unique_ptr<Camera>> m_cameras;
    std::vector<std::unique_ptr<Player>> m_players;
    std::vector<std::unique_ptr<PlayerAnimationController>> m_player_animation_controllers;
    std::vector<std::unique_ptr<AnimationPlayer>> m_animation_players;
    Ptr<Camera> m_main_camera;
    DirectionalLight m_main_light;
    AmbientLight m_ambient_light;
    std::vector<math::Mat4> m_world_transform_cache;
    Entity* m_root{nullptr};
    EntityId m_next_entity_id{0};
    std::uint32_t m_generation{0};
};

// Builds a matrix that transforms local points into the parent entity space.
[[nodiscard]] math::Mat4 parent_from_local(const LocalTransform& transform) noexcept;

// Builds a matrix that transforms local points into world/root space.
[[nodiscard]] math::Mat4 world_from_local(const Entity& entity) noexcept;

} // namespace ofg
