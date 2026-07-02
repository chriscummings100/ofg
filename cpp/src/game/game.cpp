// Static OFG game lifecycle facade.
//
// Game owns portable frame/debug state and orchestrates Resources and Renderer
// for one WebGPU device lifetime. Browser and native frame drivers provide
// targets, command encoders, finish/submit work, presentation, and platform
// handle ownership.
#include "ofg/game/game.hpp"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/render/renderer.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/player_animation_controller.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Returns a stable empty error string for calls made without a live Game.
const std::string& empty_error_string() noexcept {
    static const std::string _empty;
    return _empty;
}

// Returns a stable uninitialized status for calls made without a live Game.
const RuntimeDebugStatus& uninitialized_status() noexcept {
    static const RuntimeDebugStatus _status;
    return _status;
}

// Formats floating-point validation failures for runtime status and exceptions.
std::string finite_number_message(const char* label, double value) {
    std::ostringstream out;
    out << label << " must be finite, got " << value << ".";
    return out.str();
}

// Formats device-pixel-ratio validation failures for runtime status and exceptions.
std::string device_pixel_ratio_message(double value) {
    std::ostringstream out;
    out << "Device pixel ratio must be a positive finite number, got " << value << ".";
    return out.str();
}

constexpr float _max_delta_seconds = 0.1f;
constexpr const char* _player_model_label = "Quaternius player";
constexpr const char* _player_animation_label = "Quaternius UAL1 locomotion";
constexpr const char* _player_model_source_uri = "assets/models/player/quaternius-superhero-male.glb";
constexpr const char* _player_animation_source_uri = "assets/models/player/quaternius-ual1-standard.glb";
constexpr const char* _idle_clip_name = "Idle_Loop";
constexpr const char* _walk_clip_name = "Walk_Loop";
constexpr const char* _sprint_clip_name = "Sprint_Loop";
constexpr float _player_model_ground_offset_y = -0.9f;

// Clears transient status errors without erasing a durable model-loading failure.
void clear_status_last_error(RuntimeDebugStatus& status) noexcept {
    if (status.m_model_loading_state != "failed") {
        status.m_last_error.reset();
    }
}

class EmptyGltfResourceProvider final : public GltfResourceProvider {
public:
    // GLB player assets carry their buffers and images internally.
    [[nodiscard]] std::optional<AssetFile> load_relative(std::string_view) override {
        return std::nullopt;
    }
};

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

// Creates the three clips consumed by PlayerAnimationController in idle/walk/sprint order.
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

std::unique_ptr<Game> Game::s_game;

// Converts a Game lifecycle state into its debug-status string value.
const char* game_lifecycle_state_name(GameLifecycleState state) noexcept {
    switch (state) {
    case GameLifecycleState::Uninitialized:
        return "uninitialized";
    case GameLifecycleState::Created:
        return "created";
    case GameLifecycleState::Prep_Resources:
        return "prep_resources";
    case GameLifecycleState::Prep_Scene:
        return "prep_scene";
    case GameLifecycleState::Prep_Renderer:
        return "prep_renderer";
    case GameLifecycleState::Ready:
        return "ready";
    case GameLifecycleState::Rel_Renderer:
        return "rel_renderer";
    case GameLifecycleState::Rel_Scene:
        return "rel_scene";
    case GameLifecycleState::Rel_Resources:
        return "rel_resources";
    case GameLifecycleState::Released:
        return "released";
    case GameLifecycleState::Failed:
        return "failed";
    }
    return "unknown";
}

// Stores borrowed platform WebGPU handles and lifecycle state.
Game::Game(GpuContext gpu, WGPUTextureFormat color_format) : m_gpu(gpu), m_color_format(color_format) {
    m_status.m_lifecycle_state = game_lifecycle_state_name(m_state);
}

// Releases only members directly owned by Game.
Game::~Game() = default;

