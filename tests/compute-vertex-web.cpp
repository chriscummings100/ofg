// Minimal Emscripten host for the same RHI proof as the native GPU test; no application fixture is replaced.
#include "compute-vertex-proof.h"
#include "render/present.h"
#include <emscripten.h>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
struct ProofApp
{
    rhi::ComPtr<rhi::IDevice> device;
    rhi::ComPtr<rhi::ICommandQueue> queue;
    rhi::ComPtr<rhi::ISurface> surface;
    rhi::ComPtr<rhi::ITexture> target;
    rhi::ComPtr<rhi::IRenderPipeline> present;
    std::unique_ptr<ofg::ComputeVertexProof> proof;
};

// Converts backend failures into console errors consumed by the smoke check.
void check(rhi::Result result)
{
    if (SLANG_FAILED(result))
    {
        throw std::runtime_error("Compute browser proof RHI failure: " + std::to_string(result));
    }
}

// Uploads/dispatches into persistent storage before acquiring the short-lived browser canvas image.
void frame(void* pointer)
{
    auto& app = *static_cast<ProofApp*>(pointer);
    try
    {
        const float offset = EM_ASM_DOUBLE({ return Module.proofOffset || 0; });
        app.proof->draw(app.target, offset);
        rhi::ComPtr<rhi::ITexture> canvas;
        check(app.surface->acquireNextImage(canvas.writeRef()));
        check(drawPresentation(app.queue, app.present, app.target, canvas));
        check(app.surface->present());
        EM_ASM({ Module.proofFrames = (Module.proofFrames || 0) + 1; });
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        emscripten_cancel_main_loop();
    }
}
} // namespace

// Initializes the pinned RHI WebGPU backend and leaves lifetime with the browser callback loop.
int main()
{
    try
    {
        auto app = std::make_unique<ProofApp>();
        rhi::DeviceDesc device{};
        device.deviceType = rhi::DeviceType::WGPU;
        device.enableValidation = true;
        check(rhi::getRHI()->createDevice(device, app->device.writeRef()));
        check(app->device->getQueue(rhi::QueueType::Graphics, app->queue.writeRef()));
        check(app->device->createSurface(rhi::WindowHandle::fromWGPUCanvas("#canvas"), app->surface.writeRef()));
        rhi::SurfaceConfig config{};
        config.width = config.height = 256;
        config.format = app->surface->getInfo().preferredFormat;
        check(app->surface->configure(config));
        rhi::TextureDesc target{};
        target.size = {256, 256, 1};
        target.format = config.format;
        target.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        target.defaultState = rhi::ResourceState::RenderTarget;
        check(app->device->createTexture(target, nullptr, app->target.writeRef()));
        check(createPresentationPipeline(app->device, config.format, app->present.writeRef()));
        app->proof = std::make_unique<ofg::ComputeVertexProof>(app->device, app->queue, config.format);
        emscripten_set_main_loop_arg(frame, app.release(), 0, false);
        return 0;
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
