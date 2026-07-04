// Static high-level OFG renderer facade.
#include "ofg/render/renderer.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bootstrap_scene.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/frustum.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_frame_state.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/scene.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace ofg {
namespace {

// Converts the shared renderer clear color into WebGPU descriptor form.
WGPUColor webgpu_clear_color() noexcept {
    const ClearColor clear = clear_color();
    WGPUColor color = WGPU_COLOR_INIT;
    color.r = clear.m_r;
    color.g = clear.m_g;
    color.b = clear.m_b;
    color.a = clear.m_a;
    return color;
}

// Returns the first renderer-facing directional light, if one is live.
const LightProperties* first_directional_light(std::span<const LightProperties> lights) noexcept {
    for (const LightProperties& light : lights) {
        if (light.m_type == LightPropertiesType::Directional) {
            return &light;
        }
    }
    return nullptr;
}

// Replaces the first directional light direction with a straight-down debug sun.
void apply_overhead_sun_debug(std::span<LightProperties> lights) noexcept {
    for (LightProperties& light : lights) {
        if (light.m_type == LightPropertiesType::Directional) {
            light.m_direction = math::vec3(0.0f, -1.0f, 0.0f);
            return;
        }
    }
}

} // namespace

std::unique_ptr<Renderer> Renderer::s_renderer;

// Converts a Renderer lifecycle state into its debug/status string value.
const char* renderer_lifecycle_state_name(RendererLifecycleState state) noexcept {
    switch (state) {
    case RendererLifecycleState::Uninitialized:
        return "uninitialized";
    case RendererLifecycleState::Created:
        return "created";
    case RendererLifecycleState::Preparing:
        return "preparing";
    case RendererLifecycleState::Ready:
        return "ready";
    case RendererLifecycleState::Releasing:
        return "releasing";
    case RendererLifecycleState::Released:
        return "released";
    case RendererLifecycleState::Failed:
        return "failed";
    }
    return "unknown";
}

// Stores borrowed platform WebGPU handles for pass creation.
Renderer::Renderer(GpuContext gpu, WGPUTextureFormat color_format) : m_gpu(gpu), m_color_format(color_format) {}

// Releases pass resources and the renderer-owned temp-buffer singleton.
Renderer::~Renderer() {
    m_tone_map_pass.reset();
    m_bloom_pass.reset();
    (void)TempBuffer::release();
    TempBuffer::destroy();
}

// Creates the renderer singleton for one WebGPU device and color target format.
void Renderer::create(GpuContext gpu, WGPUTextureFormat color_format) {
    if (s_renderer != nullptr) {
        throw EngineError("Renderer::create cannot be called while a Renderer singleton is live.");
    }
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("Renderer requires a WebGPU device and queue.");
    }
    if (color_format == WGPUTextureFormat_Undefined) {
        throw EngineError("Renderer requires a defined color format.");
    }

    std::unique_ptr<Renderer> renderer(new Renderer(std::move(gpu), color_format));
    TempBuffer::create(renderer->m_gpu);
    s_renderer = std::move(renderer);
    s_renderer->set_state(RendererLifecycleState::Created);
}

// Advances renderer startup work and reports whether Renderer is ready.
bool Renderer::prepare() {
    return require_renderer("Renderer::prepare").prepare_impl();
}

// Resizes pass-level render targets.
void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    require_renderer("Renderer::resize").resize_impl(width, height);
}

// Records all renderer passes into the caller-owned command encoder.
void Renderer::render(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene) {
    require_renderer("Renderer::render").render_impl(encoder, target, scene);
}

// Advances renderer teardown work and reports whether resources are released.
bool Renderer::release() {
    if (s_renderer == nullptr) {
        return true;
    }
    return s_renderer->release_impl();
}

// Destroys the renderer singleton after release has completed.
void Renderer::destroy() noexcept {
    s_renderer.reset();
}

// Returns the current renderer lifecycle state.
RendererLifecycleState Renderer::state() noexcept {
    if (s_renderer != nullptr) {
        return s_renderer->m_state;
    }
    return RendererLifecycleState::Uninitialized;
}

// Reports durable resource creation counters.
RendererCounters Renderer::counters() noexcept {
    if (s_renderer == nullptr) {
        return RendererCounters{};
    }
    RendererCounters total;
    if (s_renderer->m_scene_color_target != nullptr) {
        add_renderer_counters(total, s_renderer->m_scene_color_target->counters());
    }
    if (s_renderer->m_depth_target != nullptr) {
        add_renderer_counters(total, s_renderer->m_depth_target->counters());
    }
    if (s_renderer->m_shadow_map_target != nullptr) {
        add_renderer_counters(total, s_renderer->m_shadow_map_target->counters());
    }
    if (s_renderer->m_shadow_caster_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_shadow_caster_pass->counters());
    }
    if (s_renderer->m_shadow_debug_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_shadow_debug_pass->counters());
    }
    if (s_renderer->m_opaque_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_opaque_pass->counters());
    }
    if (s_renderer->m_sky_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_sky_pass->counters());
    }
    if (s_renderer->m_bloom_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_bloom_pass->counters());
    }
    if (s_renderer->m_tone_map_pass != nullptr) {
        add_renderer_counters(total, s_renderer->m_tone_map_pass->counters());
    }
    add_renderer_counters(total, TempBuffer::counters());
    return total;
}

