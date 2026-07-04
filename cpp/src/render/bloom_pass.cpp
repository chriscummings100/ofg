// HDR bloom post-effect pass implementation.
#include "ofg/render/bloom_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include "shaders/bloom_prefilter_downsample.wgsl.hpp"
#include "shaders/bloom_upsample.wgsl.hpp"

#include <array>
#include <string>
#include <utility>

namespace ofg {
namespace {

constexpr WGPUTextureUsage _bloom_temp_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
constexpr std::uint64_t _bloom_uniform_bytes = sizeof(BloomUniformBlock);
constexpr std::uint32_t _bloom_bytes_per_pixel = 8;

// Creates a shader module from embedded WGSL source.
WGPUShaderModule create_bloom_shader_module(WGPUDevice device, const char* label, const char* source) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::cstring_view(source);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::cstring_view(label);

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
    if (module == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreateShaderModule returned null for ") + label + ".");
    }
    return module;
}

// Creates the uniform plus single-source texture bind group layout.
WGPUBindGroupLayout create_prefilter_downsample_bind_group_layout(WGPUDevice device) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = WGPU_FALSE;
    entries[0].buffer.minBindingSize = _bloom_uniform_bytes;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = WGPU_FALSE;

    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG bloom prefilter/downsample bind group layout");
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroupLayout returned null for bloom prefilter/downsample.");
    }
    return layout;
}

// Creates the uniform plus lower/higher texture bind group layout.
WGPUBindGroupLayout create_upsample_bind_group_layout(WGPUDevice device) {
    std::array<WGPUBindGroupLayoutEntry, 3> entries{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = WGPU_FALSE;
    entries[0].buffer.minBindingSize = _bloom_uniform_bytes;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = WGPU_FALSE;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[2].texture.multisampled = WGPU_FALSE;

    WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG bloom upsample bind group layout");
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroupLayout returned null for bloom upsample.");
    }
    return layout;
}

// Creates a pipeline layout for one bloom bind group layout.
WGPUPipelineLayout create_bloom_pipeline_layout(
    WGPUDevice device, WGPUBindGroupLayout bind_group_layout, const char* label) {
    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view(label);
    descriptor.bindGroupLayoutCount = 1;
    descriptor.bindGroupLayouts = &bind_group_layout;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreatePipelineLayout returned null for ") + label + ".");
    }
    return layout;
}

// Creates a full-screen triangle render pipeline for one bloom fragment entry point.
WGPURenderPipeline create_bloom_pipeline(WGPUDevice device,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    WGPUTextureFormat bloom_format,
    const char* fragment_entry_point,
    const char* label) {
    WGPUVertexState vertex_state = WGPU_VERTEX_STATE_INIT;
    vertex_state.module = module;
    vertex_state.entryPoint = gpu::cstring_view("vs_main");

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = bloom_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
    fragment_state.module = module;
    fragment_state.entryPoint = gpu::cstring_view(fragment_entry_point);
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view(label);
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    if (pipeline == nullptr) {
        throw EngineError(std::string("wgpuDeviceCreateRenderPipeline returned null for ") + label + ".");
    }
    return pipeline;
}

// Creates the persistent bloom settings uniform buffer.
WGPUBuffer create_bloom_uniform_buffer(WGPUDevice device) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG bloom uniforms");
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = _bloom_uniform_bytes;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError("wgpuDeviceCreateBuffer returned null for bloom pass.");
    }
    return buffer;
}

// Creates a bind group for prefilter or downsample with one source texture.
WGPUBindGroup create_prefilter_downsample_bind_group(
    WGPUDevice device, WGPUBindGroupLayout layout, WGPUBuffer uniform_buffer, WGPUTextureView source_view) {
    std::array<WGPUBindGroupEntry, 2> entries{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = uniform_buffer;
    entries[0].offset = 0;
    entries[0].size = _bloom_uniform_bytes;
    entries[1].binding = 1;
    entries[1].textureView = source_view;

    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG bloom prefilter/downsample bind group");
    descriptor.layout = layout;
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &descriptor);
    if (bind_group == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroup returned null for bloom prefilter/downsample.");
    }
    return bind_group;
}

