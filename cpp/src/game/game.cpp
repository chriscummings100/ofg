// Shared OFG game/render frame object.
#include "ofg/game/game.hpp"

#include "ofg/render/demo_scene.hpp"
#include "ofg/render/webgpu_common.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace ofg {

// Stores created resources, renderer, and borrowed platform WebGPU handles.
Game::Game(GpuContext gpu,
    WGPUTextureFormat color_format,
    ResourceArena resources,
    DemoScene demo_scene,
    DrawList draw_list,
    RenderView render_view,
    std::unique_ptr<Renderer> renderer)
    : m_gpu(gpu), m_color_format(color_format), m_resources(std::move(resources)), m_demo_scene(demo_scene),
      m_draw_list(std::move(draw_list)), m_render_view(render_view), m_renderer(std::move(renderer)) {}

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

    std::unique_ptr<Renderer> renderer = Renderer::create(gpu, color_format, error);
    if (!renderer) {
        return nullptr;
    }

    ResourceArena resources;
    DemoScene demo_scene;
    DrawList draw_list;
    RenderView render_view;
    if (!build_demo_scene(gpu, resources, demo_scene, error)) {
        return nullptr;
    }
    if (!update_demo_scene(demo_scene, 0.0, 16.0F / 9.0F, draw_list, render_view, error)) {
        return nullptr;
    }
    if (!renderer->prepare(draw_list, error)) {
        return nullptr;
    }

    std::unique_ptr<Game> game(new Game(
        gpu, color_format, std::move(resources), demo_scene, std::move(draw_list), render_view, std::move(renderer)));
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
    if (!m_runtime.resize(width, height, device_pixel_ratio, error)) {
        return false;
    }
    if (m_renderer && !m_renderer->resize(width, height, error)) {
        (void)m_runtime.mark_error(error);
        return false;
    }
    if (width > 0 && height > 0) {
        m_aspect = static_cast<float>(width) / static_cast<float>(height);
        if (!update_demo_scene(m_demo_scene, m_last_time_ms, m_aspect, m_draw_list, m_render_view, error)) {
            (void)m_runtime.mark_error(error);
            return false;
        }
    }
    error.clear();
    return true;
}

// Advances shared per-frame state.
bool Game::tick(double time_ms, std::string& error) {
    if (!m_runtime.tick(time_ms, error)) {
        return false;
    }
    m_last_time_ms = time_ms;
    if (!update_demo_scene(m_demo_scene, m_last_time_ms, m_aspect, m_draw_list, m_render_view, error)) {
        (void)m_runtime.mark_error(error);
        return false;
    }
    error.clear();
    return true;
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
    if (!m_renderer->render(encoder, target, m_render_view, m_draw_list, error)) {
        (void)m_runtime.mark_error(error);
        return false;
    }
    const RendererCounters counters = m_renderer->counters();
    std::string runtime_error;
    (void)m_runtime.mark_renderer_counters(
        counters.m_pipeline_create_count, counters.m_buffer_create_count, runtime_error);
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
    m_draw_list.clear();
    m_resources.clear();
    m_demo_scene = DemoScene{};
    m_runtime.dispose();
    m_gpu = GpuContext{};
    m_color_format = WGPUTextureFormat_Undefined;
}

} // namespace ofg
