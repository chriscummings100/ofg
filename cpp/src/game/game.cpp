// Static OFG game lifecycle facade.
//
// Game owns portable frame/debug state and orchestrates Resources and Renderer
// for one WebGPU device lifetime. Browser and native frame drivers provide
// targets, command encoders, finish/submit work, presentation, and platform
// handle ownership.
#include "ofg/game/game.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/render/renderer.hpp"
#include "ofg/render/shadow_diagnostics.hpp"
#include "ofg/render/shadow_settings.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"
#include "ofg/terrain/terrain_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

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

// Converts demo-scene validation stats into the public runtime debug payload.
RuntimeDemoSceneStatus runtime_demo_scene_status_from(DemoSceneValidationStats stats) {
    return RuntimeDemoSceneStatus{"large-default-culling-shadow-validation",
        stats.m_box_count,
        stats.m_near_box_count,
        stats.m_mid_box_count,
        stats.m_far_box_count,
        stats.m_partly_below_ground_count,
        stats.m_overlap_cluster_box_count,
        stats.m_off_camera_candidate_count};
}

// Converts renderer shadow diagnostics into the public runtime debug payload.
RuntimeShadowStatus runtime_shadow_status_from(const ShadowPassDiagnostics& diagnostics) {
    RuntimeShadowStatus status;
    status.m_enabled = diagnostics.m_enabled;
    status.m_cascade_count = diagnostics.m_cascade_count;
    status.m_encoded_pass_count = diagnostics.m_encoded_pass_count;
    status.m_map_size = diagnostics.m_map_size;
    status.m_estimated_depth_bytes = diagnostics.m_estimated_depth_bytes;
    status.m_pcf_mode = shadow_pcf_mode_name(diagnostics.m_pcf_mode);
    status.m_pcf_sample_count = diagnostics.m_pcf_sample_count;
    status.m_sun_elevation_radians = diagnostics.m_sun_elevation_radians;
    status.m_effective_intensity = diagnostics.m_effective_intensity;
    status.m_low_sun_clamped = diagnostics.m_low_sun_clamped;
    for (std::size_t index = 0; index < diagnostics.m_cascades.size(); ++index) {
        const ShadowCascadeDiagnostics& cascade = diagnostics.m_cascades[index];
        status.m_cascades[index] = RuntimeShadowCascadeStatus{cascade.m_index,
            cascade.m_tested_caster_count,
            cascade.m_accepted_caster_count,
            cascade.m_rejected_caster_count,
            cascade.m_draw_count,
            cascade.m_submesh_count,
            cascade.m_index_count};
    }
    status.m_total_tested_caster_count = diagnostics.m_total_tested_caster_count;
    status.m_total_accepted_caster_count = diagnostics.m_total_accepted_caster_count;
    status.m_total_rejected_caster_count = diagnostics.m_total_rejected_caster_count;
    status.m_total_draw_count = diagnostics.m_total_draw_count;
    status.m_total_submesh_count = diagnostics.m_total_submesh_count;
    status.m_total_index_count = diagnostics.m_total_index_count;
    return status;
}

// Converts renderer-owned ImGui diagnostics into the public runtime debug payload.
RuntimeDebugUiStatus runtime_debug_ui_status_from(const DebugUiStatus& diagnostics) {
    RuntimeDebugUiStatus status;
    status.m_visible = diagnostics.m_visible;
    status.m_wants_capture_mouse = diagnostics.m_wants_capture_mouse;
    status.m_wants_capture_keyboard = diagnostics.m_wants_capture_keyboard;
    status.m_overlay_pass_count = diagnostics.m_overlay_pass_count;
    status.m_menu_tree_generation = diagnostics.m_menu_tree_generation;
    status.m_menu_tree_rebuild_count = diagnostics.m_menu_tree_rebuild_count;
    status.m_draw_list_count = diagnostics.m_draw_list_count;
    status.m_draw_command_count = diagnostics.m_draw_command_count;
    status.m_vertex_count = diagnostics.m_vertex_count;
    status.m_index_count = diagnostics.m_index_count;
    status.m_uploaded_vertex_bytes = diagnostics.m_uploaded_vertex_bytes;
    status.m_uploaded_index_bytes = diagnostics.m_uploaded_index_bytes;
    status.m_vertex_buffer_capacity = diagnostics.m_vertex_buffer_capacity;
    status.m_index_buffer_capacity = diagnostics.m_index_buffer_capacity;
    status.m_vertex_buffer_resize_count = diagnostics.m_vertex_buffer_resize_count;
    status.m_index_buffer_resize_count = diagnostics.m_index_buffer_resize_count;
    status.m_font_texture_create_count = diagnostics.m_font_texture_create_count;
    return status;
}

constexpr float _max_delta_seconds = 0.1f;

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