// Creates a bind group for upsample accumulation from lower and higher levels.
WGPUBindGroup create_upsample_bind_group(WGPUDevice device,
    WGPUBindGroupLayout layout,
    WGPUBuffer uniform_buffer,
    WGPUTextureView lower_view,
    WGPUTextureView higher_view) {
    std::array<WGPUBindGroupEntry, 3> entries{
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = uniform_buffer;
    entries[0].offset = 0;
    entries[0].size = _bloom_uniform_bytes;
    entries[1].binding = 1;
    entries[1].textureView = lower_view;
    entries[2].binding = 2;
    entries[2].textureView = higher_view;

    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG bloom upsample bind group");
    descriptor.layout = layout;
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &descriptor);
    if (bind_group == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroup returned null for bloom upsample.");
    }
    return bind_group;
}

// Returns the descriptor for one bloom temporary target.
TempBufferDesc bloom_temp_desc(std::uint32_t width, std::uint32_t height, WGPUTextureFormat format) noexcept {
    TempBufferDesc desc;
    desc.m_width = width;
    desc.m_height = height;
    desc.m_format = format;
    desc.m_usage = _bloom_temp_usage;
    return desc;
}

// Estimates one RGBA16Float target byte size for diagnostics.
std::uint64_t bloom_target_bytes(std::uint32_t width, std::uint32_t height) noexcept {
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * _bloom_bytes_per_pixel;
}

