// Render pipeline cache for OFG opaque draw submission.
#include "ofg/render/pipeline_cache.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/gpu/common.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace ofg {
namespace {

// Creates a pipeline layout for frame, draw, and material bind groups.
WGPUPipelineLayout create_pipeline_layout(WGPUDevice device,
    WGPUBindGroupLayout frame_layout,
    WGPUBindGroupLayout draw_layout,
    WGPUBindGroupLayout material_layout) {
    std::array<WGPUBindGroupLayout, 3> layouts{frame_layout, draw_layout, material_layout};

    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG opaque pipeline layout");
    descriptor.bindGroupLayoutCount = layouts.size();
    descriptor.bindGroupLayouts = layouts.data();

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreatePipelineLayout returned null for opaque pipeline.");
    }
    return layout;
}

// Creates the WebGPU render pipeline for MeshVertex opaque draws.
WGPURenderPipeline create_render_pipeline(WGPUDevice device,
    const PipelineKey& key,
    WGPUBindGroupLayout frame_layout,
    WGPUBindGroupLayout draw_layout,
    WGPUShaderModule shader_module) {
    WGPUPipelineLayout layout = create_pipeline_layout(device, frame_layout, draw_layout, key.m_material_layout);

    std::array<WGPUVertexAttribute, 4> attributes{
        WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(MeshVertex, m_position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = offsetof(MeshVertex, m_normal);
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x4;
    attributes[2].offset = offsetof(MeshVertex, m_tangent);
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Float32x2;
    attributes[3].offset = offsetof(MeshVertex, m_uv);
    attributes[3].shaderLocation = 3;

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

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = key.m_color_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
    fragment_state.module = shader_module;
    fragment_state.entryPoint = gpu::cstring_view("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth_stencil.format = key.m_depth_format;
    depth_stencil.depthWriteEnabled = WGPUOptionalBool_True;
    depth_stencil.depthCompare = WGPUCompareFunction_Less;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG opaque pipeline");
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.depthStencil = &depth_stencil;
    descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    wgpuPipelineLayoutRelease(layout);
    if (pipeline == nullptr) {
        throw EngineError("wgpuDeviceCreateRenderPipeline returned null for opaque pipeline.");
    }
    return pipeline;
}

} // namespace

// Compares two pipeline keys by render-state identity.
bool operator==(const PipelineKey& left, const PipelineKey& right) noexcept {
    return left.m_color_format == right.m_color_format && left.m_depth_format == right.m_depth_format &&
           left.m_material_layout == right.m_material_layout && left.m_shader_revision == right.m_shader_revision;
}

// Moves cached pipelines without duplicating ownership.
PipelineCache::PipelineCache(PipelineCache&& other) noexcept
    : m_entries(std::move(other.m_entries)), m_counters(other.m_counters) {
    other.m_entries.clear();
    other.m_counters = PipelineCacheCounters{};
}

// Releases current pipelines, then takes ownership from another cache.
PipelineCache& PipelineCache::operator=(PipelineCache&& other) noexcept {
    if (this != &other) {
        clear();
        m_entries = std::move(other.m_entries);
        m_counters = other.m_counters;
        other.m_entries.clear();
        other.m_counters = PipelineCacheCounters{};
    }
    return *this;
}

// Releases cached WebGPU pipelines.
PipelineCache::~PipelineCache() {
    clear();
}

// Returns an existing pipeline or creates one for the supplied layouts.
WGPURenderPipeline PipelineCache::get_or_create(WGPUDevice device,
    PipelineKey key,
    WGPUBindGroupLayout frame_layout,
    WGPUBindGroupLayout draw_layout,
    WGPUShaderModule shader_module) {
    if (device == nullptr || frame_layout == nullptr || draw_layout == nullptr || key.m_material_layout == nullptr ||
        shader_module == nullptr) {
        throw EngineError("Opaque pipeline creation requires device, bind group layouts, and shader module.");
    }
    if (key.m_color_format == WGPUTextureFormat_Undefined || key.m_depth_format == WGPUTextureFormat_Undefined) {
        throw EngineError("Opaque pipeline creation requires defined color and depth formats.");
    }

    for (const Entry& entry : m_entries) {
        if (entry.m_key == key) {
            return entry.m_pipeline;
        }
    }

    WGPURenderPipeline pipeline = create_render_pipeline(device, key, frame_layout, draw_layout, shader_module);
    m_entries.push_back(Entry{key, pipeline});
    m_counters.m_pipeline_create_count += 1;
    return pipeline;
}

// Releases all cached pipelines.
void PipelineCache::clear() noexcept {
    for (Entry& entry : m_entries) {
        if (entry.m_pipeline != nullptr) {
            wgpuRenderPipelineRelease(entry.m_pipeline);
            entry.m_pipeline = nullptr;
        }
    }
    m_entries.clear();
}

// Reports cache creation counters.
PipelineCacheCounters PipelineCache::counters() const noexcept {
    return m_counters;
}

} // namespace ofg
