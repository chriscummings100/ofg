// WebGPU renderer for the deterministic bootstrap triangle.
//
// The implementation is deliberately tiny but production-shaped: shader module,
// pipeline layout, render pipeline, and vertex buffer are created once, while
// each frame only encodes render commands into a caller-owned command encoder.
#include "ofg/render/bootstrap_renderer.hpp"

#include "ofg/render/bootstrap_scene.hpp"
#include "ofg/render/webgpu_common.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace ofg {
namespace {

constexpr char _bootstrap_shader_source[] = R"wgsl(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

@vertex
fn vs_main(
    @location(0) position: vec2<f32>,
    @location(1) color: vec3<f32>,
) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4<f32>(position, 0.0, 1.0);
    out.color = color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(in.color, 1.0);
}
)wgsl";

// Converts the shared scene clear color into the WebGPU descriptor type.
WGPUColor webgpu_clear_color() noexcept {
    const ClearColor color = clear_color();
    return WGPUColor{color.m_r, color.m_g, color.m_b, color.m_a};
}

// Creates the WGSL shader module used by the bootstrap triangle pipeline.
WGPUShaderModule create_shader_module(WGPUDevice device, std::string& error) {
    WGPUShaderSourceWGSL shader_source = WGPU_SHADER_SOURCE_WGSL_INIT;
    shader_source.code = gpu::cstring_view(_bootstrap_shader_source);

    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &shader_source.chain;
    descriptor.label = gpu::cstring_view("OFG C++ bootstrap shader");

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &descriptor);
    if (shader == nullptr) {
        error = "wgpuDeviceCreateShaderModule returned null.";
    }
    return shader;
}

// Creates an empty pipeline layout because the bootstrap shader has no bindings.
WGPUPipelineLayout create_pipeline_layout(WGPUDevice device, std::string& error) {
    WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG C++ bootstrap pipeline layout");

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (layout == nullptr) {
        error = "wgpuDeviceCreatePipelineLayout returned null.";
    }
    return layout;
}

// Creates the render pipeline for BootstrapVertex position/color attributes.
WGPURenderPipeline create_render_pipeline(WGPUDevice device,
    WGPUTextureFormat format,
    WGPUShaderModule shader,
    WGPUPipelineLayout layout,
    std::string& error) {
    // Describe the tightly packed CPU vertex layout shared with doctests.
    std::array<WGPUVertexAttribute, 2> attributes{WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
    attributes[0].format = WGPUVertexFormat_Float32x2;
    attributes[0].offset = bootstrap_vertex_position_offset();
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = bootstrap_vertex_color_offset();
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertex_buffer_layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertex_buffer_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_buffer_layout.arrayStride = bootstrap_vertex_stride_bytes();
    vertex_buffer_layout.attributeCount = attributes.size();
    vertex_buffer_layout.attributes = attributes.data();

    // Connect vertex and fragment entry points to the selected target format.
    WGPUVertexState vertex_state = WGPU_VERTEX_STATE_INIT;
    vertex_state.module = shader;
    vertex_state.entryPoint = gpu::cstring_view("vs_main");
    vertex_state.bufferCount = 1;
    vertex_state.buffers = &vertex_buffer_layout;

    WGPUColorTargetState color_target = WGPU_COLOR_TARGET_STATE_INIT;
    color_target.format = format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
    fragment_state.module = shader;
    fragment_state.entryPoint = gpu::cstring_view("fs_main");
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = WGPUPrimitiveTopology_TriangleList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG C++ bootstrap pipeline");
    descriptor.layout = layout;
    descriptor.vertex = vertex_state;
    descriptor.primitive = primitive;
    descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    if (pipeline == nullptr) {
        error = "wgpuDeviceCreateRenderPipeline returned null.";
    }
    return pipeline;
}

// Creates and uploads the static bootstrap vertex buffer once.
WGPUBuffer create_vertex_buffer(WGPUDevice device, WGPUQueue queue, std::string& error) {
    const auto& vertices = bootstrap_vertices();
    const std::size_t vertex_bytes = sizeof(BootstrapVertex) * vertices.size();

    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG C++ bootstrap vertex buffer");
    descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    descriptor.size = vertex_bytes;
    descriptor.mappedAtCreation = WGPU_FALSE;

    WGPUBuffer vertex_buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    if (vertex_buffer == nullptr) {
        error = "wgpuDeviceCreateBuffer returned null.";
        return nullptr;
    }

    wgpuQueueWriteBuffer(queue, vertex_buffer, 0, vertices.data(), vertex_bytes);
    return vertex_buffer;
}

} // namespace

