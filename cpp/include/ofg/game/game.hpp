// Shared OFG game/render frame object.
//
// Game owns portable frame state and durable renderer resources for one WebGPU
// device lifetime. Browser and native frame drivers provide per-frame targets,
// command encoders, finish/submit work, presentation, readback, and platform
// handle ownership.
#pragma once

#include "ofg/game/game_runtime.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/renderer.hpp"
#include "ofg/resources/resource_arena.hpp"
#include "ofg/runtime/runtime_debug_status.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

class Game {
public:
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    ~Game();

    // Creates device-owned renderer resources for one platform WebGPU lifetime.
    [[nodiscard]] static std::unique_ptr<Game> create(
        GpuContext gpu, WGPUTextureFormat color_format, std::string& error);

    // Accepts the latest platform target size used for render validation.
    bool resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio, std::string& error);
    // Advances shared per-frame state.
    bool tick(double time_ms, std::string& error);
    // Records render commands into the caller-owned command encoder.
    bool render(WGPUCommandEncoder encoder, RenderTarget target, std::string& error);
    // Records a recoverable platform/runtime error in shared debug status.
    bool record_error(std::string message);
    // Records a GPU/device error that requires platform reinitialization.
    bool record_gpu_error(std::string message);
    // Returns browser-facing debug status JSON.
    [[nodiscard]] std::string debug_status_json() const;
    // Returns the current debug status snapshot.
    [[nodiscard]] const RuntimeDebugStatus& status() const noexcept;
    // Releases durable renderer resources and blocks later mutation.
    void dispose();

private:
    // Stores created resources, renderer, and borrowed platform WebGPU handles.
    Game(GpuContext gpu,
        WGPUTextureFormat color_format,
        ResourceArena resources,
        DemoScene demo_scene,
        DrawList draw_list,
        RenderView render_view,
        std::unique_ptr<Renderer> renderer);

    GpuContext m_gpu;
    WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
    GameRuntime m_runtime;
    ResourceArena m_resources;
    DemoScene m_demo_scene;
    DrawList m_draw_list;
    RenderView m_render_view;
    std::unique_ptr<Renderer> m_renderer;
    double m_last_time_ms{0.0};
    float m_aspect{16.0F / 9.0F};
};

} // namespace ofg