// Encodes one full-screen bloom render pass.
void encode_bloom_draw(WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group,
    const TempBufferRef& target,
    const char* label) {
    const RenderTarget render_target = target.render_target();
    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = render_target.m_view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    WGPUColor clear_color = WGPU_COLOR_INIT;
    clear_color.r = 0.0;
    clear_color.g = 0.0;
    clear_color.b = 0.0;
    clear_color.a = 1.0;
    color_attachment.clearValue = clear_color;

    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.label = gpu::cstring_view(label);
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    if (pass == nullptr) {
        throw EngineError(std::string("wgpuCommandEncoderBeginRenderPass returned null for ") + label + ".");
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// Releases every active local temp-buffer handle during an exceptional render exit.
void release_temp_array(std::array<TempBufferRef, max_bloom_pyramid_levels>& levels) noexcept {
    for (TempBufferRef& level : levels) {
        TempBuffer::release(level);
    }
}

} // namespace

// Reports whether this result has a live bloom texture for the current frame.
bool BloomResult::valid() const noexcept {
    return m_buffer.valid();
}

// Returns the live bloom texture view, or null after release/frame end.
WGPUTextureView BloomResult::view() const noexcept {
    return m_buffer.view();
}

// Converts this result into the compact ToneMapPass input contract.
ToneMapBloomInput BloomResult::tone_map_input() const noexcept {
    if (!valid()) {
        return disabled_tone_map_bloom_input();
    }
    ToneMapBloomInput input;
    input.m_view = view();
    input.m_width = m_width;
    input.m_height = m_height;
    input.m_intensity = m_intensity;
    input.m_tint = m_tint;
    return input;
}

// Stores already-created pass GPU state.
BloomPass::BloomPass(GpuContext gpu,
    WGPUTextureFormat bloom_format,
    WGPUShaderModule prefilter_downsample_shader_module,
    WGPUShaderModule upsample_shader_module,
    WGPUBindGroupLayout prefilter_downsample_bind_group_layout,
    WGPUBindGroupLayout upsample_bind_group_layout,
    WGPUPipelineLayout prefilter_downsample_pipeline_layout,
    WGPUPipelineLayout upsample_pipeline_layout,
    WGPURenderPipeline prefilter_pipeline,
    WGPURenderPipeline downsample_pipeline,
    WGPURenderPipeline upsample_pipeline,
    WGPUBuffer uniform_buffer)
    : m_gpu(std::move(gpu)), m_bloom_format(bloom_format),
      m_prefilter_downsample_shader_module(prefilter_downsample_shader_module),
      m_upsample_shader_module(upsample_shader_module),
      m_prefilter_downsample_bind_group_layout(prefilter_downsample_bind_group_layout),
      m_upsample_bind_group_layout(upsample_bind_group_layout),
      m_prefilter_downsample_pipeline_layout(prefilter_downsample_pipeline_layout),
      m_upsample_pipeline_layout(upsample_pipeline_layout), m_prefilter_pipeline(prefilter_pipeline),
      m_downsample_pipeline(downsample_pipeline), m_upsample_pipeline(upsample_pipeline),
      m_uniform_buffer(uniform_buffer) {}

// Releases owned WebGPU resources.
BloomPass::~BloomPass() {
    release_gpu_state();
}

// Creates shader, layout, pipeline, and persistent uniform state for bloom.
std::unique_ptr<BloomPass> BloomPass::create(GpuContext gpu, WGPUTextureFormat bloom_format) {
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("BloomPass requires a WebGPU device and queue.");
    }
    if (bloom_format != WGPUTextureFormat_RGBA16Float) {
        throw EngineError("BloomPass currently requires RGBA16Float bloom targets.");
    }

    WGPUShaderModule prefilter_downsample_shader_module = nullptr;
    WGPUShaderModule upsample_shader_module = nullptr;
    WGPUBindGroupLayout prefilter_downsample_bind_group_layout = nullptr;
    WGPUBindGroupLayout upsample_bind_group_layout = nullptr;
    WGPUPipelineLayout prefilter_downsample_pipeline_layout = nullptr;
    WGPUPipelineLayout upsample_pipeline_layout = nullptr;
    WGPURenderPipeline prefilter_pipeline = nullptr;
    WGPURenderPipeline downsample_pipeline = nullptr;
    WGPURenderPipeline upsample_pipeline = nullptr;
    WGPUBuffer uniform_buffer = nullptr;

    try {
        prefilter_downsample_shader_module = create_bloom_shader_module(
            gpu.m_device, "OFG bloom prefilter/downsample shader", render::shaders::bloom_prefilter_downsample_wgsl);
        upsample_shader_module =
            create_bloom_shader_module(gpu.m_device, "OFG bloom upsample shader", render::shaders::bloom_upsample_wgsl);
        prefilter_downsample_bind_group_layout = create_prefilter_downsample_bind_group_layout(gpu.m_device);
        upsample_bind_group_layout = create_upsample_bind_group_layout(gpu.m_device);
        prefilter_downsample_pipeline_layout = create_bloom_pipeline_layout(
            gpu.m_device, prefilter_downsample_bind_group_layout, "OFG bloom prefilter/downsample pipeline layout");
        upsample_pipeline_layout = create_bloom_pipeline_layout(
            gpu.m_device, upsample_bind_group_layout, "OFG bloom upsample pipeline layout");
        prefilter_pipeline = create_bloom_pipeline(gpu.m_device,
            prefilter_downsample_pipeline_layout,
            prefilter_downsample_shader_module,
            bloom_format,
            "fs_prefilter",
            "OFG bloom prefilter pipeline");
        downsample_pipeline = create_bloom_pipeline(gpu.m_device,
            prefilter_downsample_pipeline_layout,
            prefilter_downsample_shader_module,
            bloom_format,
            "fs_downsample",
            "OFG bloom downsample pipeline");
        upsample_pipeline = create_bloom_pipeline(gpu.m_device,
            upsample_pipeline_layout,
            upsample_shader_module,
            bloom_format,
            "fs_upsample",
            "OFG bloom upsample pipeline");
        uniform_buffer = create_bloom_uniform_buffer(gpu.m_device);
    } catch (...) {
        if (uniform_buffer != nullptr) {
            wgpuBufferRelease(uniform_buffer);
        }
        if (upsample_pipeline != nullptr) {
            wgpuRenderPipelineRelease(upsample_pipeline);
        }
        if (downsample_pipeline != nullptr) {
            wgpuRenderPipelineRelease(downsample_pipeline);
        }
        if (prefilter_pipeline != nullptr) {
            wgpuRenderPipelineRelease(prefilter_pipeline);
        }
        if (upsample_pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(upsample_pipeline_layout);
        }
        if (prefilter_downsample_pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(prefilter_downsample_pipeline_layout);
        }
        if (upsample_bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(upsample_bind_group_layout);
        }
        if (prefilter_downsample_bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(prefilter_downsample_bind_group_layout);
        }
        if (upsample_shader_module != nullptr) {
            wgpuShaderModuleRelease(upsample_shader_module);
        }
        if (prefilter_downsample_shader_module != nullptr) {
            wgpuShaderModuleRelease(prefilter_downsample_shader_module);
        }
        throw;
    }

    std::unique_ptr<BloomPass> pass(new BloomPass(std::move(gpu),
        bloom_format,
        prefilter_downsample_shader_module,
        upsample_shader_module,
        prefilter_downsample_bind_group_layout,
        upsample_bind_group_layout,
        prefilter_downsample_pipeline_layout,
        upsample_pipeline_layout,
        prefilter_pipeline,
        downsample_pipeline,
        upsample_pipeline,
        uniform_buffer));
    pass->m_counters.m_shader_module_create_count = 2;
    pass->m_counters.m_bind_group_layout_create_count = 2;
    pass->m_counters.m_pipeline_create_count = 3;
    pass->m_counters.m_buffer_create_count = 1;
    return pass;
}

