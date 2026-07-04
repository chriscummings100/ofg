// Static high-level OFG renderer facade.
//
// Renderer owns pass-level GPU state for one WebGPU device lifetime. Platform
// frame drivers still own target acquisition, command-buffer finish, queue
// submit, and presentation; Game owns scene state and passes an explicit Scene
// to Renderer for each frame.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/bloom_pass.hpp"
#include "ofg/render/bloom_settings.hpp"
#include "ofg/render/depth_target.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/opaque_pass.hpp"
#include "ofg/render/scene_color_target.hpp"
#include "ofg/render/sky_pass.hpp"
#include "ofg/render/temp_buffer.hpp"
#include "ofg/render/tone_map_pass.hpp"
#include "ofg/scene/scene.hpp"

#include <memory>

#include <webgpu/webgpu.h>

namespace ofg {

enum class RendererLifecycleState {
    Uninitialized,
    Created,
    Preparing,
    Ready,
    Releasing,
    Released,
    Failed,
};

// Converts a Renderer lifecycle state into its debug/status string value.
[[nodiscard]] const char* renderer_lifecycle_state_name(RendererLifecycleState state) noexcept;

class Renderer {
public:
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    // Creates the renderer singleton for one WebGPU device and color target format.
    static void create(GpuContext gpu, WGPUTextureFormat color_format);
    // Advances renderer startup work and reports whether Renderer is ready.
    [[nodiscard]] static bool prepare();
    // Resizes pass-level render targets.
    static void resize(std::uint32_t width, std::uint32_t height);
    // Records all renderer passes into the caller-owned command encoder.
    static void render(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene);
    // Advances renderer teardown work and reports whether resources are released.
    [[nodiscard]] static bool release();
    // Destroys the renderer singleton after release has completed.
    static void destroy() noexcept;
    // Returns the current renderer lifecycle state.
    [[nodiscard]] static RendererLifecycleState state() noexcept;
    // Reports durable resource creation counters.
    [[nodiscard]] static RendererCounters counters() noexcept;
    // Reports the most recent bloom pass diagnostics.
    [[nodiscard]] static BloomPassDiagnostics bloom_diagnostics() noexcept;
    // Reports current temp-buffer memory and reuse diagnostics.
    [[nodiscard]] static TempBufferStats temp_buffer_stats() noexcept;

private:
    // Stores borrowed platform WebGPU handles for pass creation.
    Renderer(GpuContext gpu, WGPUTextureFormat color_format);

    // Advances the internal pass-list preparation state machine.
    [[nodiscard]] bool prepare_impl();
    // Resizes pass-level render targets.
    void resize_impl(std::uint32_t width, std::uint32_t height);
    // Records all prepared passes into the caller-owned command encoder.
    void render_impl(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene);
    // Advances the pass-resource release state machine.
    [[nodiscard]] bool release_impl();
    // Returns the live singleton or throws a clear lifecycle error.
    [[nodiscard]] static Renderer& require_renderer(const char* operation);
    // Updates this instance lifecycle state.
    void set_state(RendererLifecycleState state) noexcept;

    static std::unique_ptr<Renderer> s_renderer;

    GpuContext m_gpu;
    WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
    RendererLifecycleState m_state{RendererLifecycleState::Uninitialized};
    DrawList m_draw_list;
    std::unique_ptr<SceneColorTarget> m_scene_color_target;
    std::unique_ptr<DepthTarget> m_depth_target;
    std::unique_ptr<OpaquePass> m_opaque_pass;
    std::unique_ptr<SkyPass> m_sky_pass;
    std::unique_ptr<BloomPass> m_bloom_pass;
    BloomSettings m_bloom_settings;
    std::unique_ptr<ToneMapPass> m_tone_map_pass;
};

} // namespace ofg