// Stores the durable WebGPU handles after create() has validated them.
BootstrapRenderer::BootstrapRenderer(WGPURenderPipeline pipeline, WGPUBuffer vertex_buffer)
    : m_pipeline(pipeline), m_vertex_buffer(vertex_buffer), m_counters(RendererCounters{1, 1}) {}

// Releases durable WebGPU resources owned by the renderer.
BootstrapRenderer::~BootstrapRenderer() {
    if (m_vertex_buffer != nullptr) {
        wgpuBufferRelease(m_vertex_buffer);
        m_vertex_buffer = nullptr;
    }
    if (m_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
}

// Creates all durable resources needed by the bootstrap renderer.
std::unique_ptr<BootstrapRenderer> BootstrapRenderer::create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format, std::string& error) {
    // Validate caller-provided WebGPU handles before allocating resources.
    if (device == nullptr || queue == nullptr) {
        error = "BootstrapRenderer requires a WebGPU device and queue.";
        return nullptr;
    }
    if (format == WGPUTextureFormat_Undefined) {
        error = "BootstrapRenderer requires a defined surface format.";
        return nullptr;
    }

    // Build dependent pipeline resources in order, releasing partial state on failure.
    WGPUShaderModule shader = create_shader_module(device, error);
    if (shader == nullptr) {
        return nullptr;
    }

    WGPUPipelineLayout layout = create_pipeline_layout(device, error);
    if (layout == nullptr) {
        wgpuShaderModuleRelease(shader);
        return nullptr;
    }

    WGPURenderPipeline pipeline = create_render_pipeline(device, format, shader, layout, error);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(shader);
    if (pipeline == nullptr) {
        return nullptr;
    }

    WGPUBuffer vertex_buffer = create_vertex_buffer(device, queue, error);
    if (vertex_buffer == nullptr) {
        wgpuRenderPipelineRelease(pipeline);
        return nullptr;
    }

    return std::unique_ptr<BootstrapRenderer>(new BootstrapRenderer(pipeline, vertex_buffer));
}

// Encodes a clear+draw pass into the caller-provided command encoder.
bool BootstrapRenderer::render_to_view(WGPUCommandEncoder encoder, WGPUTextureView view, std::string& error) const {
    // Reject invalid per-frame handles without mutating renderer state.
    if (encoder == nullptr || view == nullptr) {
        error = "BootstrapRenderer render requires an encoder and texture view.";
        return false;
    }
    if (m_pipeline == nullptr || m_vertex_buffer == nullptr) {
        error = "BootstrapRenderer resources are not initialized.";
        return false;
    }

    // Encode one render pass and keep presentation/submission with the caller.
    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = webgpu_clear_color();

    WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.label = gpu::cstring_view("OFG C++ bootstrap render pass");
    pass_descriptor.colorAttachmentCount = 1;
    pass_descriptor.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    if (pass == nullptr) {
        error = "wgpuCommandEncoderBeginRenderPass returned null.";
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, static_cast<std::uint32_t>(bootstrap_vertices().size()), 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

// Reports durable resource creation counts for smoke/performance checks.
RendererCounters BootstrapRenderer::counters() const noexcept {
    return m_counters;
}

} // namespace ofg
