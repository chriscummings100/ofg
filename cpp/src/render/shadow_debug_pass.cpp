// Final-target shadow-map visualization pass for renderer debugging.
#include "ofg/render/shadow_debug_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include "shaders/shadow_debug.wgsl.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace ofg {
namespace {

constexpr std::uint64_t _shadow_debug_uniform_bytes = sizeof(float) * 4U;

// Creates a shader module from the built-in shadow debug WGSL source.
WGPUShaderModule create_shadow_debug_shader_module(WGPUDevice device) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::cstring_view(render::shaders::shadow_debug_wgsl);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::cstring_view("OFG shadow debug shader");

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
    if (module == nullptr) {
        throw EngineError("wgpuDeviceCreateShaderModule returned null for shadow debug pass.");
    }
    return module;
}

// Creates the uniform plus depth-array bind group layout.
WGPUBindGroupLayout create_shadow_debug_bind_group_layout(WGPUDevice device) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = WGPU_FALSE;
    entries[0].buffer.minBindingSize = _shadow_debug_uniform_bytes;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[1].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    entries[1].texture.multisampled = WGPU_FALSE;

    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow debug bind group layout");
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroupLayout returned null for shadow debug pass.");
    }
    return layout;
}

// Creates the pipeline layout for the shadow debug pass.
WGPUPipelineLayout create_shadow_debug_pipeline_layout(WGPUDevice device, WGPUBindGroupLayout bind_group_layout) {
    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow debug pipeline layout");
    descriptor.bindGroupLayoutCount = 1;
    descriptor.bindGroupLayouts = &bind_group_layout;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreatePipelineLayout returned null for shadow debug pass.");
    }
    return layout;
}

// Creates the fullscreen overlay pipeline for the final target format.
WGPURenderPipeline create_shadow_debug_pipeline(
    WGPUDevice device, WGPUPipelineLayout layout, WGPUShaderModule module, WGPUTextureFormat output_format) {
    WGPUVertexState vertex_state = WGPU_VERTEX_STATE_INIT;
    vertex_state.module = module;
    vertex_state.entryPoint = gpu::cstring_view("vs_main");

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = output_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
    fragment_state.module = module;
    fragment_state.entryPoint = gpu::cstring_view("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow debug pipeline");
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    if (pipeline == nullptr) {
        throw EngineError("wgpuDeviceCreateRenderPipeline returned null for shadow debug pass.");
    }
    return pipeline;
}

// Creates the persistent output-size uniform buffer.
WGPUBuffer create_shadow_debug_uniform_buffer(WGPUDevice device) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow debug uniforms");
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = _shadow_debug_uniform_bytes;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError("wgpuDeviceCreateBuffer returned null for shadow debug pass.");
    }
    return buffer;
}

// Creates the bind group for a current shadow-map array view.
WGPUBindGroup create_shadow_debug_bind_group(
    WGPUDevice device, WGPUBindGroupLayout layout, WGPUBuffer uniform_buffer, WGPUTextureView shadow_map_view) {
    std::array<WGPUBindGroupEntry, 2> entries{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = uniform_buffer;
    entries[0].offset = 0;
    entries[0].size = _shadow_debug_uniform_bytes;
    entries[1].binding = 1;
    entries[1].textureView = shadow_map_view;

    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow debug bind group");
    descriptor.layout = layout;
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &descriptor);
    if (bind_group == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroup returned null for shadow debug pass.");
    }
    return bind_group;
}

} // namespace

// Stores already-created pass GPU state.
ShadowDebugPass::ShadowDebugPass(GpuContext gpu,
    WGPUTextureFormat output_format,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_group_layout,
    WGPUPipelineLayout pipeline_layout,
    WGPURenderPipeline pipeline,
    WGPUBuffer uniform_buffer)
    : m_gpu(std::move(gpu)), m_output_format(output_format), m_shader_module(shader_module),
      m_bind_group_layout(bind_group_layout), m_pipeline_layout(pipeline_layout), m_pipeline(pipeline),
      m_uniform_buffer(uniform_buffer) {}

// Releases owned WebGPU resources.
ShadowDebugPass::~ShadowDebugPass() {
    release_gpu_state();
}

