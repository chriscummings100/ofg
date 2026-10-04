// Shared indexed UI submission with immutable frame uploads and explicit texture lifetime.
#include "ui/imgui-renderer.h"
#include "core/engine-error.h"
#include "imgui-shader.h"
#include <slang-rhi/shader-cursor.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace ofg {
namespace {
// Preserves the failing GPU operation in application diagnostics.
void check(rhi::Result result, const char* operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(std::string(operation) + " failed (" + std::to_string(result) + ").");
    }
}
} // namespace

ImGuiRenderer::ImGuiRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
    using namespace rhi;
    ComPtr<slang::IBlob> diagnostics;
    auto module = device->getSlangSession()
                      ->loadModuleFromSourceString("ofg-imgui", "imgui.slang", imguiShader, diagnostics.writeRef());
    if (!module)
    {
        throw EngineError(
            diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "UI shader failed."
        );
    }
    ComPtr<slang::IEntryPoint> vertex, fragment;
    check(module->findEntryPointByName("vertexMain", vertex.writeRef()), "Find UI vertex");
    check(module->findEntryPointByName("fragmentMain", fragment.writeRef()), "Find UI fragment");
    slang::IComponentType* entries[]{vertex, fragment};
    ShaderProgramDesc program{};
    program.linkingStyle = LinkingStyle::SingleProgram;
    program.slangGlobalScope = module;
    program.slangEntryPoints = entries;
    program.slangEntryPointCount = 2;
    auto result = device->createShaderProgram(program, m_program.writeRef(), diagnostics.writeRef());
    if (diagnostics)
    {
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
    }
    check(result, "Create UI program");

    InputElementDesc elements[]{
        {"POSITION", 0, Format::RG32Float, offsetof(ImDrawVert, pos), 0},
        {"TEXCOORD", 1, Format::RG32Float, offsetof(ImDrawVert, uv), 0},
        {"COLOR", 2, Format::RGBA8Unorm, offsetof(ImDrawVert, col), 0}
    };
    ComPtr<IInputLayout> layout;
    check(device->createInputLayout(sizeof(ImDrawVert), elements, 3, layout.writeRef()), "Create UI layout");
    ColorTargetDesc color{};
    color.format = Format::RGBA16Float;
    color.enableBlend = true;
    color.color = {BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, BlendOp::Add};
    color.alpha = {BlendFactor::One, BlendFactor::InvSrcAlpha, BlendOp::Add};
    RenderPipelineDesc pipeline{};
    pipeline.program = m_program;
    pipeline.inputLayout = layout;
    pipeline.targets = &color;
    pipeline.targetCount = 1;
    pipeline.rasterizer.cullMode = CullMode::None;
    pipeline.depthStencil.depthTestEnable = false;
    pipeline.depthStencil.depthWriteEnable = false;
    check(device->createRenderPipeline(pipeline, m_pipeline.writeRef()), "Create UI pipeline");
    SamplerDesc sampler{};
    sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
    check(device->createSampler(sampler, m_sampler.writeRef()), "Create UI sampler");
    auto& io = ImGui::GetIO();
    io.BackendRendererName = "ofg-slang-rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Renderer_TextureMaxWidth = device->getInfo().limits.maxTextureDimension2D;
    ImGui::GetPlatformIO().Renderer_TextureMaxHeight = device->getInfo().limits.maxTextureDimension2D;
}

ImGuiRenderer::~ImGuiRenderer()
{
    for (auto& [data, texture] : m_textures)
    {
        data->SetTexID(ImTextureID_Invalid);
        data->SetStatus(ImTextureStatus_Destroyed);
    }
    auto& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

ImTextureID ImGuiRenderer::textureId(rhi::ITexture* texture)
{
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture));
}

rhi::ComPtr<rhi::ITexture> ImGuiRenderer::createTarget(uint32_t width, uint32_t height)
{
    rhi::TextureDesc desc{};
    desc.size = {width, height, 1};
    desc.format = rhi::Format::RGBA16Float;
    desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopySource;
    desc.defaultState = rhi::ResourceState::RenderTarget;
    rhi::ComPtr<rhi::ITexture> result;
    check(m_device->createTexture(desc, nullptr, result.writeRef()), "Create workspace target");
    return result;
}

