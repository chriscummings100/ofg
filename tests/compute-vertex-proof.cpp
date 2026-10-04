// Proves queue-ordered uploads, compute writes and vertex consumption without adding a runtime skinning fallback.
#include "compute-vertex-proof.h"
#include "compute-vertex-proof-shader.h"
#include "core/engine-error.h"
#include <slang-rhi/shader-cursor.h>
#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace ofg {
namespace {
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(sizeof(float) == 4 && sizeof(Vertex) == 72);
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 12);
static_assert(offsetof(Vertex, uv) == 24 && offsetof(Vertex, tangent) == 32);
static_assert(offsetof(Vertex, uv1) == 48 && offsetof(Vertex, color) == 56);

// Keeps the failing RHI operation in proof diagnostics on either host.
void check(rhi::Result result, const char* operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(std::string(operation) + " failed: " + std::to_string(result));
    }
}

// Separates compute globals from the resource-free draw program: WebGPU requires all reflected bindings populated.
rhi::ComPtr<rhi::IShaderProgram> program(rhi::IDevice* device, bool compute, std::initializer_list<const char*> names)
{
    rhi::ComPtr<slang::IBlob> diagnostics;
    const std::string source = std::string(compute ? "#define COMPUTE_PROOF 1\n" : "") + computeVertexProofShader;
    auto module = device->getSlangSession()->loadModuleFromSourceString(
        compute ? "proof_compute" : "proof_draw",
        compute ? "proof-compute.slang" : "proof-draw.slang",
        source.c_str(),
        diagnostics.writeRef()
    );
    if (diagnostics)
    {
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
    }
    if (!module)
    {
        throw EngineError("Load compute vertex proof shader failed.");
    }
    std::vector<rhi::ComPtr<slang::IEntryPoint>> entries;
    std::vector<slang::IComponentType*> components;
    for (auto name : names)
    {
        rhi::ComPtr<slang::IEntryPoint> entry;
        check(module->findEntryPointByName(name, entry.writeRef()), "Find proof entry point");
        components.push_back(entry);
        entries.push_back(std::move(entry));
    }
    rhi::ShaderProgramDesc desc{};
    desc.linkingStyle = rhi::LinkingStyle::SingleProgram;
    desc.slangGlobalScope = module;
    desc.slangEntryPoints = components.data();
    desc.slangEntryPointCount = components.size();
    rhi::ComPtr<rhi::IShaderProgram> result;
    auto status = device->createShaderProgram(desc, result.writeRef(), diagnostics.writeRef());
    if (diagnostics)
    {
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
    }
    check(status, "Compile proof program");
    return result;
}
} // namespace

std::array<Vertex, ComputeVertexProof::vertexCount> ComputeVertexProof::sourceVertices()
{
    std::array<Vertex, vertexCount> vertices{};
    for (auto& vertex : vertices)
    {
        vertex.position = {-.5f, 0, .5f};
        vertex.normal = {0, 0, -1};
        vertex.uv = {.25f, .75f};
        vertex.tangent = {1, 0, 0, -1};
        vertex.uv1 = {.5f, .125f};
        vertex.color = {0, 1, 0, 1};
    }
    vertices[0].position = {-.8f, -.6f, .5f};
    vertices[1].position = {-.2f, -.6f, .5f};
    vertices[2].position = {-.5f, .6f, .5f};
    vertices.back().tangent = {}; // Also preserve the derivative-frame marker.
    return vertices;
}