// Reports the most recent render-object culling stats.
RendererCullingStats Renderer::culling_stats() noexcept {
    if (s_renderer == nullptr) {
        return RendererCullingStats{};
    }
    return s_renderer->m_culling_stats;
}

// Reports the most recent shadow pass diagnostics.
ShadowPassDiagnostics Renderer::shadow_diagnostics() noexcept {
    if (s_renderer == nullptr) {
        return ShadowPassDiagnostics{};
    }
    return s_renderer->m_shadow_diagnostics;
}

// Reports the most recent bloom pass diagnostics.
BloomPassDiagnostics Renderer::bloom_diagnostics() noexcept {
    if (s_renderer == nullptr || s_renderer->m_bloom_pass == nullptr) {
        return BloomPassDiagnostics{};
    }
    return s_renderer->m_bloom_pass->diagnostics();
}

// Reports current temp-buffer memory and reuse diagnostics.
TempBufferStats Renderer::temp_buffer_stats() noexcept {
    if (s_renderer == nullptr) {
        return TempBufferStats{};
    }
    return TempBuffer::stats();
}

// Enables or disables the on-screen shadow-map cascade preview overlay.
void Renderer::set_shadow_debug_overlay_enabled(bool enabled) {
    require_renderer("Renderer::set_shadow_debug_overlay_enabled").m_shadow_debug_overlay_enabled = enabled;
}

// Reports whether the on-screen shadow-map cascade preview overlay is active.
bool Renderer::shadow_debug_overlay_enabled() noexcept {
    return s_renderer != nullptr && s_renderer->m_shadow_debug_overlay_enabled;
}

// Enables or disables the debug sun lock with light travelling straight down.
void Renderer::set_overhead_sun_debug_enabled(bool enabled) {
    require_renderer("Renderer::set_overhead_sun_debug_enabled").m_overhead_sun_debug_enabled = enabled;
}

// Reports whether the debug overhead-sun lock is active.
bool Renderer::overhead_sun_debug_enabled() noexcept {
    return s_renderer != nullptr && s_renderer->m_overhead_sun_debug_enabled;
}

// Advances the internal pass-list preparation state machine.
bool Renderer::prepare_impl() {
    switch (m_state) {
    case RendererLifecycleState::Ready:
        return true;
    case RendererLifecycleState::Created:
        set_state(RendererLifecycleState::Preparing);
        [[fallthrough]];
    case RendererLifecycleState::Preparing:
        try {
            if (m_scene_color_target == nullptr) {
                m_scene_color_target = std::make_unique<SceneColorTarget>(m_gpu);
            }
            if (m_depth_target == nullptr) {
                m_depth_target = std::make_unique<DepthTarget>(m_gpu);
            }
            if (m_shadow_map_target == nullptr) {
                m_shadow_map_target = std::make_unique<ShadowMapTarget>(m_gpu);
            }
            if (m_shadow_caster_pass == nullptr) {
                m_shadow_caster_pass = ShadowCasterPass::create(m_gpu, ShadowMapTarget::format());
            }
            if (m_shadow_debug_pass == nullptr) {
                m_shadow_debug_pass = ShadowDebugPass::create(m_gpu, m_color_format);
            }
            if (m_opaque_pass == nullptr) {
                m_opaque_pass = OpaquePass::create(m_gpu, SceneColorTarget::format());
            }
            if (m_sky_pass == nullptr) {
                m_sky_pass = SkyPass::create(m_gpu, SceneColorTarget::format(), DepthTarget::format());
            }
            if (m_bloom_pass == nullptr) {
                m_bloom_pass = BloomPass::create(m_gpu, SceneColorTarget::format());
            }
            if (m_tone_map_pass == nullptr) {
                m_tone_map_pass =
                    ToneMapPass::create(m_gpu, m_color_format, tone_map_output_encoding_for(m_color_format));
            }
            set_state(RendererLifecycleState::Ready);
            return true;
        } catch (...) {
            set_state(RendererLifecycleState::Failed);
            throw;
        }
    case RendererLifecycleState::Failed:
        throw EngineError("Renderer::prepare cannot continue while Renderer is failed.");
    case RendererLifecycleState::Releasing:
    case RendererLifecycleState::Released:
        throw EngineError("Renderer::prepare cannot run after Renderer release has started.");
    case RendererLifecycleState::Uninitialized:
        throw EngineError("Renderer::prepare requires Renderer::create first.");
    }
    throw EngineError("Renderer::prepare cannot run in an unknown lifecycle state.");
}