void ImGuiRenderer::updateTextures(ImDrawData& data)
{
    if (!data.Textures)
    {
        return;
    }
    for (auto texture : *data.Textures)
    {
        if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
        {
            if (texture->Format != ImTextureFormat_RGBA32)
            {
                throw EngineError("UI requires an RGBA32 font atlas.");
            }
            rhi::TextureDesc desc{};
            desc.size = {uint32_t(texture->Width), uint32_t(texture->Height), 1};
            desc.format = rhi::Format::RGBA8Unorm;
            desc.usage = rhi::TextureUsage::ShaderResource;
            desc.defaultState = rhi::ResourceState::ShaderResource;
            rhi::SubresourceData pixels{
                texture->GetPixels(),
                size_t(texture->GetPitch()),
                size_t(texture->GetSizeInBytes())
            };
            rhi::ComPtr<rhi::ITexture> image;
            check(m_device->createTexture(desc, &pixels, image.writeRef()), "Upload UI atlas");
            texture->SetTexID(textureId(image));
            m_textures[texture] = std::move(image);
            texture->SetStatus(ImTextureStatus_OK);
        }
        else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0)
        {
            m_textures.erase(texture);
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void ImGuiRenderer::render(ImDrawData& data, rhi::ITexture* target)
{
    using namespace rhi;
    if (target->getDesc().format != Format::RGBA16Float)
    {
        throw EngineError("UI composition requires an RGBA16Float target.");
    }
    updateTextures(data);
    if (data.DisplaySize.x <= 0 || data.DisplaySize.y <= 0)
    {
        return;
    }

    // Fresh buffers never overwrite queued frames. RHI command buffers retain them after submission.
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    vertices.reserve(data.TotalVtxCount);
    indices.reserve(data.TotalIdxCount);
    for (auto list : data.CmdLists)
    {
        vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
        indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
    }
    // WebGPU queue writes require four-byte sizes, even when a triangle leaves an odd 16-bit index count.
    // Padding belongs only to storage; draw offsets/counts still refer to the original lists.
    if (sizeof(ImDrawIdx) == 2 && indices.size() % 2 != 0)
    {
        indices.push_back(0);
    }
    ComPtr<IBuffer> vertexBuffer, indexBuffer;
    if (!vertices.empty() && !indices.empty())
    {
        BufferDesc desc{};
        desc.size = vertices.size() * sizeof(ImDrawVert);
        desc.usage = BufferUsage::VertexBuffer;
        desc.defaultState = ResourceState::VertexBuffer;
        check(m_device->createBuffer(desc, vertices.data(), vertexBuffer.writeRef()), "Upload UI vertices");
        desc.size = indices.size() * sizeof(ImDrawIdx);
        desc.usage = BufferUsage::IndexBuffer;
        desc.defaultState = ResourceState::IndexBuffer;
        check(m_device->createBuffer(desc, indices.data(), indexBuffer.writeRef()), "Upload UI indices");
    }

    ComPtr<ICommandEncoder> encoder;
    check(m_queue->createCommandEncoder(encoder.writeRef()), "Create UI encoder");
    RenderPassColorAttachment attachment{};
    attachment.view = target->getDefaultView();
    attachment.loadOp = LoadOp::Clear;
    attachment.clearValue[3] = 1;
    RenderPassDesc passDesc{};
    passDesc.colorAttachments = &attachment;
    passDesc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(passDesc);
    const auto size = target->getDesc().size;
    const float transform[]{
        2.f / data.DisplaySize.x,
        -2.f / data.DisplaySize.y,
        -1.f - data.DisplayPos.x * 2.f / data.DisplaySize.x,
        1.f + data.DisplayPos.y * 2.f / data.DisplaySize.y
    };
    uint32_t baseVertex = 0, baseIndex = 0;
    for (auto list : data.CmdLists)
    {
        for (const auto& command : list->CmdBuffer)
        {
            if (command.UserCallback)
            {
                if (command.UserCallback != ImDrawCallback_ResetRenderState)
                {
                    command.UserCallback(list, &command);
                }
                continue; // Every subsequent draw binds its complete state again.
            }
            auto clip = command.ClipRect;
            const auto x0 = uint32_t(
                std::clamp(std::floor((clip.x - data.DisplayPos.x) * data.FramebufferScale.x), 0.f, float(size.width))
            );
            const auto y0 = uint32_t(
                std::clamp(std::floor((clip.y - data.DisplayPos.y) * data.FramebufferScale.y), 0.f, float(size.height))
            );
            const auto x1 = uint32_t(
                std::clamp(std::ceil((clip.z - data.DisplayPos.x) * data.FramebufferScale.x), 0.f, float(size.width))
            );
            const auto y1 = uint32_t(
                std::clamp(std::ceil((clip.w - data.DisplayPos.y) * data.FramebufferScale.y), 0.f, float(size.height))
            );
            if (x1 <= x0 || y1 <= y0 || command.ElemCount == 0)
            {
                continue;
            }
            auto image = reinterpret_cast<ITexture*>(static_cast<uintptr_t>(command.GetTexID()));
            if (!image)
            {
                throw EngineError("UI draw has no texture.");
            }
            ComPtr<IShaderObject> root;
            check(m_device->createRootShaderObject(m_program, root.writeRef()), "Create UI bindings");
            check(ShaderCursor(root)["ui"]["scale"].setData(transform, sizeof(float) * 2), "Set UI scale");
            check(ShaderCursor(root)["ui"]["offset"].setData(transform + 2, sizeof(float) * 2), "Set UI offset");
            check(ShaderCursor(root)["image"].setBinding(image), "Bind UI image");
            check(ShaderCursor(root)["imageSampler"].setBinding(m_sampler), "Bind UI sampler");
            pass->bindPipeline(m_pipeline, root);
            RenderState state{};
            state.viewports[0] = Viewport::fromSize(size.width, size.height);
            state.viewportCount = 1;
            state.scissorRects[0] = {x0, y0, x1, y1};
            state.scissorRectCount = 1;
            state.vertexBuffers[0].buffer = vertexBuffer;
            state.vertexBufferCount = 1;
            state.indexBuffer.buffer = indexBuffer;
            state.indexFormat = sizeof(ImDrawIdx) == 2 ? IndexFormat::Uint16 : IndexFormat::Uint32;
            pass->setRenderState(state);
            DrawArguments draw{};
            draw.vertexCount = command.ElemCount;
            draw.startIndexLocation = baseIndex + command.IdxOffset;
            draw.startVertexLocation = baseVertex + command.VtxOffset;
            pass->drawIndexed(draw);
        }
        baseVertex += list->VtxBuffer.Size;
        baseIndex += list->IdxBuffer.Size;
    }
    pass->end();
    ComPtr<ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish UI commands");
    check(m_queue->submit(commands), "Submit UI commands");
}
} // namespace ofg