// Creates the singleton for one platform WebGPU lifetime.
void Game::create(GpuContext gpu, WGPUTextureFormat color_format) {
    if (s_game != nullptr) {
        throw EngineError("Game::create cannot be called while a Game singleton is live.");
    }
    if (color_format == WGPUTextureFormat_Undefined) {
        throw EngineError("Game requires a defined color format.");
    }
    if (gpu.m_device == nullptr || gpu.m_queue == nullptr) {
        throw EngineError("Game requires a WebGPU device and queue.");
    }

    std::unique_ptr<Game> game(new Game(gpu, color_format));
    try {
        Resources::create(gpu);
        Renderer::create(gpu, color_format);
        game->set_state(GameLifecycleState::Created);
        s_game = std::move(game);
    } catch (...) {
        Renderer::destroy();
        Resources::destroy();
        throw;
    }
}

// Advances startup work and reports whether Game is ready.
bool Game::prepare() {
    try {
        return require_game("Game::prepare").prepare_impl();
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_failed_exception_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_failed_exception_impl("Game::prepare failed with an unknown exception.");
        }
        throw;
    }
}

// Accepts the latest platform target size used for render validation.
void Game::resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio) {
    try {
        require_game("Game::resize").resize_impl(width, height, device_pixel_ratio);
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_error_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_error_impl("Game::resize failed with an unknown exception.");
        }
        throw;
    }
}

// Advances shared per-frame state.
void Game::update(double time_ms) {
    try {
        require_game("Game::update").update_impl(time_ms);
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_error_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_error_impl("Game::update failed with an unknown exception.");
        }
        throw;
    }
}

// Accepts the latest raw control input snapshot.
void Game::set_control_input(ControlInput input) {
    try {
        require_game("Game::set_control_input").set_control_input_impl(input);
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_error_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_error_impl("Game::set_control_input failed with an unknown exception.");
        }
        throw;
    }
}

// Imports and attaches the default player model and animation library.
void Game::load_player_model(std::span<const std::byte> player_glb, std::span<const std::byte> animation_glb) {
    try {
        require_game("Game::load_player_model").load_player_model_impl(player_glb, animation_glb);
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_player_model_load_failure_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_player_model_load_failure_impl("Game::load_player_model failed with an unknown exception.");
        }
        throw;
    }
}

// Records a recoverable player-model loading failure before bytes reach C++ import.
void Game::record_player_model_load_failure(std::string message) noexcept {
    if (s_game != nullptr) {
        s_game->record_player_model_load_failure_impl(std::move(message));
    }
}

// Records render commands into the caller-owned command encoder.
void Game::render(WGPUCommandEncoder encoder, RenderTarget target) {
    try {
        require_game("Game::render").render_impl(encoder, target);
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_error_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_error_impl("Game::render failed with an unknown exception.");
        }
        throw;
    }
}

// Advances teardown work and reports whether Game has released resources.
bool Game::release() {
    try {
        if (s_game == nullptr) {
            return true;
        }
        return s_game->release_impl();
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_failed_exception_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_failed_exception_impl("Game::release failed with an unknown exception.");
        }
        throw;
    }
}

// Destroys the singleton after release has completed.
void Game::destroy() noexcept {
    if (s_game != nullptr) {
        s_game->destroy_impl();
        s_game.reset();
    }
}

// Returns the current high-level lifecycle state.
GameLifecycleState Game::state() noexcept {
    if (s_game != nullptr) {
        return s_game->m_state;
    }
    return GameLifecycleState::Uninitialized;
}

// Returns the most recently recorded Game-level error.
const std::string& Game::last_error() noexcept {
    if (s_game != nullptr) {
        return s_game->m_last_error;
    }
    return empty_error_string();
}

// Records a recoverable platform/runtime error in shared debug status.
void Game::record_error(std::string message) noexcept {
    if (s_game != nullptr) {
        s_game->record_error_impl(std::move(message));
    }
}

// Records a GPU/device error that requires platform reinitialization.
void Game::record_gpu_error(std::string message) noexcept {
    if (s_game != nullptr) {
        s_game->record_gpu_error_impl(std::move(message));
    }
}

// Returns browser-facing debug status JSON.
std::string Game::debug_status_json() {
    return status().to_json();
}

// Returns the current debug status snapshot.
const RuntimeDebugStatus& Game::status() noexcept {
    if (s_game != nullptr) {
        return s_game->m_status;
    }
    return uninitialized_status();
}

