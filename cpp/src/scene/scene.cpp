// Entity/component scene graph implementation.
#include "ofg/scene/scene.hpp"

#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/environment.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene_update.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Writes one cached world transform per entity id in tree order.
void write_world_transform_cache(
    const Entity& entity, const math::Mat4& world_from_parent, std::vector<math::Mat4>& cache) {
    if (entity.id() >= cache.size()) {
        throw EngineError("Scene world-transform cache is smaller than the entity id range.");
    }

    const math::Mat4 world_from_entity = math::mul(world_from_parent, parent_from_local(entity.local_transform()));
    cache[entity.id()] = world_from_entity;
    for (const Entity* child = entity.first_child(); child != nullptr; child = child->next_sibling()) {
        write_world_transform_cache(*child, world_from_entity, cache);
    }
}

} // namespace

// Creates a scene with a single root entity.
Scene::Scene() {
    create_root_entity();
}

// Moves scene storage and rebinds moved entity owner pointers.
Scene::Scene(Scene&& other) noexcept
    : m_entities(std::move(other.m_entities)), m_mesh_renderers(std::move(other.m_mesh_renderers)),
      m_cameras(std::move(other.m_cameras)), m_players(std::move(other.m_players)),
      m_animation_players(std::move(other.m_animation_players)), m_lights(std::move(other.m_lights)),
      m_main_camera(std::move(other.m_main_camera)), m_environment(std::move(other.m_environment)),
      m_world_transform_cache(std::move(other.m_world_transform_cache)), m_root(other.m_root),
      m_next_entity_id(other.m_next_entity_id), m_generation(other.m_generation) {
    rebind_entities_after_move();
    other.m_main_camera = nullptr;
    other.m_root = nullptr;
    other.m_next_entity_id = 0;
}