// Encodes prefilter, downsample, and upsample passes into the caller-owned command encoder.
BloomResult BloomPass::render(WGPUCommandEncoder encoder,
    WGPUTextureView scene_color_view,
    std::uint32_t width,
    std::uint32_t height,
    const BloomSettings& settings) {
    m_last_diagnostics = BloomPassDiagnostics{};
    if (encoder == nullptr || scene_color_view == nullptr) {
        throw EngineError("BloomPass render requires an encoder and scene color view.");
    }
    if (width == 0 || height == 0) {
        throw EngineError("BloomPass render dimensions must be nonzero.");
    }

    validate_bloom_settings(settings);
    if (!settings.m_enabled || settings.m_intensity == 0.0f) {
        release_cached_bind_groups();
        m_last_diagnostics.m_skipped = true;
        return BloomResult{};
    }

    const BloomPyramidPlan plan = build_bloom_pyramid_plan(width, height, settings);
    if (plan.empty()) {
        release_cached_bind_groups();
        m_last_diagnostics.m_skipped = true;
        return BloomResult{};
    }
    if (m_cached_scene_color_view != scene_color_view) {
        release_cached_bind_groups();
        m_cached_scene_color_view = scene_color_view;
    }

    BloomUniformBlock uniforms = pack_bloom_uniforms(settings);
    wgpuQueueWriteBuffer(m_gpu.m_queue, m_uniform_buffer, 0, uniforms.m_values.data(), sizeof(uniforms));

    std::array<TempBufferRef, max_bloom_pyramid_levels> levels;
    TempBufferRef current;
    try {
        for (std::uint32_t level = 0; level < plan.m_level_count; ++level) {
            const BloomPyramidLevel planned_level = plan.m_levels[level];
            levels[level] =
                TempBuffer::get(bloom_temp_desc(planned_level.m_width, planned_level.m_height, m_bloom_format),
                    "bloom pyramid level " + std::to_string(level));
        }

        WGPUBindGroup prefilter_bind_group = prefilter_downsample_bind_group(0, scene_color_view);
        encode_bloom_draw(encoder, m_prefilter_pipeline, prefilter_bind_group, levels[0], "OFG bloom prefilter pass");
        m_last_diagnostics.m_estimated_read_bytes += bloom_target_bytes(width, height);
        m_last_diagnostics.m_estimated_write_bytes += bloom_target_bytes(levels[0].width(), levels[0].height());
        m_last_diagnostics.m_encoded_pass_count += 1;
        m_last_diagnostics.m_draw_count += 1;

        for (std::uint32_t level = 1; level < plan.m_level_count; ++level) {
            WGPUBindGroup downsample_bind_group = prefilter_downsample_bind_group(level, levels[level - 1].view());
            encode_bloom_draw(
                encoder, m_downsample_pipeline, downsample_bind_group, levels[level], "OFG bloom downsample pass");
            m_last_diagnostics.m_estimated_read_bytes +=
                bloom_target_bytes(levels[level - 1].width(), levels[level - 1].height());
            m_last_diagnostics.m_estimated_write_bytes +=
                bloom_target_bytes(levels[level].width(), levels[level].height());
            m_last_diagnostics.m_encoded_pass_count += 1;
            m_last_diagnostics.m_draw_count += 1;
        }

        m_last_diagnostics.m_active_level_count = plan.m_level_count;
        if (plan.m_level_count == 1) {
            return BloomResult{std::move(levels[0]),
                plan.m_levels[0].m_width,
                plan.m_levels[0].m_height,
                settings.m_intensity,
                settings.m_tint};
        }

        current = std::move(levels[plan.m_level_count - 1]);
        for (std::uint32_t reversed = plan.m_level_count - 1; reversed > 0; --reversed) {
            const std::uint32_t higher_level_index = reversed - 1;
            const BloomPyramidLevel higher_level = plan.m_levels[higher_level_index];
            TempBufferRef accumulation =
                TempBuffer::get(bloom_temp_desc(higher_level.m_width, higher_level.m_height, m_bloom_format),
                    "bloom upsample accumulation");
            WGPUBindGroup cached_upsample_bind_group =
                upsample_bind_group(higher_level_index, current.view(), levels[higher_level_index].view());
            encode_bloom_draw(
                encoder, m_upsample_pipeline, cached_upsample_bind_group, accumulation, "OFG bloom upsample pass");
            m_last_diagnostics.m_estimated_read_bytes += bloom_target_bytes(current.width(), current.height());
            m_last_diagnostics.m_estimated_read_bytes +=
                bloom_target_bytes(levels[higher_level_index].width(), levels[higher_level_index].height());
            m_last_diagnostics.m_estimated_write_bytes +=
                bloom_target_bytes(accumulation.width(), accumulation.height());
            m_last_diagnostics.m_encoded_pass_count += 1;
            m_last_diagnostics.m_draw_count += 1;

            TempBuffer::release(current);
            TempBuffer::release(levels[higher_level_index]);
            current = std::move(accumulation);
        }
    } catch (...) {
        TempBuffer::release(current);
        release_temp_array(levels);
        throw;
    }

    return BloomResult{
        std::move(current), plan.m_levels[0].m_width, plan.m_levels[0].m_height, settings.m_intensity, settings.m_tint};
}

