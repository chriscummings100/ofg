// Final image presentation through a resource-only shader; no scene/material API or asynchronous staging.
#pragma once

#include <slang-rhi.h>

// Creates a fullscreen image-load pipeline for the destination format; has no uniform storage that can yield.
rhi::Result createPresentationPipeline(rhi::IDevice* device, rhi::Format format, rhi::IRenderPipeline** outPipeline);

// Submits an equal-size image presentation. Source requires ShaderResource usage; caller acquires/presents target.
rhi::Result drawPresentation(
    rhi::ICommandQueue* queue,
    rhi::IRenderPipeline* pipeline,
    rhi::ITexture* source,
    rhi::ITexture* target
);
