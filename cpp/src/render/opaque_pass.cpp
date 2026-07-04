// Opaque WebGPU render pass for resolved OFG draw lists.
#include "ofg/render/opaque_pass.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/scene/scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ofg {
namespace {

constexpr std::uint64_t _frame_uniform_bytes = sizeof(float) * 32;
constexpr std::uint64_t _draw_uniform_bytes = sizeof(float) * 32;
constexpr std::uint64_t _draw_uniform_stride = 256;
constexpr std::uint32_t _initial_draw_capacity = 1;
constexpr float _normal_matrix_min_determinant = 0.000001f;

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

// Returns the inverse-transpose upper 3x3 matrix needed for normal/tangent transforms.
math::Mat4 normal_model_from_model(const math::Mat4& model) {
    const math::Vec3 column0 = math::vec3(model[0].x, model[0].y, model[0].z);
    const math::Vec3 column1 = math::vec3(model[1].x, model[1].y, model[1].z);
    const math::Vec3 column2 = math::vec3(model[2].x, model[2].y, model[2].z);
    const math::Vec3 inverse_transpose0 = math::cross(column1, column2);
    const float determinant = math::dot(column0, inverse_transpose0);
    if (!std::isfinite(determinant) || std::fabs(determinant) < _normal_matrix_min_determinant) {
        throw EngineError("Opaque draw model matrix is not invertible for normal transformation.");
    }

    const float inverse_determinant = 1.0f / determinant;
    math::Mat4 normal_model = math::mat4_identity();
    const math::Vec3 inverse_transpose1 = math::cross(column2, column0);
    const math::Vec3 inverse_transpose2 = math::cross(column0, column1);
    normal_model[0] = math::vec4(inverse_transpose0.x * inverse_determinant,
        inverse_transpose0.y * inverse_determinant,
        inverse_transpose0.z * inverse_determinant,
        0.0f);
    normal_model[1] = math::vec4(inverse_transpose1.x * inverse_determinant,
        inverse_transpose1.y * inverse_determinant,
        inverse_transpose1.z * inverse_determinant,
        0.0f);
    normal_model[2] = math::vec4(inverse_transpose2.x * inverse_determinant,
        inverse_transpose2.y * inverse_determinant,
        inverse_transpose2.z * inverse_determinant,
        0.0f);
    return normal_model;
}

// Writes the model matrix and matching normal matrix for one draw command.
void write_draw_uniforms(WGPUQueue queue, WGPUBuffer buffer, std::uint64_t offset, const math::Mat4& model) {
    write_mat4(queue, buffer, offset, model);
    write_mat4(queue, buffer, offset + sizeof(float) * 16U, normal_model_from_model(model));
}

// Writes camera and lighting frame data into the frame uniform buffer.
void write_frame_uniforms(WGPUQueue queue,
    WGPUBuffer buffer,
    const CameraProperties& camera,
    std::span<const LightProperties> lights,
    AmbientLight ambient_light) {
    std::array<float, 32> packed{};
    const std::array<float, 16> view_projection = math::pack_mat4(camera.clip_from_world);
    std::copy(view_projection.begin(), view_projection.end(), packed.begin());

    math::Vec3 main_light_direction{0.0f, -1.0f, 0.0f};
    math::Vec3 main_light_color{0.0f, 0.0f, 0.0f};
    for (const LightProperties& light : lights) {
        if (light.m_type == LightPropertiesType::Directional) {
            main_light_direction = light.m_direction;
            main_light_color = math::mul(light.m_color, light.m_intensity);
            break;
        }
    }

    packed[16] = main_light_direction.x;
    packed[17] = main_light_direction.y;
    packed[18] = main_light_direction.z;
    packed[19] = 0.0f;
    packed[20] = main_light_color.x;
    packed[21] = main_light_color.y;
    packed[22] = main_light_color.z;
    packed[23] = 0.0f;
    packed[24] = ambient_light.m_color.x * ambient_light.m_intensity;
    packed[25] = ambient_light.m_color.y * ambient_light.m_intensity;
    packed[26] = ambient_light.m_color.z * ambient_light.m_intensity;
    packed[27] = 0.0f;
    packed[28] = camera.world_from_camera[3].x;
    packed[29] = camera.world_from_camera[3].y;
    packed[30] = camera.world_from_camera[3].z;
    packed[31] = 1.0f;

    wgpuQueueWriteBuffer(queue, buffer, 0, packed.data(), sizeof(float) * packed.size());
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
    pass->m_frame_layout = create_uniform_layout(gpu.m_device,
        "OFG opaque frame layout",
        _frame_uniform_bytes,
        WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
        false);
    pass->m_frame_buffer = create_uniform_buffer(gpu.m_device, "OFG opaque frame uniforms", _frame_uniform_bytes);
    pass->m_draw_layout = create_uniform_layout(
        gpu.m_device, "OFG opaque draw layout", _draw_uniform_bytes, WGPUShaderStage_Vertex, true);
    pass->m_draw_buffer = create_uniform_buffer(gpu.m_device, "OFG opaque draw uniforms", _draw_uniform_stride);
    pass->m_buffer_create_count = 2;

    pass->m_frame_bind_group = create_uniform_bind_group(
        gpu.m_device, "OFG opaque frame bind group", pass->m_frame_layout, pass->m_frame_buffer, _frame_uniform_bytes);
    pass->m_draw_bind_group = create_uniform_bind_group(
        gpu.m_device, "OFG opaque draw bind group", pass->m_draw_layout, pass->m_draw_buffer, _draw_uniform_bytes);
    pass->m_bind_group_create_count = 2;

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

// Encodes opaque draw commands into the caller-owned scene render pass.
void OpaquePass::draw(WGPURenderPassEncoder pass,
    const CameraProperties& camera,
    std::span<const LightProperties> lights,
    AmbientLight ambient_light,
    const DrawList& draw_list) {
    if (pass == nullptr) {
        throw EngineError("OpaquePass draw requires an open render pass.");
    }
    prepare(draw_list);
    ensure_draw_capacity(static_cast<std::uint32_t>(draw_list.size()));

    write_frame_uniforms(m_gpu.m_queue, m_frame_buffer, camera, lights, ambient_light);
    std::uint32_t draw_index = 0;
    for (const DrawCommand& command : draw_list.commands()) {
        write_draw_uniforms(m_gpu.m_queue,
            m_draw_buffer,
            static_cast<std::uint64_t>(draw_index) * _draw_uniform_stride,
            command.m_model);
        draw_index += 1;
    }

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
}

// Reports durable renderer resource counters.
RendererCounters OpaquePass::counters() const noexcept {
    const PipelineCacheCounters pipeline_counters = m_pipeline_cache.counters();
    RendererCounters counters;
    counters.m_pipeline_create_count = pipeline_counters.m_pipeline_create_count;
    counters.m_buffer_create_count = m_buffer_create_count;
    counters.m_bind_group_layout_create_count = 2;
    counters.m_bind_group_create_count = m_bind_group_create_count;
    return counters;
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
    m_buffer_create_count += 1;
    WGPUBindGroup next_bind_group = nullptr;
    try {
        next_bind_group = create_uniform_bind_group(
            m_gpu.m_device, "OFG opaque draw bind group", m_draw_layout, next_buffer, _draw_uniform_bytes);
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
    m_bind_group_create_count += 1;
}

// Releases pass-level layouts, buffers, and bind groups.
void OpaquePass::release_gpu_state() noexcept {
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