// Reports durable and transient WebGPU resource creation counters.
RendererCounters BloomPass::counters() const noexcept {
    return m_counters;
}

// Reports the most recent render call's pass-count and byte estimates.
BloomPassDiagnostics BloomPass::diagnostics() const noexcept {
    return m_last_diagnostics;
}

// Releases cached per-view bind groups.
void BloomPass::release_cached_bind_groups() noexcept {
    for (WGPUBindGroup& bind_group : m_prefilter_downsample_bind_groups) {
        if (bind_group != nullptr) {
            wgpuBindGroupRelease(bind_group);
            bind_group = nullptr;
        }
    }
    for (WGPUBindGroup& bind_group : m_upsample_bind_groups) {
        if (bind_group != nullptr) {
            wgpuBindGroupRelease(bind_group);
            bind_group = nullptr;
        }
    }
    m_prefilter_downsample_source_views = {};
    m_upsample_lower_views = {};
    m_upsample_higher_views = {};
    m_cached_scene_color_view = nullptr;
}

// Returns a cached prefilter/downsample bind group for one source view.
WGPUBindGroup BloomPass::prefilter_downsample_bind_group(std::uint32_t index, WGPUTextureView source_view) {
    if (index >= max_bloom_pyramid_levels) {
        throw EngineError("BloomPass prefilter/downsample bind group index is out of range.");
    }
    if (m_prefilter_downsample_bind_groups[index] != nullptr &&
        m_prefilter_downsample_source_views[index] == source_view) {
        return m_prefilter_downsample_bind_groups[index];
    }

    WGPUBindGroup next_bind_group = create_prefilter_downsample_bind_group(
        m_gpu.m_device, m_prefilter_downsample_bind_group_layout, m_uniform_buffer, source_view);
    if (m_prefilter_downsample_bind_groups[index] != nullptr) {
        wgpuBindGroupRelease(m_prefilter_downsample_bind_groups[index]);
    }
    m_prefilter_downsample_bind_groups[index] = next_bind_group;
    m_prefilter_downsample_source_views[index] = source_view;
    m_counters.m_bind_group_create_count += 1;
    return next_bind_group;
}

