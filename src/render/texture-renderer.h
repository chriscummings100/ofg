// Graphics-owned texture preparation and mip pipelines; assets own allocations, this helper tracks them weakly.
#pragma once
#include "resources/texture.h"
#include "resources/sampler.h"
#include "render/resource-gpu-data.h"
#include <map>
#include <vector>

namespace ofg {
class TextureRenderer
{
public:
    // Observes the graphics owner's device and queue, which must outlive this helper.
    TextureRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue);
    TextureRenderer(const TextureRenderer&) = delete;
    TextureRenderer& operator=(const TextureRenderer&) = delete;
    // Invalidates surviving asset handles; caller honors the host's queue shutdown contract.
    ~TextureRenderer();
    // Uploads and generates once, returning a cached view. Throws for unready/unsupported images.
    rhi::ITextureView* prepare(const TextureView& view);
    // Prepares one shared immutable sampler.
    rhi::ISampler* prepare(Sampler& sampler);
    // Reports support for the selected format including the pinned WebGPU fp32 binding requirement.
    bool supports(TextureFormat format) const;

private:
    // Allocates a texture, uploads base pixels and queues all mip passes before publishing ownership.
    void prepareTexture(Texture& texture);
    // Returns a cached same-format view of the requested range.
    rhi::ITextureView* viewFor(TextureGpuData& data, TextureViewDesc desc);
    // Compiles/caches a reduction pipeline for this destination attachment format.
    rhi::IRenderPipeline* mipPipeline(rhi::Format format);
    // Queues reduction passes without readbacks or CPU waits.
    void generateMips(TextureGpuData& data, const TextureDesc& desc, uint32_t levels);
    rhi::IDevice* m_device;
    rhi::ICommandQueue* m_queue;
    rhi::ComPtr<rhi::IShaderProgram> m_mipProgram;
    std::map<rhi::Format, rhi::ComPtr<rhi::IRenderPipeline>> m_pipelines;
    std::vector<std::weak_ptr<TextureGpuData>> m_textures;
    std::vector<std::weak_ptr<SamplerGpuData>> m_samplers;
};
// Maps the supported CPU formats without exposing RHI in resource headers.
rhi::Format textureFormat(TextureFormat format);
} // namespace ofg
