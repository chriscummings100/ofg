// Entity tree node for the OFG scene graph.
//
// Entity owns local transform data and tree links, while Scene owns entity
// allocation, lookup, and flat component storage. Entity pointers are stable for
// one Scene generation and invalidated by Scene::clear().
#pragma once

#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/component.hpp"

#include <cstdint>

namespace ofg {

class Camera;
class MeshRenderer;
class Player;
class Scene;

using EntityId = std::uint32_t;

struct LocalTransform {
    math::Vec3 m_position{0.0f, 0.0f, 0.0f};
    math::Quat m_rotation{math::quat_identity()};
    math::Vec3 m_scale{1.0f, 1.0f, 1.0f};
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
    // Returns this entity's camera, if one exists.
    [[nodiscard]] Camera* camera() noexcept;
    // Returns this entity's camera, if one exists.
    [[nodiscard]] const Camera* camera() const noexcept;
    // Returns this entity's player, if one exists.
    [[nodiscard]] Player* player() noexcept;
    // Returns this entity's player, if one exists.
    [[nodiscard]] const Player* player() const noexcept;

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
    Camera* m_camera{nullptr};
    Player* m_player{nullptr};
};

} // namespace ofg
