// Direct RHI upload/view/sampler preparation and render-pass mip reduction, with explicit GPU ownership.
#include "render/texture-renderer.h"
#include "core/engine-error.h"
#include "mipmaps-shader.h"
#include <slang-rhi/shader-cursor.h>
#include <algorithm>

namespace ofg {
namespace {
// Keeps operation context on every RHI error.
void check(rhi::Result result, const char* operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(std::string(operation) + " failed (" + std::to_string(result) + ").");
    }
}
} // namespace

rhi::Format textureFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8Unorm:
        return rhi::Format::R8Unorm;
    case TextureFormat::RG8Unorm:
        return rhi::Format::RG8Unorm;
    case TextureFormat::RGBA8Unorm:
        return rhi::Format::RGBA8Unorm;
    case TextureFormat::RGBA8UnormSrgb:
        return rhi::Format::RGBA8UnormSrgb;
    case TextureFormat::R16Float:
        return rhi::Format::R16Float;
    case TextureFormat::RG16Float:
        return rhi::Format::RG16Float;
    case TextureFormat::RGBA16Float:
        return rhi::Format::RGBA16Float;
    case TextureFormat::R32Float:
        return rhi::Format::R32Float;
    case TextureFormat::RG32Float:
        return rhi::Format::RG32Float;
    case TextureFormat::RGBA32Float:
        return rhi::Format::RGBA32Float;
    default:
        throw EngineError("Unknown texture format.");
    }
}

TextureRenderer::TextureRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}

TextureRenderer::~TextureRenderer()
{
    for (auto& weak : m_textures)
    {
        if (auto data = weak.lock())
        {
            data->views.clear();
            data->texture.setNull();
        }
    }
    for (auto& weak : m_samplers)
    {
        if (auto data = weak.lock())
        {
            data->sampler.setNull();
        }
    }
}

bool TextureRenderer::supports(TextureFormat format) const
{
    rhi::FormatSupport support{};
    check(m_device->getFormatSupport(textureFormat(format), &support), "Query texture format");
    return (support & rhi::FormatSupport::ShaderSample) != rhi::FormatSupport::None;
}

rhi::ITextureView* TextureRenderer::viewFor(TextureGpuData& data, TextureViewDesc desc)
{
    const auto key = std::pair(desc.firstMip, desc.mipCount);
    if (auto found = data.views.find(key); found != data.views.end())
    {
        return found->second;
    }
    rhi::TextureViewDesc view{};
    view.subresourceRange = {0, 1, desc.firstMip, desc.mipCount};
    rhi::ComPtr<rhi::ITextureView> result;
    if (desc.firstMip == 0 && desc.mipCount == data.texture->getDesc().mipCount)
    {
        check(data.texture->getDefaultView(result.writeRef()), "Get default texture view");
    }
    else
    {
        check(data.texture->createView(view, result.writeRef()), "Create texture mip view");
    }
    data.views.emplace(key, result);
    return result;
}

void TextureRenderer::prepareTexture(Texture& texture)
{
    if (!texture.isLoaded())
    {
        throw EngineError("Texture is not ready: " + texture.key() + " " + texture.error());
    }
    if (texture.m_gpu && texture.m_gpu->texture)
    {
        return;
    }
    const auto& source = texture.desc();
    const auto limit = m_device->getInfo().limits.maxTextureDimension2D;
    if (source.width > limit || source.height > limit)
    {
        throw EngineError("Texture exceeds device dimensions: " + texture.key());
    }
    if (!supports(source.format))
    {
        throw EngineError(
            "Texture format is not sampleable; browser fp32 requires float32-filterable: " + texture.key()
        );
    }
    const auto levels = texture.mipCount();
    rhi::TextureDesc desc{};
    desc.size = {source.width, source.height, 1};
    desc.format = textureFormat(source.format);
    desc.mipCount = levels;
    desc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource;
    desc.defaultState = rhi::ResourceState::ShaderResource;
    if (levels > 1)
    {
        rhi::FormatSupport support{};
        check(m_device->getFormatSupport(desc.format, &support), "Query mip attachment format");
        if ((support & rhi::FormatSupport::RenderTarget) == rhi::FormatSupport::None)
        {
            throw EngineError("Texture format cannot be used for GPU mip generation.");
        }
        desc.usage |= rhi::TextureUsage::RenderTarget;
    }
    auto data = std::make_shared<TextureGpuData>();
    check(m_device->createTexture(desc, nullptr, data->texture.writeRef()), "Allocate sampled texture");
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    check(m_queue->createCommandEncoder(encoder.writeRef()), "Create texture upload encoder");
    rhi::SubresourceData pixels{
        texture.m_pixels.data(),
        source.width * texturePixelSize(source.format),
        texture.m_pixels.size()
    };
    encoder->uploadTextureData(data->texture, {0, 1, 0, 1}, {0, 0, 0}, {source.width, source.height, 1}, &pixels, 1);
    rhi::ComPtr<rhi::ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish texture upload");
    check(m_queue->submit(commands), "Submit texture upload");
    if (levels > 1)
    {
        generateMips(*data, source, levels);
    }
    std::erase_if(
        m_textures,
        [](const auto& weak)
        {
            return weak.expired();
        }
    );
    m_textures.push_back(data);
    texture.m_gpu = std::move(data);
}

rhi::ITextureView* TextureRenderer::prepare(const TextureView& view)
{
    prepareTexture(*view.texture());
    return viewFor(*view.texture()->m_gpu, view.desc());
}

