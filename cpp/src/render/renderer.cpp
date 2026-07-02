// Static high-level OFG renderer facade.
#include "ofg/render/renderer.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/scene.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Transforms a point by a world-from-local matrix.
math::Vec3 transform_point(math::Mat4 matrix, math::Vec3 point) noexcept {
    const math::Vec4 transformed = math::mul(matrix, math::vec4(point.x, point.y, point.z, 1.0f));
    return math::vec3(transformed.x, transformed.y, transformed.z);
}

// Builds the transient draw queue consumed by the current opaque pass.
void build_draw_list_from_scene(const Scene& scene, DrawList& draw_list) {
    draw_list.clear();
    for (std::size_t index = 0; index < scene.mesh_renderer_count(); ++index) {
        const MeshRenderer* mesh_renderer = scene.get_mesh_renderer(index);
        if (mesh_renderer == nullptr || mesh_renderer->entity() == nullptr) {
            throw EngineError("Scene mesh renderer must have an owning entity.");
        }
        if (!mesh_renderer->visible()) {
            continue;
        }

        const math::Mat4 world_from_renderer = world_from_local(*mesh_renderer->entity());
        const std::vector<MaterialOverride>& material_overrides = mesh_renderer->material_overrides();
        DrawCommand command;
        command.m_mesh = mesh_renderer->mesh();
        command.m_model = world_from_renderer;
        command.m_properties = &mesh_renderer->properties();
        command.m_material_overrides =
            std::span<const MaterialOverride>(material_overrides.data(), material_overrides.size());
        command.m_sort_origin = transform_point(world_from_renderer, mesh_renderer->sort_origin_offset());
        draw_list.add(std::move(command));
    }

    draw_list.validate();
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

// Releases pass resources owned by members.
Renderer::~Renderer() = default;

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

    s_renderer = std::unique_ptr<Renderer>(new Renderer(std::move(gpu), color_format));
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
    for (const std::unique_ptr<OpaquePass>& pass : s_renderer->m_passes) {
        const RendererCounters pass_counters = pass->counters();
        total.m_pipeline_create_count += pass_counters.m_pipeline_create_count;
        total.m_buffer_create_count += pass_counters.m_buffer_create_count;
    }
    return total;
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
            if (m_passes.empty()) {
                m_passes.push_back(OpaquePass::create(m_gpu, m_color_format));
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
    for (std::unique_ptr<OpaquePass>& pass : m_passes) {
        pass->resize(width, height);
    }
}

// Records all prepared passes into the caller-owned command encoder.
void Renderer::render_impl(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene) {
    if (m_state != RendererLifecycleState::Ready) {
        throw EngineError("Renderer::render requires Renderer::prepare to complete first.");
    }
    if (m_passes.empty()) {
        throw EngineError("Renderer has no prepared passes.");
    }
    if (encoder == nullptr || target.m_view == nullptr) {
        throw EngineError("Renderer render requires an encoder and texture view.");
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

    build_draw_list_from_scene(scene, m_draw_list);
    for (std::unique_ptr<OpaquePass>& pass : m_passes) {
        pass->render(encoder, target, camera_properties, scene.main_light(), scene.ambient_light(), m_draw_list);
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
        m_passes.clear();
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
