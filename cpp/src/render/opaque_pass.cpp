// Opaque WebGPU render pass for resolved OFG draw lists.
#include "ofg/render/opaque_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/render/bootstrap_scene.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ofg {
namespace {

constexpr std::uint64_t _matrix_uniform_bytes = sizeof(float) * 16;
constexpr std::uint64_t _draw_uniform_stride = 256;
constexpr std::uint32_t _initial_draw_capacity = 1;

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
WGPUBindGroupLayout create_uniform_layout(WGPUDevice device, const char* label, bool dynamic_offset) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = dynamic_offset ? WGPU_TRUE : WGPU_FALSE;
    entry.buffer.minBindingSize = _matrix_uniform_bytes;

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
    WGPUDevice device, const char* label, WGPUBindGroupLayout layout, WGPUBuffer buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = _matrix_uniform_bytes;

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

// Returns the pipeline key for a resolved material.
PipelineKey pipeline_key_for(
    const Material& material, WGPUTextureFormat color_format, WGPUTextureFormat depth_format) noexcept {
    return PipelineKey{color_format, depth_format, material.bind_group_layout(), material.shader().revision()};
}

// Validates GPU resource handles needed by a draw-list material.
void validate_material_gpu_state(const Material& material) {
    if (material.shader().module() == nullptr) {
        throw EngineError("Opaque draw material shader is not GPU-ready.");
    }
    if (material.bind_group_layout() == nullptr || material.bind_group() == nullptr) {
        throw EngineError("Opaque draw material bind group state is not GPU-ready.");
    }
}

// Validates GPU resource handles needed by a draw-list mesh.
void validate_mesh_gpu_state(const Mesh& mesh) {
    if (mesh.vertex_buffer() == nullptr || mesh.index_buffer() == nullptr) {
        throw EngineError("Opaque draw mesh buffers are not GPU-ready.");
    }
}

} // namespace

// Stores already-created pass GPU state.
OpaquePass::OpaquePass(GpuContext gpu,
    WGPUTextureFormat color_format,
    WGPUBindGroupLayout frame_layout,
    WGPUBuffer frame_buffer,
    WGPUBindGroup frame_bind_group,
    WGPUBindGroupLayout draw_layout,
    WGPUBuffer draw_buffer,
    WGPUBindGroup draw_bind_group,
    std::uint32_t draw_capacity)
    : m_gpu(gpu), m_color_format(color_format), m_frame_layout(frame_layout), m_frame_buffer(frame_buffer),
      m_frame_bind_group(frame_bind_group), m_draw_layout(draw_layout), m_draw_buffer(draw_buffer),
      m_draw_bind_group(draw_bind_group), m_draw_capacity(draw_capacity) {}

// Releases pass-owned GPU handles.
OpaquePass::~OpaquePass() {
    release_gpu_state();
}

// Creates pass-level bind group layouts and uniform buffers.
std::unique_ptr<OpaquePass> OpaquePass::create(GpuContext gpu, WGPUTextureFormat color_format) {
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("OpaquePass requires a WebGPU device and queue.");
    }
    if (color_format == WGPUTextureFormat_Undefined) {
        throw EngineError("OpaquePass requires a defined color format.");
    }

    std::unique_ptr<OpaquePass> pass(new OpaquePass(
        gpu, color_format, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, _initial_draw_capacity));
    pass->m_frame_layout = create_uniform_layout(gpu.m_device, "OFG opaque frame layout", false);
    pass->m_frame_buffer = create_uniform_buffer(gpu.m_device, "OFG opaque frame uniforms", _matrix_uniform_bytes);
    pass->m_draw_layout = create_uniform_layout(gpu.m_device, "OFG opaque draw layout", true);
    pass->m_draw_buffer = create_uniform_buffer(gpu.m_device, "OFG opaque draw uniforms", _draw_uniform_stride);

    pass->m_frame_bind_group = create_uniform_bind_group(
        gpu.m_device, "OFG opaque frame bind group", pass->m_frame_layout, pass->m_frame_buffer);
    pass->m_draw_bind_group =
        create_uniform_bind_group(gpu.m_device, "OFG opaque draw bind group", pass->m_draw_layout, pass->m_draw_buffer);

    return pass;
}

// Ensures a pipeline exists for every valid draw-list material.
void OpaquePass::prepare(const DrawList& draw_list) {
    draw_list.validate();

    for (const DrawCommand& command : draw_list.commands()) {
        validate_mesh_gpu_state(*command.m_mesh);
        const std::span<const SubMesh> submeshes = command.m_mesh->submeshes();
        for (std::uint32_t submesh_index = 0; submesh_index < submeshes.size(); ++submesh_index) {
            Material& material = resolve_material(command, submesh_index);
            validate_material_gpu_state(material);
            const PipelineKey key = pipeline_key_for(material, m_color_format, m_depth_format);
            (void)m_pipeline_cache.get_or_create(
                m_gpu.m_device, key, m_frame_layout, m_draw_layout, material.shader().module());
        }
    }
}