// Resizes pass-level render targets.
void Renderer::resize_impl(std::uint32_t width, std::uint32_t height) {
    if (m_state != RendererLifecycleState::Ready) {
        throw EngineError("Renderer::resize requires Renderer::prepare to complete first.");
    }
    if (m_scene_color_target == nullptr || m_depth_target == nullptr) {
        throw EngineError("Renderer targets are not prepared.");
    }
    m_scene_color_target->resize(width, height);
    m_depth_target->resize(width, height);
}

// Records all prepared passes into the caller-owned command encoder.
void Renderer::render_impl(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene) {
    if (m_state != RendererLifecycleState::Ready) {
        throw EngineError("Renderer::render requires Renderer::prepare to complete first.");
    }
    if (m_scene_color_target == nullptr || m_depth_target == nullptr || m_shadow_map_target == nullptr ||
        m_shadow_caster_pass == nullptr || m_shadow_debug_pass == nullptr || m_opaque_pass == nullptr ||
        m_sky_pass == nullptr || m_bloom_pass == nullptr || m_tone_map_pass == nullptr) {
        throw EngineError("Renderer has no prepared passes.");
    }
    if (encoder == nullptr || target.m_view == nullptr) {
        throw EngineError("Renderer render requires an encoder and texture view.");
    }
    if (target.m_format != m_color_format) {
        throw EngineError("Renderer render target format " + gpu::texture_format_name(target.m_format) +
                          " does not match renderer format " + gpu::texture_format_name(m_color_format) + ".");
    }
    if (target.m_width == 0 || target.m_height == 0) {
        throw EngineError("Renderer render target dimensions must be nonzero.");
    }
    const Camera* camera = scene.main_camera();
    if (camera == nullptr) {
        throw EngineError("Renderer render requires a scene camera.");
    }
    const float aspect = static_cast<float>(target.m_width) / static_cast<float>(target.m_height);
    const CameraProperties camera_properties = camera->camera_properties(aspect);

    RenderObjectExtractionStats extraction_stats;
    extract_render_objects(scene, m_render_objects, extraction_stats);
    const ViewFrustum camera_frustum = view_frustum_from_camera(camera_properties);
    CullingStats camera_culling_stats;
    m_draw_list.clear();
    append_culled_draws(m_render_objects, camera_frustum.plane_set(), m_draw_list, camera_culling_stats);
    m_draw_list.validate();
    m_culling_stats = RendererCullingStats{extraction_stats.m_extracted_object_count,
        camera_culling_stats.m_accepted_object_count,
        camera_culling_stats.m_rejected_object_count};

    std::array<LightProperties, 1> light_storage{};
    const std::size_t light_count = build_light_properties(scene, std::span<LightProperties>(light_storage));
    std::span<LightProperties> mutable_lights(light_storage.data(), light_count);
    if (m_overhead_sun_debug_enabled) {
        apply_overhead_sun_debug(mutable_lights);
    }
    const std::span<const LightProperties> lights(light_storage.data(), light_count);
    ShadowFrameState shadow_frame_state = make_disabled_shadow_frame_state(m_shadow_settings);
    ShadowCascadeSet shadow_cascades;
    bool has_shadow_cascades = false;
    const LightProperties* shadow_light = first_directional_light(lights);
    if (shadow_light != nullptr) {
        m_shadow_map_target->resize(m_shadow_settings.m_map_size);
        const ShadowCascadeSet cascades =
            build_shadow_cascades(camera_properties, shadow_light->m_direction, m_shadow_settings);
        shadow_cascades = cascades;
        has_shadow_cascades = true;
        m_shadow_caster_pass->render(encoder, *m_shadow_map_target, cascades, m_shadow_settings, m_render_objects);
        m_shadow_diagnostics = m_shadow_caster_pass->diagnostics();
        shadow_frame_state = make_shadow_frame_state(cascades,
            m_shadow_settings,
            m_shadow_map_target->sampling_view(),
            m_shadow_map_target->sampler(),
            m_shadow_map_target->size(),
            m_shadow_map_target->view_generation());
    } else {
        m_shadow_diagnostics = ShadowPassDiagnostics{};
        m_shadow_diagnostics.m_cascade_count = ShadowMapTarget::cascade_count();
        m_shadow_diagnostics.m_map_size = m_shadow_map_target->size();
        m_shadow_diagnostics.m_estimated_depth_bytes = m_shadow_map_target->estimated_depth_bytes();
        m_shadow_diagnostics.m_pcf_mode = m_shadow_settings.m_pcf_mode;
        m_shadow_diagnostics.m_pcf_sample_count = shadow_pcf_sample_count(m_shadow_settings.m_pcf_mode);
    }
    m_scene_color_target->resize(target.m_width, target.m_height);
    m_depth_target->resize(target.m_width, target.m_height);

    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = m_scene_color_target->view();
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = webgpu_clear_color();

    WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth_attachment.view = m_depth_target->view();
    depth_attachment.depthLoadOp = WGPULoadOp_Clear;
    depth_attachment.depthStoreOp = WGPUStoreOp_Store;
    depth_attachment.depthClearValue = 1.0F;

    WGPURenderPassDescriptor scene_pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    scene_pass_descriptor.label = gpu::cstring_view("OFG scene color pass");
    scene_pass_descriptor.colorAttachmentCount = 1;
    scene_pass_descriptor.colorAttachments = &color_attachment;
    scene_pass_descriptor.depthStencilAttachment = &depth_attachment;

    WGPURenderPassEncoder scene_pass = wgpuCommandEncoderBeginRenderPass(encoder, &scene_pass_descriptor);
    if (scene_pass == nullptr) {
        throw EngineError("wgpuCommandEncoderBeginRenderPass returned null for scene color pass.");
    }
    try {
        m_opaque_pass->draw(scene_pass,
            camera_properties,
            lights,
            scene.environment().ambient_light(),
            shadow_frame_state,
            m_draw_list);
        m_sky_pass->draw(scene_pass, camera_properties, scene.environment(), lights);
    } catch (...) {
        wgpuRenderPassEncoderEnd(scene_pass);
        wgpuRenderPassEncoderRelease(scene_pass);
        throw;
    }
    wgpuRenderPassEncoderEnd(scene_pass);
    wgpuRenderPassEncoderRelease(scene_pass);

    bool temp_frame_active = false;
    BloomResult bloom_result;
    try {
        TempBuffer::begin_frame();
        temp_frame_active = true;
        bloom_result = m_bloom_pass->render(encoder,
            m_scene_color_target->view(),
            m_scene_color_target->width(),
            m_scene_color_target->height(),
            m_bloom_settings);
        m_tone_map_pass->render(encoder, m_scene_color_target->view(), bloom_result.tone_map_input(), target);
        if (m_shadow_debug_overlay_enabled && has_shadow_cascades && m_shadow_map_target->sampling_view() != nullptr &&
            m_shadow_map_target->size() > 0U) {
            m_shadow_debug_pass->render(encoder, m_shadow_map_target->sampling_view(), shadow_cascades, target);
        }
        TempBuffer::release(bloom_result.m_buffer);
        TempBuffer::end_frame();
        temp_frame_active = false;
    } catch (...) {
        TempBuffer::release(bloom_result.m_buffer);
        if (temp_frame_active) {
            TempBuffer::end_frame();
        }
        throw;
    }
}