ComputeVertexProof::ComputeVertexProof(rhi::IDevice* device, rhi::ICommandQueue* queue, rhi::Format format)
    : m_queue(queue)
{
    const auto& limits = device->getInfo().limits;
    if (limits.maxBufferSize < vertexCount * sizeof(Vertex) || limits.maxComputeThreadsPerGroup < 64 ||
        limits.maxComputeThreadGroupSize[0] < 64 || limits.maxComputeDispatchThreadGroups[0] < 2 ||
        limits.maxVertexStreamStride < sizeof(Vertex))
    {
        throw EngineError("Compute vertex proof requires 4680-byte buffers, 64-thread groups and two dispatch groups.");
    }

    const auto source = sourceVertices();
    rhi::BufferDesc buffer{};
    buffer.size = sizeof(source);
    buffer.usage = rhi::BufferUsage::ShaderResource;
    buffer.defaultState = rhi::ResourceState::ShaderResource;
    check(device->createBuffer(buffer, source.data(), m_source.writeRef()), "Create immutable proof vertices");
    buffer.usage = rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::CopySource;
    buffer.defaultState = rhi::ResourceState::UnorderedAccess;
    check(device->createBuffer(buffer, nullptr, m_output.writeRef()), "Create proof output vertices");
    buffer.size = 16;
    buffer.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    buffer.defaultState = rhi::ResourceState::ShaderResource;
    check(device->createBuffer(buffer, nullptr, m_translation.writeRef()), "Create proof translation buffer");

    rhi::ComputePipelineDesc compute{};
    auto computeProgram = program(device, true, {"computeMain"});
    compute.program = computeProgram;
    check(device->createComputePipeline(compute, m_compute.writeRef()), "Create proof compute pipeline");

    const rhi::InputElementDesc elements[] = {
        {"POSITION", 0, rhi::Format::RGB32Float, 0, 0},
        {"COLOR", 1, rhi::Format::RGBA32Float, 56, 0}
    };
    const rhi::VertexStreamDesc stream{sizeof(Vertex), rhi::InputSlotClass::PerVertex, 0};
    rhi::InputLayoutDesc input{};
    input.inputElements = elements;
    input.inputElementCount = 2;
    input.vertexStreams = &stream;
    input.vertexStreamCount = 1;
    rhi::ComPtr<rhi::IInputLayout> layout;
    check(device->createInputLayout(input, layout.writeRef()), "Create proof vertex layout");
    rhi::ColorTargetDesc color{};
    color.format = format;
    auto renderProgram = program(device, false, {"vertexMain", "fragmentMain"});
    rhi::RenderPipelineDesc render{};
    render.program = renderProgram;
    render.inputLayout = layout;
    render.targets = &color;
    render.targetCount = 1;
    render.rasterizer.cullMode = rhi::CullMode::None;
    render.depthStencil.depthTestEnable = false;
    render.depthStencil.depthWriteEnable = false;
    check(device->createRenderPipeline(render, m_render.writeRef()), "Create proof render pipeline");
}

void ComputeVertexProof::draw(rhi::ITexture* target, float translationX)
{
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    check(m_queue->createCommandEncoder(encoder.writeRef()), "Create proof encoder");
    const float translation[4] = {translationX, 0, 0, 0};
    check(encoder->uploadBufferData(m_translation, 0, sizeof(translation), translation), "Upload proof translation");
    auto compute = encoder->beginComputePass();
    auto cursor = rhi::ShaderCursor(compute->bindPipeline(m_compute));
    check(cursor["sourceVertices"].setBinding(m_source), "Bind proof source");
    check(cursor["translation"].setBinding(m_translation), "Bind proof translation");
    check(cursor["deformedVertices"].setBinding(m_output), "Bind proof output");
    compute->dispatchCompute((vertexCount + 63) / 64, 1, 1);
    compute->end();
    encoder->setBufferState(m_output, rhi::ResourceState::VertexBuffer);

    rhi::RenderPassColorAttachment attachment{};
    attachment.view = target->getDefaultView();
    attachment.loadOp = rhi::LoadOp::Clear;
    attachment.clearValue[3] = 1;
    rhi::RenderPassDesc passDesc{};
    passDesc.colorAttachments = &attachment;
    passDesc.colorAttachmentCount = 1;
    auto render = encoder->beginRenderPass(passDesc);
    render->bindPipeline(m_render);
    const auto size = target->getDesc().size;
    rhi::RenderState state{};
    state.viewports[0] = rhi::Viewport::fromSize(size.width, size.height);
    state.viewportCount = 1;
    state.scissorRects[0] = rhi::ScissorRect::fromSize(size.width, size.height);
    state.scissorRectCount = 1;
    state.vertexBuffers[0].buffer = m_output;
    state.vertexBufferCount = 1;
    render->setRenderState(state);
    rhi::DrawArguments draw{};
    draw.vertexCount = 3;
    render->draw(draw);
    render->end();
    rhi::ComPtr<rhi::ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish proof commands");
    check(m_queue->submit(commands), "Submit proof commands");
}
} // namespace ofg