// Resizes or releases the pass depth target.
void OpaquePass::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        release_depth_state();
        m_depth_width = 0;
        m_depth_height = 0;
        return;
    }
    if (m_depth_view != nullptr && width == m_depth_width && height == m_depth_height) {
        return;
    }

    WGPUTexture next_texture =
        gpu::create_depth_texture(m_gpu.m_device, m_depth_format, width, height, "OFG opaque depth texture");
    WGPUTextureView next_view = nullptr;
    try {
        next_view = gpu::create_depth_view(next_texture, m_depth_format, "OFG opaque depth view");
    } catch (...) {
        wgpuTextureRelease(next_texture);
        throw;
    }

    release_depth_state();
    m_depth_texture = next_texture;
    m_depth_view = next_view;
    m_depth_width = width;
    m_depth_height = height;
}

// Encodes opaque draws into the caller-owned command encoder.
void OpaquePass::render(
    WGPUCommandEncoder encoder, RenderTarget target, const RenderView& view, const DrawList& draw_list) {
    if (encoder == nullptr || target.m_view == nullptr) {
        throw EngineError("OpaquePass render requires an encoder and texture view.");
    }
    resize(target.m_width, target.m_height);
    prepare(draw_list);
    ensure_draw_capacity(static_cast<std::uint32_t>(draw_list.size()));

    write_mat4(m_gpu.m_queue, m_frame_buffer, 0, view.m_view_projection);
    std::uint32_t draw_index = 0;
    for (const DrawCommand& command : draw_list.commands()) {
        write_mat4(m_gpu.m_queue,
            m_draw_buffer,
            static_cast<std::uint64_t>(draw_index) * _draw_uniform_stride,
            command.m_model);
        draw_index += 1;
    }

    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = target.m_view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = webgpu_clear_color();

    WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth_attachment.view = m_depth_view;
    depth_attachment.depthLoadOp = WGPULoadOp_Clear;
    depth_attachment.depthStoreOp = WGPUStoreOp_Store;
    depth_attachment.depthClearValue = 1.0F;

    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.label = gpu::cstring_view("OFG opaque pass");
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color_attachment;
    pass_descriptor.depthStencilAttachment = &depth_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    if (pass == nullptr) {
        throw EngineError("wgpuCommandEncoderBeginRenderPass returned null for opaque pass.");
    }

    try {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_frame_bind_group, 0, nullptr);
        draw_index = 0;
        for (const DrawCommand& command : draw_list.commands()) {
            const std::uint32_t dynamic_offset = draw_index * static_cast<std::uint32_t>(_draw_uniform_stride);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, m_draw_bind_group, 1, &dynamic_offset);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, command.m_mesh->vertex_buffer(), 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, command.m_mesh->index_buffer(), WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

            const std::span<const SubMesh> submeshes = command.m_mesh->submeshes();
            for (std::uint32_t submesh_index = 0; submesh_index < submeshes.size(); ++submesh_index) {
                Material& material = resolve_material(command, submesh_index);
                const PipelineKey key = pipeline_key_for(material, m_color_format, m_depth_format);
                WGPURenderPipeline pipeline = m_pipeline_cache.get_or_create(
                    m_gpu.m_device, key, m_frame_layout, m_draw_layout, material.shader().module());
                const SubMesh& submesh = submeshes[submesh_index];
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 2, material.bind_group(), 0, nullptr);
                wgpuRenderPassEncoderDrawIndexed(pass, submesh.m_index_count, 1, submesh.m_index_start, 0, 0);
            }
            draw_index += 1;
        }
    } catch (...) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        throw;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// Reports durable renderer resource counters.
RendererCounters OpaquePass::counters() const noexcept {
    const PipelineCacheCounters pipeline_counters = m_pipeline_cache.counters();
    return RendererCounters{pipeline_counters.m_pipeline_create_count, 1};
}

// Recreates the dynamic draw uniform buffer for a larger command count.
void OpaquePass::ensure_draw_capacity(std::uint32_t draw_count) {
    if (draw_count <= m_draw_capacity) {
        return;
    }

    std::uint32_t next_capacity = std::max(m_draw_capacity, _initial_draw_capacity);
    while (next_capacity < draw_count) {
        next_capacity *= 2;
    }

    WGPUBuffer next_buffer = create_uniform_buffer(
        m_gpu.m_device, "OFG opaque draw uniforms", static_cast<std::uint64_t>(next_capacity) * _draw_uniform_stride);
    WGPUBindGroup next_bind_group = nullptr;
    try {
        next_bind_group =
            create_uniform_bind_group(m_gpu.m_device, "OFG opaque draw bind group", m_draw_layout, next_buffer);
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
}

// Releases the current depth texture and view.
void OpaquePass::release_depth_state() noexcept {
    if (m_depth_view != nullptr) {
        wgpuTextureViewRelease(m_depth_view);
        m_depth_view = nullptr;
    }
    if (m_depth_texture != nullptr) {
        wgpuTextureRelease(m_depth_texture);
        m_depth_texture = nullptr;
    }
}

// Releases pass-level layouts, buffers, bind groups, and depth state.
void OpaquePass::release_gpu_state() noexcept {
    release_depth_state();
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
    m_draw_capacity = 0;
}

} // namespace ofg