rhi::ISampler* TextureRenderer::prepare(Sampler& sampler)
{
    if (sampler.m_gpu && sampler.m_gpu->sampler)
    {
        return sampler.m_gpu->sampler;
    }
    const auto& source = sampler.desc();
    rhi::SamplerDesc desc{};
    desc.minFilter = source.minFilter == TextureFilter::Linear ? rhi::TextureFilteringMode::Linear
                                                               : rhi::TextureFilteringMode::Point;
    desc.magFilter = source.magFilter == TextureFilter::Linear ? rhi::TextureFilteringMode::Linear
                                                               : rhi::TextureFilteringMode::Point;
    desc.mipFilter = source.mipFilter == TextureFilter::Linear ? rhi::TextureFilteringMode::Linear
                                                               : rhi::TextureFilteringMode::Point;
    desc.addressU = source.addressU == TextureAddressMode::Repeat ? rhi::TextureAddressingMode::Wrap
                                                                  : rhi::TextureAddressingMode::ClampToEdge;
    desc.addressV = source.addressV == TextureAddressMode::Repeat ? rhi::TextureAddressingMode::Wrap
                                                                  : rhi::TextureAddressingMode::ClampToEdge;
    auto data = std::make_shared<SamplerGpuData>();
    check(m_device->createSampler(desc, data->sampler.writeRef()), "Create material sampler");
    std::erase_if(
        m_samplers,
        [](const auto& weak)
        {
            return weak.expired();
        }
    );
    m_samplers.push_back(data);
    sampler.m_gpu = std::move(data);
    return sampler.m_gpu->sampler;
}

rhi::IRenderPipeline* TextureRenderer::mipPipeline(rhi::Format format)
{
    if (auto found = m_pipelines.find(format); found != m_pipelines.end())
    {
        return found->second;
    }
    if (!m_mipProgram)
    {
        rhi::ComPtr<slang::IBlob> diagnostics;
        auto module =
            m_device->getSlangSession()
                ->loadModuleFromSourceString("ofg_mipmaps", "mipmaps.slang", mipmapsShader, diagnostics.writeRef());
        if (!module)
        {
            throw EngineError(
                "Compile mipmaps: " +
                std::string(diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "no diagnostics")
            );
        }
        rhi::ComPtr<slang::IEntryPoint> vertex, fragment;
        check(module->findEntryPointByName("vertexMain", vertex.writeRef()), "Find mip vertex entry");
        check(module->findEntryPointByName("fragmentMain", fragment.writeRef()), "Find mip fragment entry");
        slang::IComponentType* entries[]{vertex, fragment};
        rhi::ShaderProgramDesc desc{};
        desc.linkingStyle = rhi::LinkingStyle::SingleProgram;
        desc.slangGlobalScope = module;
        desc.slangEntryPoints = entries;
        desc.slangEntryPointCount = 2;
        check(m_device->createShaderProgram(desc, m_mipProgram.writeRef()), "Create mip program");
    }
    rhi::ColorTargetDesc target{};
    target.format = format;
    rhi::RenderPipelineDesc desc{};
    desc.program = m_mipProgram;
    desc.targets = &target;
    desc.targetCount = 1;
    desc.rasterizer.cullMode = rhi::CullMode::None;
    desc.depthStencil.depthTestEnable = false;
    desc.depthStencil.depthWriteEnable = false;
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    check(m_device->createRenderPipeline(desc, pipeline.writeRef()), "Create mip pipeline");
    m_pipelines.emplace(format, pipeline);
    return pipeline;
}

void TextureRenderer::generateMips(TextureGpuData& data, const TextureDesc& desc, uint32_t levels)
{
    auto pipeline = mipPipeline(textureFormat(desc.format));
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    check(m_queue->createCommandEncoder(encoder.writeRef()), "Create mip encoder");
    uint32_t width = desc.width, height = desc.height;
    for (uint32_t level = 1; level < levels; ++level)
    {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        rhi::RenderPassColorAttachment color{};
        color.view = viewFor(data, {level, 1});
        // WebGPU requires an explicit clear/load; the pinned backend maps DontCare to undefined.
        color.loadOp = rhi::LoadOp::Clear;
        rhi::RenderPassDesc passDesc{};
        passDesc.colorAttachments = &color;
        passDesc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(passDesc);
        auto root = pass->bindPipeline(pipeline);
        rhi::ShaderCursor cursor(root);
        check(cursor["sourceImage"].setBinding(viewFor(data, {level - 1, 1})), "Bind mip source");
        const uint32_t dimensions[]{width, height};
        const uint32_t alphaWeighted = desc.format == TextureFormat::RGBA8UnormSrgb;
        check(cursor["mip"]["destinationSize"].setData(dimensions, sizeof(dimensions)), "Bind mip size");
        check(cursor["mip"]["alphaWeighted"].setData(alphaWeighted), "Bind mip color policy");
        rhi::RenderState state{};
        state.viewports[0] = rhi::Viewport::fromSize(width, height);
        state.viewportCount = 1;
        state.scissorRects[0] = rhi::ScissorRect::fromSize(width, height);
        state.scissorRectCount = 1;
        pass->setRenderState(state);
        rhi::DrawArguments draw{};
        draw.vertexCount = 3;
        pass->draw(draw);
        pass->end();
    }
    rhi::ComPtr<rhi::ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish mip generation");
    check(m_queue->submit(commands), "Submit mip generation");
}
} // namespace ofg
