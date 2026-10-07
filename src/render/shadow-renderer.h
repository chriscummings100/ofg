// GPU cascade depth passes consuming prepared geometry; no scene ownership or duplicate resource upload.
#pragma once
#include "render/draw-list.h"
#include <slang-rhi.h>
namespace ofg {
struct ShadowDraw
{
    const DrawItem* item = nullptr;
    rhi::IBuffer* vertices = nullptr;
    rhi::IBuffer* indices = nullptr;
    rhi::ITextureView* alphaTexture = nullptr;
    rhi::ISampler* alphaSampler = nullptr;
};
class ShadowRenderer
{
public:
    // Retains device/layout and creates the small always-valid fallback depth binding.
    ShadowRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue, rhi::IInputLayout* inputLayout);
    // Clears surviving materials' cached objects before this device-owned renderer is released.
    ~ShadowRenderer();
    // Fits and renders the four layers, retaining submitted resources through RHI commands.
    void render(const DrawList& frame, std::span<const ShadowDraw> draws);
    // Returns the sampled depth array, including when shadows are disabled.
    rhi::ITexture* texture() const { return m_depth; }
    // Returns the depth-comparison sampler used by PCF.
    rhi::ISampler* sampler() const { return m_sampler; }
    // Returns the current cascade geometry for frame binding and diagnostics.
    const std::array<ShadowCascade, 4>& cascades() const { return m_cascades; }

private:
    // Prepares and freezes material properties once per edit, including shadow-only casters.
    void prepareMaterial(const ShadowDraw& draw);
    // Allocates a complete four-layer depth resource before replacing previous storage.
    void resize(uint32_t resolution);
    // Compiles the masked/opaque and culling/winding variant required by this draw.
    rhi::IRenderPipeline* pipeline(bool texture, bool doubleSided, bool mirrored);
    rhi::ComPtr<rhi::IDevice> m_device;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IInputLayout> m_layout;
    rhi::ComPtr<rhi::ITexture> m_depth;
    rhi::ComPtr<rhi::ISampler> m_sampler;
    rhi::ComPtr<slang::IModule> m_passModule;
    slang::TypeReflection* m_passType = nullptr;
    std::vector<std::weak_ptr<ShadowMaterialGpuData>> m_materials;
    std::array<rhi::ComPtr<rhi::IShaderProgram>, 2> m_programs;
    std::array<rhi::ComPtr<rhi::IRenderPipeline>, 8> m_pipelines;
    std::array<ShadowCascade, 4> m_cascades{};
};
} // namespace ofg