// Returns a cached upsample bind group for one lower/higher view pair.
WGPUBindGroup BloomPass::upsample_bind_group(
    std::uint32_t index, WGPUTextureView lower_view, WGPUTextureView higher_view) {
    if (index >= max_bloom_pyramid_levels) {
        throw EngineError("BloomPass upsample bind group index is out of range.");
    }
    if (m_upsample_bind_groups[index] != nullptr && m_upsample_lower_views[index] == lower_view &&
        m_upsample_higher_views[index] == higher_view) {
        return m_upsample_bind_groups[index];
    }

    WGPUBindGroup next_bind_group = create_upsample_bind_group(
        m_gpu.m_device, m_upsample_bind_group_layout, m_uniform_buffer, lower_view, higher_view);
    if (m_upsample_bind_groups[index] != nullptr) {
        wgpuBindGroupRelease(m_upsample_bind_groups[index]);
    }
    m_upsample_bind_groups[index] = next_bind_group;
    m_upsample_lower_views[index] = lower_view;
    m_upsample_higher_views[index] = higher_view;
    m_counters.m_bind_group_create_count += 1;
    return next_bind_group;
}

// Releases all WebGPU handles owned by this pass.
void BloomPass::release_gpu_state() noexcept {
    release_cached_bind_groups();
    if (m_uniform_buffer != nullptr) {
        wgpuBufferRelease(m_uniform_buffer);
        m_uniform_buffer = nullptr;
    }
    if (m_upsample_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_upsample_pipeline);
        m_upsample_pipeline = nullptr;
    }
    if (m_downsample_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_downsample_pipeline);
        m_downsample_pipeline = nullptr;
    }
    if (m_prefilter_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_prefilter_pipeline);
        m_prefilter_pipeline = nullptr;
    }
    if (m_upsample_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_upsample_pipeline_layout);
        m_upsample_pipeline_layout = nullptr;
    }
    if (m_prefilter_downsample_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_prefilter_downsample_pipeline_layout);
        m_prefilter_downsample_pipeline_layout = nullptr;
    }
    if (m_upsample_bind_group_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_upsample_bind_group_layout);
        m_upsample_bind_group_layout = nullptr;
    }
    if (m_prefilter_downsample_bind_group_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_prefilter_downsample_bind_group_layout);
        m_prefilter_downsample_bind_group_layout = nullptr;
    }
    if (m_upsample_shader_module != nullptr) {
        wgpuShaderModuleRelease(m_upsample_shader_module);
        m_upsample_shader_module = nullptr;
    }
    if (m_prefilter_downsample_shader_module != nullptr) {
        wgpuShaderModuleRelease(m_prefilter_downsample_shader_module);
        m_prefilter_downsample_shader_module = nullptr;
    }
}

} // namespace ofg
