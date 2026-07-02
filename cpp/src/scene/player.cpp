// Player scene component implementation.
#include "ofg/scene/player.hpp"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr float _fast_multiplier = 2.0f;
constexpr float _slow_multiplier = 0.35f;
constexpr float _minimum_speed = 0.0001f;
constexpr const char* _player_model_label = "Quaternius player";
constexpr const char* _player_animation_label = "Quaternius UAL1 locomotion";
constexpr const char* _player_model_source_uri = "assets/models/player/quaternius-superhero-male.glb";
constexpr const char* _player_animation_source_uri = "assets/models/player/quaternius-ual1-standard.glb";
constexpr const char* _idle_clip_name = "Idle_Loop";
constexpr const char* _walk_clip_name = "Walk_Loop";
constexpr const char* _sprint_clip_name = "Sprint_Loop";
constexpr float _player_model_ground_offset_y = -0.9f;

class EmptyGltfResourceProvider final : public GltfResourceProvider {
public:
    // GLB player assets carry their buffers and images internally.
    [[nodiscard]] std::optional<AssetFile> load_relative(std::string_view) override {
        return std::nullopt;
    }
};

// Returns a flat normalized direction, or a zero vector when no movement exists.
math::Vec3 flat_normalized(math::Vec3 value) {
    value.y = 0.0f;
    const float length_squared = math::length_squared(value);
    if (length_squared <= 0.0f) {
        return math::vec3(0.0f, 0.0f, 0.0f);
    }
    if (length_squared <= 1.0f) {
        return value;
    }
    return math::mul(value, 1.0f / std::sqrt(length_squared));
}

// Returns a movement speed multiplier from the current control modifiers.
float speed_multiplier(const ControlInput& controls) noexcept {
    if (controls.m_fast && !controls.m_slow) {
        return _fast_multiplier;
    }
    if (controls.m_slow && !controls.m_fast) {
        return _slow_multiplier;
    }
    return 1.0f;
}

// Extracts the owning entity or reports a component binding error.
Entity& require_entity(Player& player) {
    Entity* entity = player.entity();
    if (entity == nullptr) {
        throw EngineError("Player update requires an owning entity.");
    }
    return *entity;
}