// Advances the renderer/resource preparation state machine.
bool Game::prepare_impl() {
    switch (m_state) {
    case GameLifecycleState::Ready:
        return true;
    case GameLifecycleState::Created:
        set_state(GameLifecycleState::Prep_Resources);
        [[fallthrough]];
    case GameLifecycleState::Prep_Resources:
        if (!Resources::prepare()) {
            return false;
        }
        set_state(GameLifecycleState::Prep_Scene);
        [[fallthrough]];
    case GameLifecycleState::Prep_Scene: {
        m_demo_scene = DemoScene{};
        m_current_scene = std::make_unique<Scene>();
        build_demo_scene(m_demo_scene);
        setup_demo_scene(m_demo_scene, *m_current_scene);
        attach_player_model_to_scene();
        update_demo_scene(m_demo_scene, m_last_time_ms, *m_current_scene);
        set_state(GameLifecycleState::Prep_Renderer);
    }
        [[fallthrough]];
    case GameLifecycleState::Prep_Renderer: {
        if (!Renderer::prepare()) {
            return false;
        }
        if (m_status.m_canvas_width > 0 && m_status.m_canvas_height > 0) {
            Renderer::resize(m_status.m_canvas_width, m_status.m_canvas_height);
        }
        mark_gpu_ready(m_gpu.m_adapter_name, m_gpu.m_backend, gpu::texture_format_name(m_color_format));
        const RendererCounters counters = Renderer::counters();
        mark_renderer_counters(counters.m_pipeline_create_count, counters.m_buffer_create_count);
        set_state(GameLifecycleState::Ready);
        return true;
    }
    case GameLifecycleState::Failed:
        throw EngineError("Game::prepare cannot continue while Game is failed: " + m_last_error);
    case GameLifecycleState::Rel_Renderer:
    case GameLifecycleState::Rel_Scene:
    case GameLifecycleState::Rel_Resources:
    case GameLifecycleState::Released:
        throw EngineError("Game::prepare cannot run after Game release has started.");
    case GameLifecycleState::Uninitialized:
        throw EngineError("Game::prepare requires Game::create first.");
    }
    throw EngineError("Game::prepare cannot run in an unknown lifecycle state.");
}

// Accepts the latest platform target size used for render validation.
void Game::resize_impl(std::uint32_t width, std::uint32_t height, double device_pixel_ratio) {
    if (m_state == GameLifecycleState::Rel_Renderer || m_state == GameLifecycleState::Rel_Scene ||
        m_state == GameLifecycleState::Rel_Resources || m_state == GameLifecycleState::Released) {
        throw EngineError("Game::resize cannot run after Game release has started.");
    }
    if (m_state == GameLifecycleState::Failed) {
        throw EngineError("Game::resize cannot run while Game is failed: " + m_last_error);
    }

    resize_runtime(width, height, device_pixel_ratio);
    const bool renderer_ready = Renderer::state() == RendererLifecycleState::Ready;
    if (renderer_ready) {
        Renderer::resize(width, height);
    }
}

// Advances shared per-frame state.
void Game::update_impl(double time_ms) {
    if (m_state != GameLifecycleState::Ready) {
        throw EngineError("Game::update requires Game::prepare to complete first.");
    }

    tick_runtime(time_ms);
    const float delta_seconds = frame_delta_seconds(time_ms);
    if (m_current_scene == nullptr) {
        throw EngineError("Game update requires a current scene.");
    }
    update_demo_scene(m_demo_scene, time_ms, *m_current_scene);
    Player* primary_player = m_current_scene->player_count() == 0 ? nullptr : m_current_scene->get_player(0);
    Camera* main_camera = m_current_scene->main_camera();
    SceneUpdateContext context{m_control_input, time_ms, delta_seconds, primary_player, main_camera};
    m_current_scene->update(context);
    if (main_camera != nullptr) {
        m_status.m_camera_mode = camera_control_mode_name(main_camera->control_mode());
    }
    clear_consumed_control_edges();
    m_last_time_ms = time_ms;
    m_has_last_time = true;
}

// Stores a validated raw control input snapshot.
void Game::set_control_input_impl(ControlInput input) {
    if (m_state == GameLifecycleState::Failed) {
        throw EngineError("Game::set_control_input cannot run while Game is failed: " + m_last_error);
    }
    if (m_state == GameLifecycleState::Rel_Renderer || m_state == GameLifecycleState::Rel_Scene ||
        m_state == GameLifecycleState::Rel_Resources || m_state == GameLifecycleState::Released) {
        throw EngineError("Game::set_control_input cannot run after Game release has started.");
    }
    validate_control_input(input);
    m_control_input = input;
}

