// Direct Slang RHI renderer for one ImGui context; callers own render targets and external image textures.
#pragma once

#include <slang-rhi.h>
#include <imgui.h>
#include <unordered_map>

namespace ofg {
class ImGuiRenderer
{
public:
    // Creates the display-linear RGBA16F pipeline and advertises texture/vertex-offset support to this context.
    ImGuiRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Releases font ownership before the caller destroys the current ImGui context.
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    // Clears and draws into an RGBA16F target. External texture IDs borrow ITexture pointers until submission.
    void render(ImDrawData& data, rhi::ITexture* target);
    // Creates a sampled RGBA16F attachment, including readback usage for integration checks.
    rhi::ComPtr<rhi::ITexture> createTarget(uint32_t width, uint32_t height);
    // Converts a borrowed texture to the opaque image identifier expected by ImGui.
    static ImTextureID textureId(rhi::ITexture* texture);

private:
    // Publishes immutable full-atlas snapshots on create/update; submitted draws retain preceding versions.
    void updateTextures(ImDrawData& data);
    rhi::ComPtr<rhi::IDevice> m_device;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IShaderProgram> m_program;
    rhi::ComPtr<rhi::IRenderPipeline> m_pipeline;
    rhi::ComPtr<rhi::ISampler> m_sampler;
    std::unordered_map<ImTextureData*, rhi::ComPtr<rhi::ITexture>> m_textures;
};
} // namespace ofg
