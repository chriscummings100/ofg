// Scene-owned entity hierarchy, stable allocation and local-to-world transforms.
// Concrete component storage is added alongside each component when needed.
#pragma once

#include "math/mat.h"
#include "scene/entity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {

class Scene
{
public:
    // Creates a scene with a single root entity.
    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    // Transfers storage without changing entity addresses; leaves other without a root.
    Scene(Scene&& other) noexcept;
    // Releases this scene's entities, then transfers and rebinds other's storage.
    Scene& operator=(Scene&& other) noexcept;

    // Returns the root, or null in a moved-from scene until clear() is called.
    [[nodiscard]] Entity* getRoot() noexcept;
    // Returns the root, or null in a moved-from scene until clear() is called.
    [[nodiscard]] const Entity* getRoot() const noexcept;
    // Looks up an ID in this generation; IDs are reused after clear().
    [[nodiscard]] Entity* getEntity(EntityId id) noexcept;
    // Looks up an ID in this generation; IDs are reused after clear().
    [[nodiscard]] const Entity* getEntity(EntityId id) const noexcept;
    // Creates a child under a live parent from this scene; rejects null/foreign parents.
    [[nodiscard]] Entity* createEntity(Entity* parent);
    // Counts all entities, including the root.
    [[nodiscard]] std::size_t entityCount() const noexcept;
    // Returns this scene's clear counter, not a globally unique lifetime identifier.
    [[nodiscard]] std::uint32_t generation() const noexcept;
    // Destroys entities, invalidates their Ptr observers and creates a fresh root with ID zero.
    void clear();

private:
    // Checks ownership of a live entity pointer; does not validate dangling raw pointers.
    [[nodiscard]] bool containsCurrentEntity(const Entity* entity) const noexcept;
    // Creates the root entity for the current generation.
    void createRootEntity();
    // Rebinds moved entity owner pointers to this scene.
    void rebindEntitiesAfterMove() noexcept;

    std::vector<std::unique_ptr<Entity>> m_entities;
    Entity* m_root{nullptr};
    EntityId m_nextEntityId{0};
    std::uint32_t m_generation{0};
};

// Composes translation * rotation * scale, mapping local points into parent space.
[[nodiscard]] math::Mat4 parentFromLocal(const LocalTransform& transform) noexcept;
// Recursively composes ancestor transforms, including the root's own transform.
[[nodiscard]] math::Mat4 worldFromLocal(const Entity& entity) noexcept;

} // namespace ofg