// Imports and attaches the default player model and animation library.
void Game::load_player_model_impl(std::span<const std::byte> player_glb, std::span<const std::byte> animation_glb) {
    if (m_state == GameLifecycleState::Failed) {
        throw EngineError("Game::load_player_model cannot run while Game is failed: " + m_last_error);
    }
    if (m_state == GameLifecycleState::Rel_Renderer || m_state == GameLifecycleState::Rel_Scene ||
        m_state == GameLifecycleState::Rel_Resources || m_state == GameLifecycleState::Released) {
        throw EngineError("Game::load_player_model cannot run after Game release has started.");
    }
    if (player_glb.empty()) {
        throw EngineError("Game::load_player_model requires non-empty player GLB bytes.");
    }
    if (animation_glb.empty()) {
        throw EngineError("Game::load_player_model requires non-empty animation-library GLB bytes.");
    }
    if (m_status.m_player_model_loaded || m_player_model_resource != nullptr) {
        return;
    }

    m_status.m_model_loading_state = "loading";
    m_status.m_player_model_loaded = false;
    clear_status_last_error(m_status);

    EmptyGltfResourceProvider resource_provider;
    GltfDocument player_document = load_gltf_document(_player_model_source_uri, player_glb, resource_provider);
    GltfDocument animation_document =
        load_gltf_document(_player_animation_source_uri, animation_glb, resource_provider);

    auto import_context = std::make_unique<ModelResourceImportContext>(m_gpu);
    std::unique_ptr<ModelResource> player_resource = import_gltf_model_resource(
        player_document, GltfImportOptions{_player_model_label, _player_model_source_uri}, *import_context);
    std::unique_ptr<ModelResource> animation_resource = import_gltf_model_resource(
        animation_document, GltfImportOptions{_player_animation_label, _player_animation_source_uri}, *import_context);
    std::vector<std::unique_ptr<AnimationClip>> locomotion_clips =
        remap_locomotion_clips(*animation_resource, *player_resource);

    m_player_model_import_context = std::move(import_context);
    m_player_model_resource = std::move(player_resource);
    m_player_animation_resource = std::move(animation_resource);
    m_player_locomotion_clips = std::move(locomotion_clips);
    attach_player_model_to_scene();
}

