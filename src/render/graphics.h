// Application-thread graphics service: explicit device lifecycle, indexed draws and weak shader pipeline caching.
#pragma once

#include "render/draw-list.h"
#include <slang-rhi.h>

namespace ofg {
struct OutdoorDiagnostics
{
    bool ready = false;
    double environmentAgeSeconds = 0;
    float environmentBlend = 1;
    uint32_t environmentSteps = 0, environmentPasses = 0, environmentPublications = 0;
    uint32_t shadowResolution = 0;
    std::array<float, 4> cascadeDistances{};
};
class Graphics
{
public:
    Graphics() = delete;
    // Retains one device/graphics queue; rejects null handles and double initialization.
    static void initialize(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Submits diagnostic draws or HDR opaque/output/unlit/sorted-alpha passes. Throws on invalid data/RHI failure.
    static void render(const DrawList& list, rhi::ITexture* colorTarget);
    // Releases pipelines/depth and all surviving asset GPU handles; native caller drains the queue first.
    static void shutdown() noexcept;
    // Returns successful pipeline creations in this lifecycle for reuse diagnostics.
    static size_t pipelineCreationCount() noexcept;
    // Returns retained pipeline entries, pruning expired shader owners during rendering.
    static size_t pipelineCount() noexcept;

    // Returns the latest outdoor generation and cascade state without synchronizing with the GPU.
    static OutdoorDiagnostics outdoorDiagnostics() noexcept;
    // Prepares immutable geometry in bounded upload chunks before terrain publishes it. Consumes byteAllowance.
    // Returns true once uploads are ordered on the graphics queue; failures preserve owned partial allocations.
    static bool prepareStreamingMesh(Mesh& mesh, size_t& byteAllowance);
    // Returns currently allocated streaming buffer bytes, including buffers whose upload is incomplete.
    static size_t streamingMeshGpuBytes(const Mesh& mesh) noexcept;

private:
    // Uploads immutable geometry once; publishes only after both buffer creations succeed.
    static void prepareMesh(Mesh& mesh);
    // Prepares immutable material data and resolves instance fields once, publishing only on success.
    static void prepareMaterial(Material& material);
    // Compiles source/entry points once and preserves compiler diagnostics on failure.
    static void prepareShader(Shader& shader);
};
} // namespace ofg
