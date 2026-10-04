// Private RHI allocations owned by CPU resource descriptions and weakly tracked for Graphics shutdown.
#pragma once

#include <slang-rhi.h>
#include <map>
#include <array>

namespace ofg {
struct EnvironmentGpuData
{
    std::array<rhi::ComPtr<rhi::ITexture>, 3> cubes;
    rhi::ComPtr<rhi::ITexture> lookup;
};
struct TextureGpuData
{
    rhi::ComPtr<rhi::ITexture> texture;
    std::map<std::pair<uint32_t, uint32_t>, rhi::ComPtr<rhi::ITextureView>> views;
};
struct SamplerGpuData
{
    rhi::ComPtr<rhi::ISampler> sampler;
};
struct MeshGpuData
{
    rhi::ComPtr<rhi::IBuffer> vertices;
    rhi::ComPtr<rhi::IBuffer> indices;
    rhi::ComPtr<rhi::IBuffer> influences;
};

struct ShaderGpuData
{
    rhi::ComPtr<rhi::IShaderProgram> program;
};
} // namespace ofg
