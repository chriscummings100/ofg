// Small shared operations for raster-generated lighting textures; owns no global state.
#pragma once
#include "math/mat.h"
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
namespace ofg {
// Reports RHI failures with the failed lighting operation.
void checkLighting(rhi::Result result, const char* operation);
// Compiles a fullscreen or mesh vertex/fragment pair, preserving Slang diagnostics.
rhi::ComPtr<rhi::IShaderProgram> lightingProgram(
    rhi::IDevice* device,
    const char* source,
    const char* name,
    const char* fragment = "fragmentMain"
);
// Creates a cull-free fullscreen pipeline for the given output format.
rhi::ComPtr<rhi::IRenderPipeline> lightingPipeline(
    rhi::IDevice* device,
    rhi::IShaderProgram* program,
    rhi::Format format = rhi::Format::RGBA16Float
);
// Creates one 2D array-layer/mip attachment view of a lighting allocation.
rhi::ComPtr<rhi::ITextureView> lightingView(rhi::ITexture* texture, uint32_t layer, uint32_t mip = 0);
// Packs a matrix according to Slang reflection instead of assuming a compiler matrix layout.
void setLightingMatrix(rhi::ShaderCursor cursor, const math::Mat4& value);
// Records a cleared fullscreen lighting pass into an existing encoder; bindings are retained by the pass.
void drawLightingPass(
    rhi::ICommandEncoder* encoder,
    rhi::IRenderPipeline* pipeline,
    rhi::IShaderObject* root,
    rhi::ITextureView* target,
    uint32_t width,
    uint32_t height
);
// Finishes and submits an encoder; no CPU wait is introduced.
void submitLighting(rhi::ICommandQueue* queue, rhi::ICommandEncoder* encoder);
} // namespace ofg
