// Final image presentation through a resource-only shader; no scene/material API or asynchronous staging.
#pragma once

#include <slang-rhi.h>

// Creates a resource-only pipeline; encodeLinear applies sRGB transfer for linear sources on UNORM targets.
rhi::Result createPresentationPipeline(
    rhi::IDevice* device,
    rhi::Format format,
    rhi::IRenderPipeline** outPipeline,
    bool encodeLinear = false
);

// Submits an equal-size image presentation. Source requires ShaderResource usage; caller acquires/presents target.
rhi::Result drawPresentation(
    rhi::ICommandQueue* queue,
    rhi::IRenderPipeline* pipeline,
    rhi::ITexture* source,
    rhi::ITexture* target
);
