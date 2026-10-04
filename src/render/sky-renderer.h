// Raster atmosphere lookups and sky capture owned by Graphics, with no dependency on ImGui.
#pragma once
#include "render/draw-list.h"
#include <slang-rhi.h>
namespace ofg {
class SkyRenderer
{
public:
    // Retains the application device and graphics queue; GPU work is initialized lazily.
    SkyRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Updates sky and camera aerial perspective for a frozen outdoor frame.
    void update(const DrawList& frame);
    // Renders the exposed sky into the HDR target before surface rendering.
    void drawBackground(const DrawList& frame, rhi::ITexture* target);
    // Freezes the current sky-view lookup after update(); the caller freezes matching CPU inputs.
    void beginCapture();
    // Captures one disk-free face using the frozen lookup and matching inputs; returns the shared capture cube.
    rhi::ITexture* captureFace(const DrawList& snapshot, uint32_t face);
    // Returns the camera-distance lookup, valid after update().
    rhi::ITexture* aerialPerspective() const { return m_aerial; }

private:
    // Compiles the fixed pass variants and allocates reusable lookup storage.
    void initialize();
    // Creates one independent reflected parameter object for a pass/slice.
    rhi::ComPtr<rhi::IShaderObject> bindings(uint32_t pass, const DrawList& frame, uint32_t slice);
    rhi::ComPtr<rhi::IDevice> m_device;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    std::array<rhi::ComPtr<rhi::IShaderProgram>, 6> m_programs;
    std::array<rhi::ComPtr<rhi::IRenderPipeline>, 6> m_pipelines;
    rhi::ComPtr<rhi::ITexture> m_transmission, m_multiple, m_skyView, m_aerial, m_capture, m_captureSkyView;
    rhi::ComPtr<rhi::ISampler> m_sampler;
    std::array<float, 13> m_skySnapshot{};
    std::array<float, 20> m_aerialSnapshot{};
    std::array<float, 4> m_atmosphere{-1, -1, -1, -1};
};
} // namespace ofg
