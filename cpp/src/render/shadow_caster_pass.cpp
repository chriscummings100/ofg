// Depth-only shadow caster pass for cascaded sun shadows.
#include "ofg/render/shadow_caster_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/resources/mesh.hpp"

#include "shaders/shadow_caster.wgsl.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ofg {
namespace {

constexpr std::uint64_t _frame_uniform_bytes = sizeof(float) * 16U;
constexpr std::uint64_t _draw_uniform_bytes = sizeof(float) * 16U;
constexpr std::uint64_t _draw_uniform_stride = 256U;
constexpr std::uint32_t _initial_draw_capacity = 1U;

// Creates a uniform buffer with CopyDst writes enabled.
WGPUBuffer create_uniform_buffer(WGPUDevice device, const char* label, std::uint64_t byte_size) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view(label);
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = byte_size;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreateBuffer returned null for ") + label + ".");
    }
    return buffer;
}

// Creates a single uniform-buffer bind group layout.
WGPUBindGroupLayout create_uniform_layout(
    WGPUDevice device, const char* label, std::uint64_t byte_size, WGPUShaderStage visibility, bool dynamic_offset) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = visibility;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = dynamic_offset ? WGPU_TRUE : WGPU_FALSE;
    entry.buffer.minBindingSize = byte_size;

    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view(label);
    descriptor.entryCount = 1;
    descriptor.entries = &entry;

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreateBindGroupLayout returned null for ") + label + ".");
    }
    return layout;
}

// Creates a bind group for one matrix uniform buffer binding.
WGPUBindGroup create_uniform_bind_group(
    WGPUDevice device, const char* label, WGPUBindGroupLayout layout, WGPUBuffer buffer, std::uint64_t byte_size) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = byte_size;

    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view(label);
    descriptor.layout = layout;
    descriptor.entryCount = 1;
    descriptor.entries = &entry;

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &descriptor);
    if (bind_group == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreateBindGroup returned null for ") + label + ".");
    }
    return bind_group;
}

// Writes a packed Mat4 into a uniform buffer.
void write_mat4(WGPUQueue queue, WGPUBuffer buffer, std::uint64_t offset, const math::Mat4& matrix) {
    const std::array<float, 16> packed = math::pack_mat4(matrix);
    wgpuQueueWriteBuffer(queue, buffer, offset, packed.data(), sizeof(float) * packed.size());
}

// Creates the depth-only shadow caster shader module.
WGPUShaderModule create_shadow_caster_shader_module(WGPUDevice device) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::cstring_view(render::shaders::shadow_caster_wgsl);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::cstring_view("OFG shadow caster shader");

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
    if (module == nullptr) {
        throw EngineError("wgpuDeviceCreateShaderModule returned null for shadow caster pass.");
    }
    return module;
}

// Creates the frame/draw-only pipeline layout for the shadow caster pass.
WGPUPipelineLayout create_shadow_pipeline_layout(
    WGPUDevice device, WGPUBindGroupLayout frame_layout, WGPUBindGroupLayout draw_layout) {
    std::array<WGPUBindGroupLayout, 2> layouts{frame_layout, draw_layout};

    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow caster pipeline layout");
    descriptor.bindGroupLayoutCount = layouts.size();
    descriptor.bindGroupLayouts = layouts.data();

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreatePipelineLayout returned null for shadow caster pass.");
    }
    return layout;
}

// Creates the depth-only render pipeline used for every cascade.
WGPURenderPipeline create_shadow_pipeline(
    WGPUDevice device, WGPUPipelineLayout layout, WGPUShaderModule shader_module, WGPUTextureFormat depth_format) {
    std::array<WGPUVertexAttribute, 1> attributes{WGPU_VERTEX_ATTRIBUTE_INIT};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(MeshVertex, m_position);
    attributes[0].shaderLocation = 0;

    WGPUVertexBufferLayout vertex_buffer_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_buffer_layout.arrayStride = mesh_vertex_stride_bytes();
    vertex_buffer_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_buffer_layout.attributeCount = attributes.size();
    vertex_buffer_layout.attributes = attributes.data();

    WGPUVertexState vertex_state = WGPU_VERTEX_STATE_INIT;
    vertex_state.module = shader_module;
    vertex_state.entryPoint = gpu::cstring_view("vs_main");
    vertex_state.bufferCount = 1;
    vertex_state.buffers = &vertex_buffer_layout;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth_stencil.format = depth_format;
    depth_stencil.depthWriteEnabled = WGPUOptionalBool_True;
    depth_stencil.depthCompare = WGPUCompareFunction_LessEqual;
    depth_stencil.depthBias = 2;
    depth_stencil.depthBiasSlopeScale = 2.0f;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow caster pipeline");
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.depthStencil = &depth_stencil;
    descriptor.fragment = nullptr;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    if (pipeline == nullptr) {
        throw EngineError("wgpuDeviceCreateRenderPipeline returned null for shadow caster pass.");
    }
    return pipeline;
}