// Attaches an already-imported player model to the active scene if possible.
void Game::attach_player_model_to_scene() {
    if (m_player_model_resource == nullptr || m_status.m_player_model_loaded) {
        return;
    }
    if (m_current_scene == nullptr) {
        return;
    }
    if (m_demo_scene.m_player_entity == nullptr || m_demo_scene.m_player == nullptr) {
        throw EngineError("Player model attachment requires a prepared demo player entity.");
    }
    if (m_player_locomotion_clips.size() != 3U || m_player_locomotion_clips[0] == nullptr ||
        m_player_locomotion_clips[1] == nullptr || m_player_locomotion_clips[2] == nullptr) {
        throw EngineError("Player model attachment requires idle, walk, and sprint clips.");
    }

    ModelInstance instance =
        instantiate_model_resource(*m_player_model_resource, *m_current_scene, *m_demo_scene.m_player_entity);
    Entity* model_root = instance.m_root_entity.get();
    if (model_root == nullptr) {
        throw EngineError("Player model attachment failed to instantiate a model root entity.");
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
    auto* controller =
        static_cast<PlayerAnimationController*>(model_root->create_component(ComponentType::PlayerAnimationController));
    controller->bind(*m_demo_scene.m_player, *animation_player);
    controller->set_locomotion_clips(
        *m_player_locomotion_clips[0], *m_player_locomotion_clips[1], *m_player_locomotion_clips[2]);

    m_player_model_root_entity = model_root;
    m_player_model_animation_player = animation_player;
    m_player_animation_controller = controller;
    m_demo_scene.m_player_fallback_visible = false;
    m_status.m_model_loading_state = "loaded";
    m_status.m_player_model_loaded = true;
    clear_status_last_error(m_status);
}

// Records a recoverable player-model loading failure.
void Game::record_player_model_load_failure_impl(std::string message) noexcept {
    if (message.empty()) {
        message = "Unknown player model loading error.";
    }
    m_last_error = std::move(message);
    m_status.m_model_loading_state = "failed";
    m_status.m_player_model_loaded = false;
    m_status.m_last_error = m_last_error;
    m_demo_scene.m_player_fallback_visible = true;
}

// Records render commands into the caller-owned command encoder.
void Game::render_impl(WGPUCommandEncoder encoder, RenderTarget target) {
    if (m_state != GameLifecycleState::Ready) {
        throw EngineError("Game::render requires Game::prepare to complete first.");
    }
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (encoder == nullptr) {
        const std::string message = "Game render requires a command encoder.";
        fail_runtime(message);
        throw EngineError(message);
    }

    validate_render_target(target, m_color_format, m_status.m_canvas_width, m_status.m_canvas_height);
    if (Renderer::state() != RendererLifecycleState::Ready) {
        const std::string message = "Game renderer resources are not created.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (m_current_scene == nullptr) {
        const std::string message = "Game render requires a current scene.";
        fail_runtime(message);
        throw EngineError(message);
    }

    mark_surface_configured();
    Renderer::render(encoder, target, *m_current_scene);
    const RendererCounters counters = Renderer::counters();
    mark_renderer_counters(counters.m_pipeline_create_count, counters.m_buffer_create_count);
}

// Advances the renderer/resource release state machine.
bool Game::release_impl() {
    switch (m_state) {
    case GameLifecycleState::Released:
        return true;
    case GameLifecycleState::Uninitialized:
        return true;
    case GameLifecycleState::Created:
    case GameLifecycleState::Prep_Resources:
    case GameLifecycleState::Prep_Scene:
    case GameLifecycleState::Prep_Renderer:
    case GameLifecycleState::Ready:
    case GameLifecycleState::Failed:
        set_state(GameLifecycleState::Rel_Renderer);
        [[fallthrough]];
    case GameLifecycleState::Rel_Renderer:
        if (!Renderer::release()) {
            return false;
        }
        set_state(GameLifecycleState::Rel_Scene);
        [[fallthrough]];
    case GameLifecycleState::Rel_Scene:
        m_demo_scene = DemoScene{};
        m_control_input = ControlInput{};
        m_player_model_root_entity.reset();
        m_player_model_animation_player.reset();
        m_player_animation_controller.reset();
        m_current_scene.reset();
        m_player_locomotion_clips.clear();
        m_player_animation_resource.reset();
        m_player_model_resource.reset();
        m_player_model_import_context.reset();
        m_has_last_time = false;
        set_state(GameLifecycleState::Rel_Resources);
        [[fallthrough]];
    case GameLifecycleState::Rel_Resources:
        if (!Resources::release()) {
            return false;
        }
        dispose_runtime();
        m_gpu = GpuContext{};
        m_color_format = WGPUTextureFormat_Undefined;
        set_state(GameLifecycleState::Released);
        return true;
    }
    throw EngineError("Game::release cannot run in an unknown lifecycle state.");
}

// Destroys owned systems after release has completed.
void Game::destroy_impl() noexcept {
    Renderer::destroy();
    Resources::destroy();
}

// Accepts a new physical target size and device pixel ratio.
void Game::resize_runtime(std::uint32_t width, std::uint32_t height, double device_pixel_ratio) {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
        const std::string message = device_pixel_ratio_message(device_pixel_ratio);
        fail_runtime(message);
        throw EngineError(message);
    }

    const bool dimensions_changed = m_status.m_canvas_width != width || m_status.m_canvas_height != height;
    m_status.m_canvas_width = width;
    m_status.m_canvas_height = height;
    m_status.m_device_pixel_ratio = device_pixel_ratio;
    if (dimensions_changed || width == 0 || height == 0) {
        m_surface_configured = false;
    }
    m_status.m_initialized = m_gpu_ready && m_surface_configured && width > 0 && height > 0;
    clear_status_last_error(m_status);
}

// Advances frame state after validating the frame timestamp.
void Game::tick_runtime(double time_ms) {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (!std::isfinite(time_ms)) {
        const std::string message = finite_number_message("Frame time", time_ms);
        fail_runtime(message);
        throw EngineError(message);
    }

    m_frame_state.tick(time_ms);
    m_status.m_frame_count = m_frame_state.frame_count();
    clear_status_last_error(m_status);
}