// Advances the pass-resource release state machine.
bool Renderer::release_impl() {
    switch (m_state) {
    case RendererLifecycleState::Released:
        return true;
    case RendererLifecycleState::Created:
    case RendererLifecycleState::Preparing:
    case RendererLifecycleState::Ready:
    case RendererLifecycleState::Failed:
        set_state(RendererLifecycleState::Releasing);
        [[fallthrough]];
    case RendererLifecycleState::Releasing:
        m_draw_list.clear();
        m_render_objects.clear();
        m_culling_stats = RendererCullingStats{};
        m_shadow_diagnostics = ShadowPassDiagnostics{};
        m_tone_map_pass.reset();
        m_bloom_pass.reset();
        (void)TempBuffer::release();
        TempBuffer::destroy();
        m_sky_pass.reset();
        m_opaque_pass.reset();
        m_shadow_debug_pass.reset();
        m_shadow_caster_pass.reset();
        m_shadow_map_target.reset();
        m_depth_target.reset();
        m_scene_color_target.reset();
        m_gpu = GpuContext{};
        m_color_format = WGPUTextureFormat_Undefined;
        set_state(RendererLifecycleState::Released);
        return true;
    case RendererLifecycleState::Uninitialized:
        return true;
    }
    throw EngineError("Renderer::release cannot run in an unknown lifecycle state.");
}

// Returns the live singleton or throws a clear lifecycle error.
Renderer& Renderer::require_renderer(const char* operation) {
    if (s_renderer == nullptr) {
        throw EngineError(std::string(operation) + " requires Renderer::create first.");
    }
    return *s_renderer;
}

// Updates this instance lifecycle state.
void Renderer::set_state(RendererLifecycleState state) noexcept {
    m_state = state;
}

} // namespace ofg