// Validates GPU resource handles needed by a shadow caster.
void validate_mesh_gpu_state(const RenderObject& object) {
    if (object.m_mesh == nullptr) {
        throw EngineError("Shadow caster render object mesh must not be null.");
    }
    if (object.m_mesh->vertex_buffer() == nullptr || object.m_mesh->index_buffer() == nullptr) {
        throw EngineError("Shadow caster mesh buffers are not GPU-ready.");
    }
}

// Builds diagnostics shared by rendered and skipped shadow frames.
ShadowPassDiagnostics base_diagnostics(
    const ShadowMapTarget& target, const ShadowCascadeSet& cascades, const ShadowSettings& settings) {
    ShadowPassDiagnostics diagnostics;
    diagnostics.m_cascade_count = ShadowMapTarget::cascade_count();
    diagnostics.m_map_size = target.size();
    diagnostics.m_estimated_depth_bytes = target.estimated_depth_bytes();
    diagnostics.m_pcf_mode = settings.m_pcf_mode;
    diagnostics.m_pcf_sample_count = shadow_pcf_sample_count(settings.m_pcf_mode);
    diagnostics.m_sun_elevation_radians = cascades.m_sun_elevation_radians;
    diagnostics.m_effective_intensity = cascades.m_effective_intensity;
    diagnostics.m_low_sun_clamped = cascades.m_low_sun_clamped;
    for (std::uint32_t index = 0; index < ShadowMapTarget::cascade_count(); ++index) {
        diagnostics.m_cascades[index].m_index = index;
    }
    return diagnostics;
}

// Accumulates one cascade's diagnostics into the frame totals.
void add_cascade_totals(ShadowPassDiagnostics& diagnostics, const ShadowCascadeDiagnostics& cascade) noexcept {
    diagnostics.m_total_tested_caster_count += cascade.m_tested_caster_count;
    diagnostics.m_total_accepted_caster_count += cascade.m_accepted_caster_count;
    diagnostics.m_total_rejected_caster_count += cascade.m_rejected_caster_count;
    diagnostics.m_total_draw_count += cascade.m_draw_count;
    diagnostics.m_total_submesh_count += cascade.m_submesh_count;
    diagnostics.m_total_index_count += cascade.m_index_count;
}

} // namespace

// Stores already-created pass GPU state.
ShadowCasterPass::ShadowCasterPass(GpuContext gpu,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout frame_layout,
    WGPUBuffer frame_buffer,
    WGPUBindGroup frame_bind_group,
    WGPUBindGroupLayout draw_layout,
    WGPUBuffer draw_buffer,
    WGPUBindGroup draw_bind_group,
    WGPUPipelineLayout pipeline_layout,
    WGPURenderPipeline pipeline,
    std::uint32_t draw_capacity)
    : m_gpu(gpu), m_shader_module(shader_module), m_frame_layout(frame_layout), m_frame_buffer(frame_buffer),
      m_frame_bind_group(frame_bind_group), m_draw_layout(draw_layout), m_draw_buffer(draw_buffer),
      m_draw_bind_group(draw_bind_group), m_pipeline_layout(pipeline_layout), m_pipeline(pipeline),
      m_draw_capacity(draw_capacity) {}

// Releases pass-owned GPU handles.
ShadowCasterPass::~ShadowCasterPass() {
    release_gpu_state();
}

