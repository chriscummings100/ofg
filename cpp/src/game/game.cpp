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
#include "ofg/resources/resources.hpp"
#include "ofg/scene/scene.hpp"

#include <cmath>
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

} // namespace

std::unique_ptr<Game> Game::s_game;

// Converts a Game lifecycle state into its debug-status string value.
const char* game_lifecycle_state_name(GameLifecycleState state) noexcept {
    switch (state) {
    case GameLifecycleState::Uninitialized:
        return "uninitialized";
    case GameLifecycleState::Created:
        return "created";
    case GameLifecycleState::Preparing:
        return "preparing";
    case GameLifecycleState::Ready:
        return "ready";
    case GameLifecycleState::Releasing:
        return "releasing";
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
        set_state(GameLifecycleState::Preparing);
        m_prepare_stage = PrepareStage::Resources;
        [[fallthrough]];
    case GameLifecycleState::Preparing:
        switch (m_prepare_stage) {
        case PrepareStage::NotStarted:
            m_prepare_stage = PrepareStage::Resources;
            [[fallthrough]];
        case PrepareStage::Resources:
            if (!Resources::prepare()) {
                return false;
            }
            m_prepare_stage = PrepareStage::Scene;
            [[fallthrough]];
        case PrepareStage::Scene: {
            DemoScene demo_scene;
            Scene scene;
            build_demo_scene(demo_scene);
            update_demo_scene(demo_scene, m_last_time_ms, m_aspect, scene);
            m_demo_scene = demo_scene;
            m_scene = std::move(scene);
            m_prepare_stage = PrepareStage::Renderer;
        }
            [[fallthrough]];
        case PrepareStage::Renderer: {
            if (!Renderer::prepare()) {
                return false;
            }
            if (m_status.m_canvas_width > 0 && m_status.m_canvas_height > 0) {
                Renderer::resize(m_status.m_canvas_width, m_status.m_canvas_height);
            }
            mark_gpu_ready(m_gpu.m_adapter_name, m_gpu.m_backend, gpu::texture_format_name(m_color_format));
            const RendererCounters counters = Renderer::counters();
            mark_renderer_counters(counters.m_pipeline_create_count, counters.m_buffer_create_count);
            m_prepare_stage = PrepareStage::Complete;
            set_state(GameLifecycleState::Ready);
            return true;
        }
        case PrepareStage::Complete:
            set_state(GameLifecycleState::Ready);
            return true;
        }
    case GameLifecycleState::Failed:
        throw EngineError("Game::prepare cannot continue while Game is failed: " + m_last_error);
    case GameLifecycleState::Releasing:
    case GameLifecycleState::Released:
        throw EngineError("Game::prepare cannot run after Game release has started.");
    case GameLifecycleState::Uninitialized:
        throw EngineError("Game::prepare requires Game::create first.");
    }
    throw EngineError("Game::prepare cannot run in an unknown lifecycle state.");
}

// Accepts the latest platform target size used for render validation.
void Game::resize_impl(std::uint32_t width, std::uint32_t height, double device_pixel_ratio) {
    if (m_state == GameLifecycleState::Releasing || m_state == GameLifecycleState::Released) {
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
    if (width > 0 && height > 0) {
        m_aspect = static_cast<float>(width) / static_cast<float>(height);
        if (renderer_ready) {
            update_demo_scene(m_demo_scene, m_last_time_ms, m_aspect, m_scene);
        }
    }
}

// Advances shared per-frame state.
void Game::update_impl(double time_ms) {
    if (m_state != GameLifecycleState::Ready) {
        throw EngineError("Game::update requires Game::prepare to complete first.");
    }

    tick_runtime(time_ms);
    m_last_time_ms = time_ms;
    update_demo_scene(m_demo_scene, m_last_time_ms, m_aspect, m_scene);
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

    mark_surface_configured();
    Renderer::render(encoder, target, m_scene);
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
    case GameLifecycleState::Preparing:
    case GameLifecycleState::Ready:
    case GameLifecycleState::Failed:
        set_state(GameLifecycleState::Releasing);
        m_release_stage = ReleaseStage::Renderer;
        [[fallthrough]];
    case GameLifecycleState::Releasing:
        switch (m_release_stage) {
        case ReleaseStage::NotStarted:
            m_release_stage = ReleaseStage::Renderer;
            [[fallthrough]];
        case ReleaseStage::Renderer:
            if (!Renderer::release()) {
                return false;
            }
            m_scene.clear();
            m_demo_scene = DemoScene{};
            m_release_stage = ReleaseStage::Resources;
            [[fallthrough]];
        case ReleaseStage::Resources:
            if (!Resources::release()) {
                return false;
            }
            dispose_runtime();
            m_gpu = GpuContext{};
            m_color_format = WGPUTextureFormat_Undefined;
            m_release_stage = ReleaseStage::Complete;
            set_state(GameLifecycleState::Released);
            return true;
        case ReleaseStage::Complete:
            set_state(GameLifecycleState::Released);
            return true;
        }
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
    m_status.m_last_error.reset();
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
    m_status.m_last_error.reset();
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
    m_status.m_last_error.reset();
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
    m_status.m_last_error.reset();
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
        m_status.m_last_error.reset();
        return;
    }

    if (!m_surface_configured) {
        m_status.m_surface_configure_count += 1;
    }
    m_surface_configured = true;
    m_status.m_initialized = true;
    m_status.m_last_error.reset();
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
