// Shared OFG game/render frame object.
#include "ofg/game/game.hpp"

#include "ofg/render/webgpu_common.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ofg {

// Stores a created renderer and borrowed platform WebGPU handles.
Game::Game(GpuContext gpu, WGPUTextureFormat color_format, std::unique_ptr<BootstrapRenderer> renderer)
    : m_gpu(gpu), m_color_format(color_format), m_renderer(std::move(renderer)) {}

// Releases durable renderer resources before platform device handles go away.
Game::~Game() = default;

// Creates device-owned renderer resources for one platform WebGPU lifetime.
std::unique_ptr<Game> Game::create(GpuContext gpu, WGPUTextureFormat color_format, std::string& error) {
    if (color_format == WGPUTextureFormat_Undefined) {
        error = "Game requires a defined color format.";
        return nullptr;
    }
    if (gpu.m_device == nullptr || gpu.m_queue == nullptr) {
        error = "Game requires a WebGPU device and queue.";
        return nullptr;
    }

    std::unique_ptr<BootstrapRenderer> renderer =
        BootstrapRenderer::create(gpu.m_device, gpu.m_queue, color_format, error);
    if (!renderer) {
        return nullptr;
    }

    std::unique_ptr<Game> game(new Game(gpu, color_format, std::move(renderer)));
    std::string runtime_error;
    (void)game->m_runtime.mark_gpu_ready(
        gpu.m_adapter_name, gpu.m_backend, gpu::texture_format_name(color_format), runtime_error);
    const RendererCounters counters = game->m_renderer->counters();
    (void)game->m_runtime.mark_renderer_counters(
        counters.m_pipeline_create_count, counters.m_buffer_create_count, runtime_error);
    error.clear();
    return game;
}

// Accepts the latest platform target size used for render validation.
bool Game::resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio, std::string& error) {
    return m_runtime.resize(width, height, device_pixel_ratio, error);
}

// Advances shared per-frame state.
bool Game::tick(double time_ms, std::string& error) {
    return m_runtime.tick(time_ms, error);
}

// Records render commands into the caller-owned command encoder.
bool Game::render(WGPUCommandEncoder encoder, RenderTarget target, std::string& error) {
    if (m_runtime.disposed()) {
        error = "Game runtime has been disposed.";
        (void)m_runtime.mark_error(error);
        return false;
    }
    if (encoder == nullptr) {
        error = "Game render requires a command encoder.";
        (void)m_runtime.mark_error(error);
        return false;
    }
    const RuntimeDebugStatus& current_status = m_runtime.status();
    if (!validate_render_target(
            target, m_color_format, current_status.m_canvas_width, current_status.m_canvas_height, error)) {
        (void)m_runtime.mark_error(error);
        return false;
    }
    if (!m_renderer) {
        error = "Game renderer resources are not initialized.";
        (void)m_runtime.mark_error(error);
        return false;
    }

    if (!m_runtime.mark_surface_configured(error)) {
        return false;
    }
    if (!m_renderer->render_to_view(encoder, target.m_view, error)) {
        (void)m_runtime.mark_error(error);
        return false;
    }
    error.clear();
    return true;
}

// Records a recoverable platform/runtime error in shared debug status.
bool Game::record_error(std::string message) {
    return m_runtime.mark_error(std::move(message));
}

// Records a GPU/device error that requires platform reinitialization.
bool Game::record_gpu_error(std::string message) {
    return m_runtime.mark_gpu_error(std::move(message));
}

// Returns browser-facing debug status JSON.
std::string Game::debug_status_json() const {
    return m_runtime.debug_status_json();
}

// Returns the current debug status snapshot.
const RuntimeDebugStatus& Game::status() const noexcept {
    return m_runtime.status();
}

// Releases durable renderer resources and blocks later mutation.
void Game::dispose() {
    m_renderer.reset();
    m_runtime.dispose();
    m_gpu = GpuContext{};
    m_color_format = WGPUTextureFormat_Undefined;
}

} // namespace ofg