// Returns the entity's local right direction.
math::Vec3 entity_right(const Entity& entity) noexcept {
    const math::Mat4 rotation = math::mat4_from_quat(entity.local_transform().m_rotation);
    const math::Vec4 right = math::mul(rotation, math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    return math::vec3(right.x, 0.0f, right.z);
}

// Returns the entity's local forward direction.
math::Vec3 entity_forward(const Entity& entity) noexcept {
    const math::Mat4 rotation = math::mat4_from_quat(entity.local_transform().m_rotation);
    const math::Vec4 forward = math::mul(rotation, math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    return math::vec3(forward.x, 0.0f, forward.z);
}

// Returns one named clip from a model resource, or a diagnostic naming the missing clip.
const AnimationClip& require_clip(const ModelResource& resource, std::string_view clip_name) {
    for (std::size_t index = 0; index < resource.animation_clip_count(); ++index) {
        const AnimationClip* clip = resource.animation_clip(index);
        if (clip != nullptr && clip->name() == clip_name) {
            return *clip;
        }
    }
    throw EngineError("Player animation library does not contain required clip '" + std::string(clip_name) + "'.");
}

// Builds a node-name to source-node-index map for animation retargeting.
std::unordered_map<std::string, std::uint32_t> node_indices_by_name(const ModelResource& resource) {
    std::unordered_map<std::string, std::uint32_t> result;
    for (std::size_t index = 0; index < resource.nodes().size(); ++index) {
        const std::string& name = resource.nodes()[index].m_name;
        if (name.empty()) {
            continue;
        }
        const auto inserted = result.emplace(name, static_cast<std::uint32_t>(index));
        if (!inserted.second) {
            throw EngineError("ModelResource '" + resource.label() + "' contains duplicate node name '" + name + "'.");
        }
    }
    return result;
}

// Copies one animation clip while remapping channel targets by node name.
std::unique_ptr<AnimationClip> remap_clip_to_model_nodes(
    const AnimationClip& source_clip, const ModelResource& source_nodes, const ModelResource& target_nodes) {
    const std::unordered_map<std::string, std::uint32_t> target_by_name = node_indices_by_name(target_nodes);
    auto remapped = std::make_unique<AnimationClip>(source_clip.name());
    remapped->set_duration_seconds(source_clip.duration_seconds());

    for (const AnimationChannel& channel : source_clip.channels()) {
        if (channel.m_target_node_index >= source_nodes.nodes().size()) {
            throw EngineError("Player animation clip '" + source_clip.name() + "' targets a node outside its library.");
        }
        const std::string& target_name = source_nodes.nodes()[channel.m_target_node_index].m_name;
        if (target_name.empty()) {
            throw EngineError(
                "Player animation clip '" + source_clip.name() + "' targets an unnamed animation-library node.");
        }
        const auto found = target_by_name.find(target_name);
        if (found == target_by_name.end()) {
            throw EngineError("Player animation clip '" + source_clip.name() + "' targets node '" + target_name +
                              "', which is absent from the player model.");
        }

        AnimationChannel remapped_channel = channel;
        remapped_channel.m_target_node_index = found->second;
        remapped->add_channel(std::move(remapped_channel));
    }

    return remapped;
}

// Creates the three clips consumed by Player in idle/walk/sprint order.
std::vector<std::unique_ptr<AnimationClip>> remap_locomotion_clips(
    const ModelResource& animation_library, const ModelResource& player_model) {
    std::vector<std::unique_ptr<AnimationClip>> clips;
    clips.reserve(3U);
    clips.push_back(
        remap_clip_to_model_nodes(require_clip(animation_library, _idle_clip_name), animation_library, player_model));
    clips.push_back(
        remap_clip_to_model_nodes(require_clip(animation_library, _walk_clip_name), animation_library, player_model));
    clips.push_back(
        remap_clip_to_model_nodes(require_clip(animation_library, _sprint_clip_name), animation_library, player_model));
    return clips;
}

} // namespace

// Computes stable idle/walk/sprint animation weights from movement speeds.
LocomotionAnimationWeights compute_locomotion_animation_weights(float speed, float walk_speed, float sprint_speed) {
    if (!std::isfinite(speed) || speed < 0.0f) {
        throw EngineError("Player locomotion animation requires a finite non-negative player speed.");
    }
    if (!std::isfinite(walk_speed) || walk_speed < 0.0f) {
        throw EngineError("Player locomotion animation requires a finite non-negative walk speed.");
    }
    if (!std::isfinite(sprint_speed) || sprint_speed < 0.0f) {
        throw EngineError("Player locomotion animation requires a finite non-negative sprint speed.");
    }

    LocomotionAnimationWeights weights;
    if (speed <= _minimum_speed) {
        return weights;
    }
    if (walk_speed <= _minimum_speed) {
        weights.m_idle = 0.0f;
        weights.m_sprint = 1.0f;
        return weights;
    }
    if (speed <= walk_speed) {
        const float walk_blend = std::clamp(speed / walk_speed, 0.0f, 1.0f);
        weights.m_idle = 1.0f - walk_blend;
        weights.m_walk = walk_blend;
        return weights;
    }

    const float sprint_reference_speed = std::max(sprint_speed, walk_speed + _minimum_speed);
    const float sprint_blend = std::clamp((speed - walk_speed) / (sprint_reference_speed - walk_speed), 0.0f, 1.0f);
    weights.m_idle = 0.0f;
    weights.m_walk = 1.0f - sprint_blend;
    weights.m_sprint = sprint_blend;
    return weights;
}

// Binds this player to one scene-owned entity.
Player::Player(Entity* entity) noexcept : Component(ComponentType::Player, entity) {}

// Releases player-owned model resources after the concrete resource types are complete.
Player::~Player() = default;

// Returns the walking speed in world units per second.
float Player::walk_speed() const noexcept {
    return m_walk_speed;
}

// Returns the fast movement speed in world units per second.
float Player::fast_speed() const noexcept {
    return m_walk_speed * _fast_multiplier;
}

// Replaces the walking speed after validating it is finite and non-negative.
void Player::set_walk_speed(float speed) {
    if (!std::isfinite(speed) || speed < 0.0f) {
        throw EngineError("Player walk speed must be a finite non-negative value.");
    }
    m_walk_speed = speed;
}

// Returns the height used to keep the centered player box grounded.
float Player::height() const noexcept {
    return m_height;
}

// Replaces the player height after validating it is finite and positive.
void Player::set_height(float height) {
    if (!std::isfinite(height) || height <= 0.0f) {
        throw EngineError("Player height must be a positive finite value.");
    }
    m_height = height;
}

// Returns the latest intended flat movement speed in world units per second.
float Player::current_speed() const noexcept {
    return m_current_speed;
}

// Imports and attaches the default hardcoded player model to this player entity.
void Player::load_default_model(
    GpuContext gpu, Scene& scene, std::span<const std::byte> player_glb, std::span<const std::byte> animation_glb) {
    if (m_default_model_loaded || m_model_resource != nullptr) {
        return;
    }
    if (player_glb.empty()) {
        throw EngineError("Player default model requires non-empty player GLB bytes.");
    }
    if (animation_glb.empty()) {
        throw EngineError("Player default model requires non-empty animation-library GLB bytes.");
    }

    Entity& owner = require_entity(*this);
    EmptyGltfResourceProvider resource_provider;
    GltfDocument player_document = load_gltf_document(_player_model_source_uri, player_glb, resource_provider);
    GltfDocument animation_document =
        load_gltf_document(_player_animation_source_uri, animation_glb, resource_provider);

    auto import_context = std::make_unique<ModelResourceImportContext>(gpu);
    std::unique_ptr<ModelResource> player_resource = import_gltf_model_resource(
        player_document, GltfImportOptions{_player_model_label, _player_model_source_uri}, *import_context);
    std::unique_ptr<ModelResource> animation_resource = import_gltf_model_resource(
        animation_document, GltfImportOptions{_player_animation_label, _player_animation_source_uri}, *import_context);
    std::vector<std::unique_ptr<AnimationClip>> locomotion_clips =
        remap_locomotion_clips(*animation_resource, *player_resource);
    if (locomotion_clips.size() != 3U || locomotion_clips[0] == nullptr || locomotion_clips[1] == nullptr ||
        locomotion_clips[2] == nullptr) {
        throw EngineError("Player default model requires idle, walk, and sprint clips.");
    }

    ModelInstance instance = instantiate_model_resource(*player_resource, scene, owner);
    Entity* model_root = instance.m_root_entity.get();
    if (model_root == nullptr) {
        throw EngineError("Player default model attachment failed to instantiate a model root entity.");
    }
    model_root->local_transform().m_position = math::vec3(0.0f, _player_model_ground_offset_y, 0.0f);
    // The selected player mesh is authored facing +Z, matching OFG player/camera forward.
    model_root->local_transform().m_rotation = math::quat_identity();
    model_root->local_transform().m_scale = math::vec3(1.0f, 1.0f, 1.0f);

    AnimationPlayer* animation_player = instance.m_animation_player.get();
    if (animation_player == nullptr) {
        animation_player = static_cast<AnimationPlayer*>(model_root->create_component(ComponentType::AnimationPlayer));
        animation_player->bind_targets(std::move(instance.m_entities_by_node_index));
    }

    m_model_import_context = std::move(import_context);
    m_model_resource = std::move(player_resource);
    m_animation_resource = std::move(animation_resource);
    m_locomotion_clips = std::move(locomotion_clips);
    m_model_root_entity = model_root;
    m_model_animation_player = animation_player;
    bind_locomotion_animation(
        *animation_player, *m_locomotion_clips[0], *m_locomotion_clips[1], *m_locomotion_clips[2]);
    m_default_model_loaded = true;
    set_fallback_visible(false);
}

// Returns whether the hardcoded player model has been imported and attached.
bool Player::default_model_loaded() const noexcept {
    return m_default_model_loaded;
}

// Binds the mesh renderer used as a visible fallback while the model is unavailable.
void Player::bind_fallback_renderer(MeshRenderer& renderer) {
    m_fallback_renderer = &renderer;
    renderer.set_visible(m_fallback_visible);
}

// Sets whether the fallback renderer is currently visible.
void Player::set_fallback_visible(bool visible) noexcept {
    m_fallback_visible = visible;
    MeshRenderer* renderer = m_fallback_renderer.get();
    if (renderer != nullptr) {
        renderer->set_visible(visible);
    }
}

// Returns whether the fallback renderer is currently visible.
bool Player::fallback_visible() const noexcept {
    return m_fallback_visible;
}

// Binds the animation player and clips driven by this player's movement speed.
void Player::bind_locomotion_animation(
    AnimationPlayer& animation_player, AnimationClip& idle_clip, AnimationClip& walk_clip, AnimationClip& sprint_clip) {
    m_locomotion_animation_player = &animation_player;
    m_idle_clip = &idle_clip;
    m_walk_clip = &walk_clip;
    m_sprint_clip = &sprint_clip;
    m_has_locomotion_clips = true;
    update_locomotion_animation();
}

// Returns the last computed idle animation weight.
float Player::idle_animation_weight() const noexcept {
    return m_idle_animation_weight;
}

// Returns the last computed walk animation weight.
float Player::walk_animation_weight() const noexcept {
    return m_walk_animation_weight;
}

// Returns the last computed sprint animation weight.
float Player::sprint_animation_weight() const noexcept {
    return m_sprint_animation_weight;
}

// Applies player-relevant controls for one frame.
void Player::update(const SceneUpdateContext& context) {
    m_current_speed = 0.0f;
    if (context.m_primary_player != this) {
        update_locomotion_animation();
        return;
    }
    if (!std::isfinite(context.m_delta_seconds) || context.m_delta_seconds < 0.0f) {
        throw EngineError("Player update requires a finite non-negative delta.");
    }

    Entity& owner = require_entity(*this);
    LocalTransform& transform = owner.local_transform();
    transform.m_position.y = m_height * 0.5f;
    if (context.m_main_camera == nullptr || context.m_main_camera->control_mode() == CameraControlMode::Debug) {
        update_locomotion_animation();
        return;
    }

    math::Vec3 movement = math::vec3(0.0f, 0.0f, 0.0f);
    movement = math::add(movement, math::mul(entity_right(owner), context.m_controls.m_move_x));
    movement = math::add(movement, math::mul(entity_forward(owner), context.m_controls.m_move_z));
    movement = flat_normalized(movement);

    m_current_speed = m_walk_speed * speed_multiplier(context.m_controls) * std::sqrt(math::length_squared(movement));
    const float distance = m_current_speed * context.m_delta_seconds;
    if (distance > 0.0f && math::length_squared(movement) > 0.0f) {
        transform.m_position = math::add(transform.m_position, math::mul(movement, distance));
        transform.m_position.y = m_height * 0.5f;
    }
    update_locomotion_animation();
}

// Updates locomotion clip weights from the current movement speed.
void Player::update_locomotion_animation() {
    if (!m_has_locomotion_clips) {
        return;
    }
    AnimationPlayer* animation_player = m_locomotion_animation_player.get();
    AnimationClip* idle_clip = m_idle_clip.get();
    AnimationClip* walk_clip = m_walk_clip.get();
    AnimationClip* sprint_clip = m_sprint_clip.get();
    if (animation_player == nullptr) {
        throw EngineError("Player locomotion animation target has been destroyed.");
    }
    if (idle_clip == nullptr || walk_clip == nullptr || sprint_clip == nullptr) {
        throw EngineError("Player locomotion animation clip has been destroyed.");
    }

    const LocomotionAnimationWeights weights =
        compute_locomotion_animation_weights(m_current_speed, m_walk_speed, fast_speed());
    m_idle_animation_weight = weights.m_idle;
    m_walk_animation_weight = weights.m_walk;
    m_sprint_animation_weight = weights.m_sprint;
    animation_player->set_clip_state(*idle_clip, m_idle_animation_weight, true, 1.0f);
    animation_player->set_clip_state(*walk_clip, m_walk_animation_weight, true, 1.0f);
    animation_player->set_clip_state(*sprint_clip, m_sprint_animation_weight, true, 1.0f);
}

} // namespace ofg
