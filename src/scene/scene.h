// Scene-owned entity hierarchy, stable allocation and local-to-world transforms.
// Typed component allocations stay stable and are destroyed before entities.
#pragma once

#include "math/mat.h"
#include "scene/lighting.h"
#include "scene/entity.h"
#include "scene/camera.h"
#include "scene/mesh-renderer.h"
#include "scene/animator.h"
#include "scene/model-instance.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {

class Scene
{
public:
    Lighting lighting; // Explicit scene-owned lighting, reset by clear and transferred on move.
    // Creates a scene with a single root entity.
    Scene();
    // Destroys components before their entities.
    ~Scene() = default;
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
    // Creates one camera on a live entity in this scene; duplicates/foreign entities throw.
    Camera* createCamera(Entity* entity);
    // Creates one renderer on a live entity in this scene; duplicates/foreign entities throw.
    MeshRenderer* createMeshRenderer(Entity* entity);
    // Creates one passive Animator on a live entity in this scene; duplicates/foreign entities throw.
    Animator* createAnimator(Entity* entity);
    // Stages a loaded model under a live parent, selecting explicit/default/first scene.
    // Failure leaves existing entities/components untouched. Clips and deformation remain passive.
    ModelInstance instantiateModel(
        std::shared_ptr<Model> model,
        Entity* parent,
        std::optional<uint32_t> sceneIndex = {}
    );
    // Returns stable camera allocations in creation order without exposing ownership mutation.
    std::span<const std::unique_ptr<Camera>> cameras() const noexcept { return m_cameras; }
    // Returns stable renderer allocations in creation order without exposing ownership mutation.
    std::span<const std::unique_ptr<MeshRenderer>> meshRenderers() const noexcept { return m_meshRenderers; }
    // Returns stable Animator allocations without exposing ownership mutation.
    std::span<const std::unique_ptr<Animator>> animators() const noexcept { return m_animators; }
    // Selects a camera from this scene, or null to render only the background.
    void setActiveCamera(Camera* camera);
    // Returns the selected camera, if any.
    Camera* activeCamera() noexcept { return m_activeCamera.get(); }
    // Returns the selected camera, if any.
    const Camera* activeCamera() const noexcept { return m_activeCamera.get(); }
    // Refreshes world transforms; concrete components have no per-frame behavior hooks.
    void update();

private:
    // Checks ownership of a live entity pointer; does not validate dangling raw pointers.
    [[nodiscard]] bool containsCurrentEntity(const Entity* entity) const noexcept;
    // Creates the root entity for the current generation.
    void createRootEntity();
    // Rebinds moved entity owner pointers to this scene.
    void rebindEntitiesAfterMove() noexcept;

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::vector<std::unique_ptr<Camera>> m_cameras;
    std::vector<std::unique_ptr<MeshRenderer>> m_meshRenderers;
    std::vector<std::unique_ptr<Animator>> m_animators;
    Ptr<Camera> m_activeCamera;
    Entity* m_root{nullptr};
    EntityId m_nextEntityId{0};
    std::uint32_t m_generation{0};
};

// Composes translation * rotation * scale, mapping local points into parent space.
[[nodiscard]] math::Mat4 parentFromLocal(const LocalTransform& transform) noexcept;
// Returns the cached composition of ancestors, including the root's own transform.
[[nodiscard]] math::Mat4 worldFromLocal(const Entity& entity) noexcept;

} // namespace ofg