// Creates shader, layout, pipeline, and uniforms for depth-layer overlays.
std::unique_ptr<ShadowDebugPass> ShadowDebugPass::create(GpuContext gpu, WGPUTextureFormat output_format) {
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("ShadowDebugPass requires a WebGPU device and queue.");
    }
    if (output_format == WGPUTextureFormat_Undefined) {
        throw EngineError("ShadowDebugPass requires a defined output format.");
    }

    WGPUShaderModule shader_module = nullptr;
    WGPUBindGroupLayout bind_group_layout = nullptr;
    WGPUPipelineLayout pipeline_layout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer uniform_buffer = nullptr;
    try {
        shader_module = create_shadow_debug_shader_module(gpu.m_device);
        bind_group_layout = create_shadow_debug_bind_group_layout(gpu.m_device);
        pipeline_layout = create_shadow_debug_pipeline_layout(gpu.m_device, bind_group_layout);
        pipeline = create_shadow_debug_pipeline(gpu.m_device, pipeline_layout, shader_module, output_format);
        uniform_buffer = create_shadow_debug_uniform_buffer(gpu.m_device);
    } catch (...) {
        if (uniform_buffer != nullptr) {
            wgpuBufferRelease(uniform_buffer);
        }
        if (pipeline != nullptr) {
            wgpuRenderPipelineRelease(pipeline);
        }
        if (pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(pipeline_layout);
        }
        if (bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(bind_group_layout);
        }
        if (shader_module != nullptr) {
            wgpuShaderModuleRelease(shader_module);
        }
        throw;
    }

    std::unique_ptr<ShadowDebugPass> pass(new ShadowDebugPass(
        std::move(gpu), output_format, shader_module, bind_group_layout, pipeline_layout, pipeline, uniform_buffer));
    pass->m_counters.m_shader_module_create_count = 1;
    pass->m_counters.m_bind_group_layout_create_count = 1;
    pass->m_counters.m_pipeline_create_count = 1;
    pass->m_counters.m_buffer_create_count = 1;
    return pass;
}

// Encodes an overlay of the three shadow-map cascade layers.
void ShadowDebugPass::render(WGPUCommandEncoder encoder,
    WGPUTextureView shadow_map_view,
    const ShadowCascadeSet& cascades,
    RenderTarget output_target) {
    if (encoder == nullptr || shadow_map_view == nullptr || output_target.m_view == nullptr) {
        throw EngineError("ShadowDebugPass render requires an encoder, shadow map view, and output texture view.");
    }
    if (output_target.m_width == 0 || output_target.m_height == 0) {
        throw EngineError("ShadowDebugPass output target dimensions must be nonzero.");
    }
    if (output_target.m_format != m_output_format) {
        throw EngineError("ShadowDebugPass output target format " + gpu::texture_format_name(output_target.m_format) +
                          " does not match pass format " + gpu::texture_format_name(m_output_format) + ".");
    }

    ensure_bind_group(shadow_map_view);
    write_uniforms(cascades, output_target);

    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = output_target.m_view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.label = gpu::cstring_view("OFG shadow debug overlay pass");
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    if (pass == nullptr) {
        throw EngineError("wgpuCommandEncoderBeginRenderPass returned null for shadow debug pass.");
    }

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// Reports durable resource creation counters.
RendererCounters ShadowDebugPass::counters() const noexcept {
    return m_counters;
}

// Recreates the bind group when the sampled shadow-map view changes.
void ShadowDebugPass::ensure_bind_group(WGPUTextureView shadow_map_view) {
    if (m_bind_group != nullptr && m_bound_shadow_map_view == shadow_map_view) {
        return;
    }

    WGPUBindGroup next_bind_group =
        create_shadow_debug_bind_group(m_gpu.m_device, m_bind_group_layout, m_uniform_buffer, shadow_map_view);
    if (m_bind_group != nullptr) {
        wgpuBindGroupRelease(m_bind_group);
    }
    m_bind_group = next_bind_group;
    m_bound_shadow_map_view = shadow_map_view;
    m_counters.m_bind_group_create_count += 1;
}

// Writes output-size data consumed by the overlay shader.
void ShadowDebugPass::write_uniforms(const ShadowCascadeSet& cascades, RenderTarget output_target) const {
    (void)cascades;
    std::array<float, 4> packed{};
    packed[0] = static_cast<float>(output_target.m_width);
    packed[1] = static_cast<float>(output_target.m_height);
    wgpuQueueWriteBuffer(m_gpu.m_queue, m_uniform_buffer, 0, packed.data(), sizeof(float) * packed.size());
}

// Releases all WebGPU handles owned by this pass.
void ShadowDebugPass::release_gpu_state() noexcept {
    if (m_bind_group != nullptr) {
        wgpuBindGroupRelease(m_bind_group);
        m_bind_group = nullptr;
    }
    if (m_uniform_buffer != nullptr) {
        wgpuBufferRelease(m_uniform_buffer);
        m_uniform_buffer = nullptr;
    }
    if (m_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_pipeline_layout);
        m_pipeline_layout = nullptr;
    }
    if (m_bind_group_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_bind_group_layout);
        m_bind_group_layout = nullptr;
    }
    if (m_shader_module != nullptr) {
        wgpuShaderModuleRelease(m_shader_module);
        m_shader_module = nullptr;
    }
    m_bound_shadow_map_view = nullptr;
}

} // namespace ofg
