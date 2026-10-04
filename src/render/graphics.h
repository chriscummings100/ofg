// Application-thread graphics service: explicit device lifecycle, indexed draws and weak shader pipeline caching.
#pragma once

#include "render/draw-list.h"
#include <slang-rhi.h>

namespace ofg {
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

private:
    // Uploads all cube faces/mips and the matched lookup atomically; weakly tracks shutdown handles.
    static void prepareEnvironment(Environment& environment);
    // Uploads immutable geometry once; publishes only after both buffer creations succeed.
    static void prepareMesh(Mesh& mesh);
    // Compiles source/entry points once and preserves compiler diagnostics on failure.
    static void prepareShader(Shader& shader);
};
} // namespace ofg