// Returns the clamped frame delta in seconds for component updates.
float Game::frame_delta_seconds(double time_ms) noexcept {
    if (!m_has_last_time) {
        return 0.0f;
    }
    const double delta_ms = time_ms - m_last_time_ms;
    if (!std::isfinite(delta_ms) || delta_ms <= 0.0) {
        return 0.0f;
    }
    return std::min(static_cast<float>(delta_ms * 0.001), _max_delta_seconds);
}

// Clears one-frame control edges after components consume them.
void Game::clear_consumed_control_edges() noexcept {
    m_control_input.m_cycle_camera_mode = false;
}

// Marks the shared GPU renderer path as ready.
void Game::mark_gpu_ready(std::string adapter_name, std::string backend, std::string surface_format) {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }

    m_gpu_ready = true;
    m_status.m_adapter_name = std::move(adapter_name);
    m_status.m_backend = std::move(backend);
    m_status.m_surface_format = std::move(surface_format);
    m_status.m_initialized = m_surface_configured && m_status.m_canvas_width > 0 && m_status.m_canvas_height > 0;
    clear_status_last_error(m_status);
}

// Records durable renderer resource counts for smoke/performance checks.
void Game::mark_renderer_counters(std::uint32_t pipeline_create_count, std::uint32_t buffer_create_count) {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }

    m_status.m_pipeline_create_count = pipeline_create_count;
    m_status.m_buffer_create_count = buffer_create_count;
    clear_status_last_error(m_status);
}

// Marks the platform target/surface as configured for the current nonzero size.
void Game::mark_surface_configured() {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (!m_gpu_ready) {
        const std::string message = "Game GPU device is not ready.";
        fail_runtime(message);
        throw EngineError(message);
    }
    if (m_status.m_canvas_width == 0 || m_status.m_canvas_height == 0) {
        m_surface_configured = false;
        m_status.m_initialized = false;
        clear_status_last_error(m_status);
        return;
    }

    if (!m_surface_configured) {
        m_status.m_surface_configure_count += 1;
    }
    m_surface_configured = true;
    m_status.m_initialized = true;
    clear_status_last_error(m_status);
}

// Records a recoverable runtime/render error while preserving ready resources.
void Game::record_error_impl(std::string message) noexcept {
    if (m_disposed) {
        fail_runtime("Game runtime has been disposed.");
        return;
    }
    fail_runtime(std::move(message));
}

// Records a GPU/device setup error and requires platform reinitialization.
void Game::record_gpu_error_impl(std::string message) noexcept {
    if (m_disposed) {
        fail_runtime("Game runtime has been disposed.");
        return;
    }
    m_gpu_ready = false;
    m_surface_configured = false;
    fail_runtime(std::move(message));
    set_state(GameLifecycleState::Failed);
}

// Makes the runtime inert while preserving useful diagnostic frame count.
void Game::dispose_runtime() noexcept {
    m_disposed = true;
    const std::uint64_t frame_count = m_status.m_frame_count;
    m_status = RuntimeDebugStatus::uninitialized("Game runtime has been disposed.");
    m_status.m_frame_count = frame_count;
    m_gpu_ready = false;
    m_surface_configured = false;
}

// Stores a recoverable failure reason.
void Game::fail_runtime(std::string message) noexcept {
    if (message.empty()) {
        message = "Unknown engine error.";
    }
    m_last_error = std::move(message);
    m_status.m_initialized = false;
    m_status.m_last_error = m_last_error;
}

// Updates this instance and its debug status to the given lifecycle state.
void Game::set_state(GameLifecycleState state) noexcept {
    m_state = state;
    m_status.m_lifecycle_state = game_lifecycle_state_name(state);
}

// Returns the live singleton or throws a clear lifecycle error.
Game& Game::require_game(const char* operation) {
    if (s_game == nullptr) {
        throw EngineError(std::string(operation) + " requires Game::create first.");
    }
    return *s_game;
}

// Records an exception message and marks the lifecycle as failed.
void Game::record_failed_exception_impl(std::string message) noexcept {
    fail_runtime(std::move(message));
    set_state(GameLifecycleState::Failed);
}

} // namespace ofg
