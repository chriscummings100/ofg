// Shared checkerboard rendering; owns no window, event loop or platform-specific resources.
#pragma once

#include <slang-rhi.h>

// Compiles the embedded Slang shader and creates a pipeline matching the destination's color format.
rhi::Result createCheckerboardPipeline(rhi::IDevice* device, rhi::Format format, rhi::IRenderPipeline** outPipeline);

// Submits one full-target checkerboard draw. The caller owns presentation and completion/teardown synchronization.
rhi::Result drawCheckerboard(rhi::ICommandQueue* queue, rhi::IRenderPipeline* pipeline, rhi::ITexture* target);