// Creates shader, pipeline, layouts, bind groups, and uniform buffers.
std::unique_ptr<ShadowCasterPass> ShadowCasterPass::create(GpuContext gpu, WGPUTextureFormat depth_format) {
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("ShadowCasterPass requires a WebGPU device and queue.");
    }
    if (depth_format == WGPUTextureFormat_Undefined) {
        throw EngineError("ShadowCasterPass requires a defined depth format.");
    }

    WGPUShaderModule shader_module = nullptr;
    WGPUBindGroupLayout frame_layout = nullptr;
    WGPUBuffer frame_buffer = nullptr;
    WGPUBindGroup frame_bind_group = nullptr;
    WGPUBindGroupLayout draw_layout = nullptr;
    WGPUBuffer draw_buffer = nullptr;
    WGPUBindGroup draw_bind_group = nullptr;
    WGPUPipelineLayout pipeline_layout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    try {
        shader_module = create_shadow_caster_shader_module(gpu.m_device);
        frame_layout = create_uniform_layout(
            gpu.m_device, "OFG shadow frame layout", _frame_uniform_bytes, WGPUShaderStage_Vertex, false);
        frame_buffer = create_uniform_buffer(gpu.m_device, "OFG shadow frame uniforms", _frame_uniform_bytes);
        frame_bind_group = create_uniform_bind_group(
            gpu.m_device, "OFG shadow frame bind group", frame_layout, frame_buffer, _frame_uniform_bytes);
        draw_layout = create_uniform_layout(
            gpu.m_device, "OFG shadow draw layout", _draw_uniform_bytes, WGPUShaderStage_Vertex, true);
        draw_buffer = create_uniform_buffer(gpu.m_device, "OFG shadow draw uniforms", _draw_uniform_stride);
        draw_bind_group = create_uniform_bind_group(
            gpu.m_device, "OFG shadow draw bind group", draw_layout, draw_buffer, _draw_uniform_bytes);
        pipeline_layout = create_shadow_pipeline_layout(gpu.m_device, frame_layout, draw_layout);
        pipeline = create_shadow_pipeline(gpu.m_device, pipeline_layout, shader_module, depth_format);
    } catch (...) {
        if (pipeline != nullptr) {
            wgpuRenderPipelineRelease(pipeline);
        }
        if (pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(pipeline_layout);
        }
        if (draw_bind_group != nullptr) {
            wgpuBindGroupRelease(draw_bind_group);
        }
        if (draw_buffer != nullptr) {
            wgpuBufferRelease(draw_buffer);
        }
        if (draw_layout != nullptr) {
            wgpuBindGroupLayoutRelease(draw_layout);
        }
        if (frame_bind_group != nullptr) {
            wgpuBindGroupRelease(frame_bind_group);
        }
        if (frame_buffer != nullptr) {
            wgpuBufferRelease(frame_buffer);
        }
        if (frame_layout != nullptr) {
            wgpuBindGroupLayoutRelease(frame_layout);
        }
        if (shader_module != nullptr) {
            wgpuShaderModuleRelease(shader_module);
        }
        throw;
    }

    std::unique_ptr<ShadowCasterPass> pass(new ShadowCasterPass(gpu,
        shader_module,
        frame_layout,
        frame_buffer,
        frame_bind_group,
        draw_layout,
        draw_buffer,
        draw_bind_group,
        pipeline_layout,
        pipeline,
        _initial_draw_capacity));
    pass->m_buffer_create_count = 2U;
    pass->m_bind_group_create_count = 2U;
    return pass;
}

// Encodes one depth-only render pass per active cascade.
void ShadowCasterPass::render(WGPUCommandEncoder encoder,
    ShadowMapTarget& target,
    const ShadowCascadeSet& cascades,
    const ShadowSettings& settings,
    std::span<const RenderObject> render_objects) {
    if (encoder == nullptr) {
        throw EngineError("ShadowCasterPass render requires a command encoder.");
    }
    if (target.size() == 0U || target.sampling_view() == nullptr) {
        throw EngineError("ShadowCasterPass render requires a live shadow map target.");
    }
    validate_shadow_settings(settings);

    m_diagnostics = base_diagnostics(target, cascades, settings);
    if (!settings.m_enabled || cascades.m_effective_intensity <= 0.0f) {
        return;
    }
    m_diagnostics.m_enabled = true;

    for (std::uint32_t cascade_index = 0; cascade_index < ShadowMapTarget::cascade_count(); ++cascade_index) {
        const ShadowCascade& cascade = cascades.m_cascades[cascade_index];
        ShadowCascadeDiagnostics cascade_diagnostics;
        cascade_diagnostics.m_index = cascade_index;
        m_culled_casters.clear();
        for (const RenderObject& object : render_objects) {
            cascade_diagnostics.m_tested_caster_count += 1U;
            if (!intersects_culling_planes(object.m_world_bounds, cascade.plane_set())) {
                cascade_diagnostics.m_rejected_caster_count += 1U;
                continue;
            }
            validate_mesh_gpu_state(object);
            m_culled_casters.push_back(&object);
            cascade_diagnostics.m_accepted_caster_count += 1U;
        }

        ensure_draw_capacity(static_cast<std::uint32_t>(m_culled_casters.size()));
        write_mat4(m_gpu.m_queue, m_frame_buffer, 0, cascade.m_clip_from_world);
        for (std::uint32_t draw_index = 0; draw_index < m_culled_casters.size(); ++draw_index) {
            write_mat4(m_gpu.m_queue,
                m_draw_buffer,
                static_cast<std::uint64_t>(draw_index) * _draw_uniform_stride,
                m_culled_casters[draw_index]->m_model);
        }

        WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depth_attachment.view = target.render_view(cascade_index);
        depth_attachment.depthLoadOp = WGPULoadOp_Clear;
        depth_attachment.depthStoreOp = WGPUStoreOp_Store;
        depth_attachment.depthClearValue = 1.0f;

        WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        pass_descriptor.label = gpu::cstring_view("OFG shadow caster pass");
        pass_descriptor.colorAttachmentCount = 0;
        pass_descriptor.colorAttachments = nullptr;
        pass_descriptor.depthStencilAttachment = &depth_attachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
        if (pass == nullptr) {
            throw EngineError("wgpuCommandEncoderBeginRenderPass returned null for shadow caster pass.");
        }
        wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
        for (std::uint32_t draw_index = 0; draw_index < m_culled_casters.size(); ++draw_index) {
            const RenderObject& caster = *m_culled_casters[draw_index];
            const std::uint32_t dynamic_offset = draw_index * static_cast<std::uint32_t>(_draw_uniform_stride);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, m_draw_bind_group, 1, &dynamic_offset);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, caster.m_mesh->vertex_buffer(), 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, caster.m_mesh->index_buffer(), WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

            const std::span<const SubMesh> submeshes = caster.m_mesh->submeshes();
            cascade_diagnostics.m_draw_count += 1U;
            for (const SubMesh& submesh : submeshes) {
                cascade_diagnostics.m_submesh_count += 1U;
                cascade_diagnostics.m_index_count += submesh.m_index_count;
                wgpuRenderPassEncoderDrawIndexed(pass, submesh.m_index_count, 1, submesh.m_index_start, 0, 0);
            }
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        m_diagnostics.m_encoded_pass_count += 1U;
        m_diagnostics.m_cascades[cascade_index] = cascade_diagnostics;
        add_cascade_totals(m_diagnostics, cascade_diagnostics);
    }
}