// Accepts the latest raw debug UI input snapshot.
void Game::set_debug_ui_input(DebugUiInput input) {
    try {
        require_game("Game::set_debug_ui_input").set_debug_ui_input_impl(std::move(input));
    } catch (const std::exception& error) {
        if (s_game != nullptr) {
            s_game->record_error_impl(error.what());
        }
        throw;
    } catch (...) {
        if (s_game != nullptr) {
            s_game->record_error_impl("Game::set_debug_ui_input failed with an unknown exception.");
        }
        throw;
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
        update_demo_scene(m_demo_scene, m_last_time_ms, *m_current_scene);
        m_status.m_demo_scene = runtime_demo_scene_status_from(demo_scene_validation_stats());
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
        m_status.m_debug_ui = runtime_debug_ui_status_from(Renderer::debug_ui_status());
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
    m_last_delta_seconds = delta_seconds > 0.0F ? delta_seconds : 1.0F / 60.0F;
    if (m_current_scene == nullptr) {
        throw EngineError("Game update requires a current scene.");
    }
    update_demo_scene(m_demo_scene, time_ms, *m_current_scene);
    if (m_control_input.m_toggle_overhead_sun) {
        Renderer::set_overhead_sun_debug_enabled(!Renderer::overhead_sun_debug_enabled());
    }
    Resources::advance_loads();
    Player* primary_player = m_current_scene->player_count() == 0 ? nullptr : m_current_scene->get_player(0);
    Camera* main_camera = m_current_scene->main_camera();
    ControlInput scene_control_input = m_control_input;
    const DebugUiStatus debug_ui_status = Renderer::debug_ui_status();
    if (debug_ui_status.m_visible &&
        (debug_ui_status.m_wants_capture_mouse || debug_ui_status.m_wants_capture_keyboard)) {
        scene_control_input = ControlInput{};
    }
    SceneUpdateContext context{
        scene_control_input, time_ms, delta_seconds, primary_player, main_camera, m_current_scene.get(), m_gpu};
    m_current_scene->update(context);
    sync_terrain_debug_scene(m_demo_scene.m_terrain_resources, *m_current_scene, *m_current_scene->get_root());
    if (primary_player != nullptr) {
        primary_player->publish_default_model_debug_status(m_status, m_last_error);
    }
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

// Stores a raw debug UI input snapshot for the next render.
void Game::set_debug_ui_input_impl(DebugUiInput input) {
    if (m_state == GameLifecycleState::Failed) {
        throw EngineError("Game::set_debug_ui_input cannot run while Game is failed: " + m_last_error);
    }
    if (m_state == GameLifecycleState::Rel_Renderer || m_state == GameLifecycleState::Rel_Scene ||
        m_state == GameLifecycleState::Rel_Resources || m_state == GameLifecycleState::Released) {
        throw EngineError("Game::set_debug_ui_input cannot run after Game release has started.");
    }
    m_debug_ui_input = std::move(input);
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
    DebugUiFrameInfo debug_ui_frame_info;
    debug_ui_frame_info.m_delta_seconds = m_last_delta_seconds;
    debug_ui_frame_info.m_device_pixel_ratio = static_cast<float>(m_status.m_device_pixel_ratio);
    debug_ui_frame_info.m_input = m_debug_ui_input;
    Renderer::render(encoder, target, *m_current_scene, debug_ui_frame_info);
    clear_debug_ui_input_transients(m_debug_ui_input);
    const RendererCounters counters = Renderer::counters();
    mark_renderer_counters(counters.m_pipeline_create_count, counters.m_buffer_create_count);
    mark_renderer_diagnostics(Renderer::culling_stats(),
        Renderer::shadow_diagnostics(),
        Renderer::bloom_diagnostics(),
        Renderer::temp_buffer_stats(),
        Renderer::debug_ui_status());
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
        m_debug_ui_input = DebugUiInput{};
        m_current_scene.reset();
        m_has_last_time = false;
        m_last_delta_seconds = 1.0F / 60.0F;
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
    m_status.clear_transient_error();
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
    m_status.clear_transient_error();
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
    m_control_input.m_toggle_overhead_sun = false;
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
    m_status.clear_transient_error();
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
    m_status.clear_transient_error();
}

// Records the most recent culling, shadow, bloom, and temp-buffer diagnostics.
void Game::mark_renderer_diagnostics(RendererCullingStats culling,
    const ShadowPassDiagnostics& shadow,
    const BloomPassDiagnostics& bloom,
    const TempBufferStats& temp_buffers,
    const DebugUiStatus& debug_ui) {
    if (m_disposed) {
        const std::string message = "Game runtime has been disposed.";
        fail_runtime(message);
        throw EngineError(message);
    }

    m_status.m_render_culling = RuntimeRenderCullingStatus{
        culling.m_extracted_object_count, culling.m_camera_visible_object_count, culling.m_camera_culled_object_count};
    m_status.m_shadow = runtime_shadow_status_from(shadow);
    m_status.m_debug_ui = runtime_debug_ui_status_from(debug_ui);
    m_status.m_bloom_active_level_count = bloom.m_active_level_count;
    m_status.m_bloom_encoded_pass_count = bloom.m_encoded_pass_count;
    m_status.m_bloom_draw_count = bloom.m_draw_count;
    m_status.m_bloom_estimated_read_bytes = bloom.m_estimated_read_bytes;
    m_status.m_bloom_estimated_write_bytes = bloom.m_estimated_write_bytes;
    m_status.m_bloom_skipped = bloom.m_skipped;
    m_status.m_temp_buffer_active_bytes = temp_buffers.m_active_bytes;
    m_status.m_temp_buffer_reusable_bytes = temp_buffers.m_reusable_bytes;
    m_status.m_temp_buffer_peak_bytes = temp_buffers.m_peak_bytes;
    m_status.m_temp_buffer_created_count = temp_buffers.m_created_count;
    m_status.m_temp_buffer_reused_count = temp_buffers.m_reused_count;
    m_status.m_temp_buffer_discarded_count = temp_buffers.m_discarded_count;
    m_status.m_temp_buffer_active_count = temp_buffers.m_active_count;
    m_status.m_temp_buffer_reusable_count = temp_buffers.m_reusable_count;
    m_status.m_temp_buffer_early_release_count = temp_buffers.m_early_release_count;
    m_status.m_temp_buffer_end_frame_return_count = temp_buffers.m_end_frame_return_count;
    m_status.clear_transient_error();
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
        m_status.clear_transient_error();
        return;
    }

    if (!m_surface_configured) {
        m_status.m_surface_configure_count += 1;
    }
    m_surface_configured = true;
    m_status.m_initialized = true;
    m_status.clear_transient_error();
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
