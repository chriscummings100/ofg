// Private RHI allocations owned by CPU resource descriptions and weakly tracked for Graphics shutdown.
#pragma once

#include <slang-rhi.h>

namespace ofg {
struct MeshGpuData
{
    rhi::ComPtr<rhi::IBuffer> vertices;
    rhi::ComPtr<rhi::IBuffer> indices;
};

struct ShaderGpuData
{
    rhi::ComPtr<rhi::IShaderProgram> program;
};
} // namespace ofg