// Moves scene storage and rebinds moved entity owner pointers.
Scene& Scene::operator=(Scene&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    m_entities = std::move(other.m_entities);
    m_mesh_renderers = std::move(other.m_mesh_renderers);
    m_cameras = std::move(other.m_cameras);
    m_players = std::move(other.m_players);
    m_animation_players = std::move(other.m_animation_players);
    m_lights = std::move(other.m_lights);
    m_main_camera = std::move(other.m_main_camera);
    m_environment = std::move(other.m_environment);
    m_world_transform_cache = std::move(other.m_world_transform_cache);
    m_root = other.m_root;
    m_next_entity_id = other.m_next_entity_id;
    m_generation = other.m_generation;
    rebind_entities_after_move();
    other.m_main_camera = nullptr;
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

// Reports the number of camera components in creation order.
std::size_t Scene::camera_count() const noexcept {
    return m_cameras.size();
}

// Returns one camera by creation-order index.
Camera* Scene::get_camera(std::size_t index) noexcept {
    if (index >= m_cameras.size()) {
        return nullptr;
    }
    return m_cameras[index].get();
}

// Returns one camera by creation-order index.
const Camera* Scene::get_camera(std::size_t index) const noexcept {
    if (index >= m_cameras.size()) {
        return nullptr;
    }
    return m_cameras[index].get();
}

// Returns the explicit main camera or the first camera when none is selected.
Camera* Scene::main_camera() noexcept {
    if (m_main_camera != nullptr) {
        return m_main_camera.get();
    }
    return m_cameras.empty() ? nullptr : m_cameras.front().get();
}

// Returns the explicit main camera or the first camera when none is selected.
const Camera* Scene::main_camera() const noexcept {
    if (m_main_camera != nullptr) {
        return m_main_camera.get();
    }
    return m_cameras.empty() ? nullptr : m_cameras.front().get();
}

// Replaces the explicit main camera selection, or clears it for first-camera fallback.
void Scene::set_main_camera(Camera* camera) {
    if (camera != nullptr && !contains_current_camera(camera)) {
        throw EngineError("Scene::set_main_camera requires a camera from the same scene.");
    }
    m_main_camera = camera;
}

// Reports the number of light components in creation order.
std::size_t Scene::light_count() const noexcept {
    return m_lights.size();
}

// Returns one light by creation-order index.
Light* Scene::get_light(std::size_t index) noexcept {
    if (index >= m_lights.size()) {
        return nullptr;
    }
    return m_lights[index].get();
}

// Returns one light by creation-order index.
const Light* Scene::get_light(std::size_t index) const noexcept {
    if (index >= m_lights.size()) {
        return nullptr;
    }
    return m_lights[index].get();
}

// Returns scene-owned global environment state.
Environment& Scene::environment() noexcept {
    return m_environment;
}

// Returns scene-owned global environment state.
const Environment& Scene::environment() const noexcept {
    return m_environment;
}

// Reports the number of player components in creation order.
std::size_t Scene::player_count() const noexcept {
    return m_players.size();
}

// Returns one player by creation-order index.
Player* Scene::get_player(std::size_t index) noexcept {
    if (index >= m_players.size()) {
        return nullptr;
    }
    return m_players[index].get();
}

// Returns one player by creation-order index.
const Player* Scene::get_player(std::size_t index) const noexcept {
    if (index >= m_players.size()) {
        return nullptr;
    }
    return m_players[index].get();
}

// Reports the number of animation-player components in creation order.
std::size_t Scene::animation_player_count() const noexcept {
    return m_animation_players.size();
}

// Returns one animation player by creation-order index.
AnimationPlayer* Scene::get_animation_player(std::size_t index) noexcept {
    if (index >= m_animation_players.size()) {
        return nullptr;
    }
    return m_animation_players[index].get();
}

// Returns one animation player by creation-order index.
const AnimationPlayer* Scene::get_animation_player(std::size_t index) const noexcept {
    if (index >= m_animation_players.size()) {
        return nullptr;
    }
    return m_animation_players[index].get();
}

// Updates scene-owned gameplay and camera components in deterministic order.
void Scene::update(const SceneUpdateContext& context) {
    validate_control_input(context.m_controls);
    m_environment.update(*this, context.m_time_ms, context.m_delta_seconds);
    for (const std::unique_ptr<Player>& player : m_players) {
        player->update(context);
    }
    for (const std::unique_ptr<AnimationPlayer>& animation_player : m_animation_players) {
        animation_player->update(context);
    }
    m_world_transform_cache.resize(m_next_entity_id);
    if (m_root != nullptr) {
        write_world_transform_cache(*m_root, math::mat4_identity(), m_world_transform_cache);
    }
    for (const std::unique_ptr<MeshRenderer>& mesh_renderer : m_mesh_renderers) {
        mesh_renderer->update_skinning(m_world_transform_cache);
    }
    for (const std::unique_ptr<Camera>& camera : m_cameras) {
        camera->update(context);
    }
}

// Returns the generation token invalidated by clear().
std::uint32_t Scene::generation() const noexcept {
    return m_generation;
}

// Clears all entities/components and creates a fresh root.
void Scene::clear() {
    m_main_camera = nullptr;
    m_environment = Environment{};
    m_cameras.clear();
    m_players.clear();
    m_animation_players.clear();
    m_mesh_renderers.clear();
    m_lights.clear();
    m_world_transform_cache.clear();
    m_entities.clear();
    m_root = nullptr;
    m_next_entity_id = 0;
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
    case ComponentType::Camera:
        if (entity.m_camera != nullptr) {
            throw EngineError("Entity already has a Camera component.");
        }
        m_cameras.push_back(std::make_unique<Camera>(&entity));
        entity.m_camera = m_cameras.back().get();
        return entity.m_camera;
    case ComponentType::Player:
        if (entity.m_player != nullptr) {
            throw EngineError("Entity already has a Player component.");
        }
        m_players.push_back(std::make_unique<Player>(&entity));
        entity.m_player = m_players.back().get();
        return entity.m_player;
    case ComponentType::AnimationPlayer:
        if (entity.m_animation_player != nullptr) {
            throw EngineError("Entity already has an AnimationPlayer component.");
        }
        m_animation_players.push_back(std::make_unique<AnimationPlayer>(&entity));
        entity.m_animation_player = m_animation_players.back().get();
        return entity.m_animation_player;
    case ComponentType::Light:
        if (entity.m_light != nullptr) {
            throw EngineError("Entity already has a Light component.");
        }
        m_lights.push_back(std::make_unique<Light>(&entity));
        entity.m_light = m_lights.back().get();
        return entity.m_light;
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

// Returns whether a camera pointer belongs to current scene-owned storage.
bool Scene::contains_current_camera(const Camera* camera) const noexcept {
    if (camera == nullptr) {
        return false;
    }
    for (const std::unique_ptr<Camera>& current : m_cameras) {
        if (current.get() == camera) {
            return true;
        }
    }
    return false;
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