// Reports the most recent shadow pass diagnostics.
ShadowPassDiagnostics ShadowCasterPass::diagnostics() const noexcept {
    return m_diagnostics;
}

// Reports durable renderer resource counters.
RendererCounters ShadowCasterPass::counters() const noexcept {
    RendererCounters counters;
    counters.m_pipeline_create_count = m_pipeline == nullptr ? 0U : 1U;
    counters.m_buffer_create_count = m_buffer_create_count;
    counters.m_bind_group_layout_create_count = 2U;
    counters.m_bind_group_create_count = m_bind_group_create_count;
    counters.m_shader_module_create_count = m_shader_module == nullptr ? 0U : 1U;
    return counters;
}

// Recreates the dynamic draw uniform buffer for a larger caster count.
void ShadowCasterPass::ensure_draw_capacity(std::uint32_t draw_count) {
    if (draw_count <= m_draw_capacity) {
        return;
    }

    std::uint32_t next_capacity = std::max(m_draw_capacity, _initial_draw_capacity);
    while (next_capacity < draw_count) {
        next_capacity *= 2U;
    }

    WGPUBuffer next_buffer = create_uniform_buffer(
        m_gpu.m_device, "OFG shadow draw uniforms", static_cast<std::uint64_t>(next_capacity) * _draw_uniform_stride);
    m_buffer_create_count += 1U;
    WGPUBindGroup next_bind_group = nullptr;
    try {
        next_bind_group = create_uniform_bind_group(
            m_gpu.m_device, "OFG shadow draw bind group", m_draw_layout, next_buffer, _draw_uniform_bytes);
    } catch (...) {
        wgpuBufferRelease(next_buffer);
        throw;
    }

    if (m_draw_bind_group != nullptr) {
        wgpuBindGroupRelease(m_draw_bind_group);
    }
    if (m_draw_buffer != nullptr) {
        wgpuBufferRelease(m_draw_buffer);
    }
    m_draw_buffer = next_buffer;
    m_draw_bind_group = next_bind_group;
    m_draw_capacity = next_capacity;
    m_bind_group_create_count += 1U;
}

// Releases pass-owned GPU handles.
void ShadowCasterPass::release_gpu_state() noexcept {
    if (m_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_pipeline_layout);
        m_pipeline_layout = nullptr;
    }
    if (m_draw_bind_group != nullptr) {
        wgpuBindGroupRelease(m_draw_bind_group);
        m_draw_bind_group = nullptr;
    }
    if (m_draw_buffer != nullptr) {
        wgpuBufferRelease(m_draw_buffer);
        m_draw_buffer = nullptr;
    }
    if (m_draw_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_draw_layout);
        m_draw_layout = nullptr;
    }
    if (m_frame_bind_group != nullptr) {
        wgpuBindGroupRelease(m_frame_bind_group);
        m_frame_bind_group = nullptr;
    }
    if (m_frame_buffer != nullptr) {
        wgpuBufferRelease(m_frame_buffer);
        m_frame_buffer = nullptr;
    }
    if (m_frame_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_frame_layout);
        m_frame_layout = nullptr;
    }
    if (m_shader_module != nullptr) {
        wgpuShaderModuleRelease(m_shader_module);
        m_shader_module = nullptr;
    }
    m_draw_capacity = 0U;
}

} // namespace ofg
