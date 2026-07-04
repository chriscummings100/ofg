// Static high-level OFG renderer facade.
//
// Renderer owns pass-level GPU state for one WebGPU device lifetime. Platform
// frame drivers still own target acquisition, command-buffer finish, queue
// submit, and presentation; Game owns scene state and passes an explicit Scene
// to Renderer for each frame.
#pragma once

#include "ofg/debug/debug_ui.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/bloom_pass.hpp"
#include "ofg/render/bloom_settings.hpp"
#include "ofg/render/depth_target.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/opaque_pass.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/scene_color_target.hpp"
#include "ofg/render/shadow_caster_pass.hpp"
#include "ofg/render/shadow_debug_pass.hpp"
#include "ofg/render/shadow_diagnostics.hpp"
#include "ofg/render/shadow_map_target.hpp"
#include "ofg/render/shadow_settings.hpp"
#include "ofg/render/sky_pass.hpp"
#include "ofg/render/temp_buffer.hpp"
#include "ofg/render/tone_map_pass.hpp"
#include "ofg/scene/scene.hpp"

#include <memory>
#include <vector>

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
    static void render(WGPUCommandEncoder encoder,
        RenderTarget target,
        const Scene& scene,
        DebugUiFrameInfo debug_ui_frame_info = DebugUiFrameInfo{});
    // Advances renderer teardown work and reports whether resources are released.
    [[nodiscard]] static bool release();
    // Destroys the renderer singleton after release has completed.
    static void destroy() noexcept;
    // Returns the current renderer lifecycle state.
    [[nodiscard]] static RendererLifecycleState state() noexcept;
    // Reports durable resource creation counters.
    [[nodiscard]] static RendererCounters counters() noexcept;
    // Reports the most recent render-object culling stats.
    [[nodiscard]] static RendererCullingStats culling_stats() noexcept;
    // Reports the most recent shadow pass diagnostics.
    [[nodiscard]] static ShadowPassDiagnostics shadow_diagnostics() noexcept;
    // Reports the most recent bloom pass diagnostics.
    [[nodiscard]] static BloomPassDiagnostics bloom_diagnostics() noexcept;
    // Reports current temp-buffer memory and reuse diagnostics.
    [[nodiscard]] static TempBufferStats temp_buffer_stats() noexcept;
    // Reports the most recent renderer-owned debug UI diagnostics.
    [[nodiscard]] static DebugUiStatus debug_ui_status() noexcept;
    // Enables or disables the debug sun lock with light travelling straight down.
    static void set_overhead_sun_debug_enabled(bool enabled);
    // Reports whether the debug overhead-sun lock is active.
    [[nodiscard]] static bool overhead_sun_debug_enabled() noexcept;

private:
    // Stores borrowed platform WebGPU handles for pass creation.
    Renderer(GpuContext gpu, WGPUTextureFormat color_format);

    // Advances the internal pass-list preparation state machine.
    [[nodiscard]] bool prepare_impl();
    // Resizes pass-level render targets.
    void resize_impl(std::uint32_t width, std::uint32_t height);
    // Records all prepared passes into the caller-owned command encoder.
    void render_impl(WGPUCommandEncoder encoder,
        RenderTarget target,
        const Scene& scene,
        const DebugUiFrameInfo& debug_ui_frame_info);
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
    std::vector<RenderObject> m_render_objects;
    RendererCullingStats m_culling_stats;
    std::unique_ptr<SceneColorTarget> m_scene_color_target;
    std::unique_ptr<DepthTarget> m_depth_target;
    std::unique_ptr<ShadowMapTarget> m_shadow_map_target;
    std::unique_ptr<ShadowCasterPass> m_shadow_caster_pass;
    std::unique_ptr<ShadowDebugPass> m_shadow_debug_pass;
    ShadowSettings m_shadow_settings;
    ShadowPassDiagnostics m_shadow_diagnostics;
    std::unique_ptr<OpaquePass> m_opaque_pass;
    std::unique_ptr<SkyPass> m_sky_pass;
    std::unique_ptr<BloomPass> m_bloom_pass;
    BloomSettings m_bloom_settings;
    std::unique_ptr<ToneMapPass> m_tone_map_pass;
    std::unique_ptr<DebugUi> m_debug_ui;
    bool m_overhead_sun_debug_enabled{false};
};

} // namespace ofg
