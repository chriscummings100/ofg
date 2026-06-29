// Static OFG game lifecycle facade.
//
// Game owns portable frame state and orchestrates renderer resources for one
// WebGPU device lifetime. Browser and native frame drivers provide per-frame
// targets, command encoders, finish/submit work, presentation, readback, and
// platform handle ownership.
#pragma once

#include "ofg/core/frame_state.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/runtime/runtime_debug_status.hpp"
#include "ofg/scene/scene.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

enum class GameLifecycleState {
    Uninitialized,
    Created,
    Prep_Resources,
    Prep_Scene,
    Prep_Renderer,
    Ready,
    Rel_Renderer,
    Rel_Scene,
    Rel_Resources,
    Released,
    Failed,
};

// Converts a Game lifecycle state into its debug-status string value.
[[nodiscard]] const char* game_lifecycle_state_name(GameLifecycleState state) noexcept;

class Game {
public:
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    ~Game();

    // Creates the singleton for one platform WebGPU lifetime.
    static void create(GpuContext gpu, WGPUTextureFormat color_format);
    // Advances startup work and reports whether Game is ready.
    [[nodiscard]] static bool prepare();
    // Accepts the latest platform target size used for render validation.
    static void resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    // Advances shared per-frame state.
    static void update(double time_ms);
    // Records render commands into the caller-owned command encoder.
    static void render(WGPUCommandEncoder encoder, RenderTarget target);
    // Advances teardown work and reports whether Game has released resources.
    [[nodiscard]] static bool release();
    // Destroys the singleton after release has completed.
    static void destroy() noexcept;
    // Returns the current high-level lifecycle state.
    [[nodiscard]] static GameLifecycleState state() noexcept;
    // Returns the most recently recorded Game-level error.
    [[nodiscard]] static const std::string& last_error() noexcept;
    // Records a recoverable platform/runtime error in shared debug status.
    static void record_error(std::string message) noexcept;
    // Records a GPU/device error that requires platform reinitialization.
    static void record_gpu_error(std::string message) noexcept;
    // Returns browser-facing debug status JSON.
    [[nodiscard]] static std::string debug_status_json();
    // Returns the current debug status snapshot.
    [[nodiscard]] static const RuntimeDebugStatus& status() noexcept;

private:
    // Stores borrowed platform WebGPU handles and lifecycle state.
    Game(GpuContext gpu, WGPUTextureFormat color_format);

    // Advances the renderer/resource preparation state machine.
    [[nodiscard]] bool prepare_impl();
    // Accepts the latest platform target size used for render validation.
    void resize_impl(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    // Advances shared per-frame state.
    void update_impl(double time_ms);
    // Records render commands into the caller-owned command encoder.
    void render_impl(WGPUCommandEncoder encoder, RenderTarget target);
    // Advances the renderer/resource release state machine.
    [[nodiscard]] bool release_impl();
    // Destroys owned systems after release has completed.
    void destroy_impl() noexcept;
    // Accepts a new physical target size and device pixel ratio.
    void resize_runtime(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    // Advances frame state after validating the frame timestamp.
    void tick_runtime(double time_ms);
    // Marks the shared GPU renderer path as ready.
    void mark_gpu_ready(std::string adapter_name, std::string backend, std::string surface_format);
    // Records durable renderer resource counts for smoke/performance checks.
    void mark_renderer_counters(std::uint32_t pipeline_create_count, std::uint32_t buffer_create_count);
    // Marks the platform target/surface as configured for the current nonzero size.
    void mark_surface_configured();
    // Records a recoverable runtime/render error while preserving ready resources.
    void record_error_impl(std::string message) noexcept;
    // Records a GPU/device setup error and requires platform reinitialization.
    void record_gpu_error_impl(std::string message) noexcept;
    // Makes the runtime inert while preserving useful diagnostic frame count.
    void dispose_runtime() noexcept;
    // Stores a recoverable failure reason.
    void fail_runtime(std::string message) noexcept;
    // Updates this instance and its debug status to the given lifecycle state.
    void set_state(GameLifecycleState state) noexcept;

    // Returns the live singleton or throws a clear lifecycle error.
    [[nodiscard]] static Game& require_game(const char* operation);
    // Records an exception message and marks the lifecycle as failed.
    void record_failed_exception_impl(std::string message) noexcept;

    static std::unique_ptr<Game> s_game;

    GpuContext m_gpu;
    WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
    GameLifecycleState m_state{GameLifecycleState::Uninitialized};
    FrameState m_frame_state;
    RuntimeDebugStatus m_status;
    std::string m_last_error;
    DemoScene m_demo_scene;
    Scene m_scene;
    double m_last_time_ms{0.0};
    float m_aspect{16.0F / 9.0F};
    bool m_disposed{false};
    bool m_gpu_ready{false};
    bool m_surface_configured{false};
};

} // namespace ofg
