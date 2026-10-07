// Private RHI allocations owned by CPU resource descriptions and weakly tracked for Graphics shutdown.
#pragma once

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <map>
#include <array>

namespace ofg {
struct EnvironmentGpuData
{
    rhi::ComPtr<rhi::ITexture> cubes;
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
    size_t uploadedVertices = 0, uploadedIndices = 0;
    bool incrementalUpload = false;
    rhi::ComPtr<rhi::IBuffer> vertices;
    rhi::ComPtr<rhi::IBuffer> indices;
    rhi::ComPtr<rhi::IBuffer> influences;
};

// Cursors observe children retained by root; clear the whole record when releasing its device.
struct MatrixBinding
{
    rhi::ShaderCursor cursor;
    bool rowMajor = false;
};
struct MaterialGpuData
{
    rhi::ComPtr<rhi::IShaderObject> root, unboundScene;
    rhi::ShaderCursor scene, orientation;
    MatrixBinding clipFromLocal, worldFromLocal, normalFromLocal;
};

struct ShaderGpuData
{
    rhi::ComPtr<rhi::IShaderProgram> program;
};
} // namespace ofg
