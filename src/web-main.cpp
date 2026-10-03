// Browser host: WebGPU device/canvas setup and callback-driven presentation of the shared scene and selectable
// checkerboard.
#include "checkerboard.h"
#include "game.h"
#include "resources/resources.h"
#include "render/texture-renderer.h"
#include "render/graphics.h"
#include "render/present.h"
#include "lab/scene-fixture.h"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <memory>

using namespace rhi;

struct BrowserApp
{
    ComPtr<IDevice> device;
    ComPtr<ISurface> surface;
    ComPtr<ICommandQueue> queue;
    ComPtr<IRenderPipeline> pipeline;
    ComPtr<IRenderPipeline> presentPipeline;
    ComPtr<ITexture> sceneTarget;
    std::shared_ptr<ofg::Texture> texture;
    double previousTime{0};
    // Releases application and graphics state when the callback stops; browser owns submitted GPU work.
    ~BrowserApp()
    {
        ofg::Game::shutdown();
        ofg::Graphics::shutdown();
    }
};

// Reports startup/render failure to both the visible page and the browser automation diagnostics.
static void reportFailure(Result result)
{
    std::fprintf(stderr, "OFG WebGPU failed (result 0x%08X).\n", unsigned(result));
    EM_ASM({ Module.setStatus('WebGPU initialization or rendering failed. See the console.', true); });
}

// Initializes the shared renderer or diagnostic; RHI yields to browser promises during device initialization.
static Result initializeBrowser(BrowserApp& app)
{
    DeviceDesc desc = {};
    desc.deviceType = DeviceType::WGPU;
    desc.enableValidation = true;
    SLANG_RETURN_ON_FAIL(getRHI()->createDevice(desc, app.device.writeRef()));
    SLANG_RETURN_ON_FAIL(app.device->getQueue(QueueType::Graphics, app.queue.writeRef()));
    SLANG_RETURN_ON_FAIL(app.device->createSurface(WindowHandle::fromWGPUCanvas("#canvas"), app.surface.writeRef()));
    if (EM_ASM_INT({ return Module.checkerboard ? 1 : 0; }))
    {
        SLANG_RETURN_ON_FAIL(
            createCheckerboardPipeline(app.device, app.surface->getInfo().preferredFormat, app.pipeline.writeRef())
        );
    }
    else
    {
        SLANG_RETURN_ON_FAIL(createPresentationPipeline(
            app.device,
            app.surface->getInfo().preferredFormat,
            app.presentPipeline.writeRef()
        ));
        ofg::Graphics::initialize(app.device, app.queue);
        ofg::TextureRenderer formats(app.device, app.queue);
        const bool fp32 = formats.supports(ofg::TextureFormat::RGBA32Float);
        EM_ASM({ Module.fp32Supported = Boolean($0); }, fp32);
        const int floatBits = EM_ASM_INT({ return Module.floatBits; });
        app.texture =
            floatBits
                ? ofg::createFloatFixtureTexture(floatBits == 32)
                : ofg::Resources::loadResourceAsync<ofg::Texture>(emscripten_run_script_string("Module.texturePath"));
        ofg::Game::initialize(ofg::createSceneFixture(app.texture));
    }
    app.previousTime = emscripten_get_now();
    // Browsers may omit the adapter description; do not infer an identity from the native device.
    const char* adapter = app.device->getInfo().adapterName;
    if (!adapter || !adapter[0])
    {
        adapter = "browser-selected adapter";
    }
    std::printf("OFG initialized WebGPU on %s.\n", adapter);
    EM_ASM({ Module.adapter = UTF8ToString($0); }, adapter);
    return SLANG_OK;
}

// Resizes to physical display pixels and submits a frame without blocking the browser's event loop.
static Result drawBrowserFrame(BrowserApp& app)
{
    const double now = emscripten_get_now();
    const float deltaSeconds = float((now - app.previousTime) / 1000.0);
    app.previousTime = now;
    double cssWidth = 0;
    double cssHeight = 0;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS)
    {
        return SLANG_FAIL;
    }
    const double pixelRatio = emscripten_get_device_pixel_ratio();
    const int width = int(cssWidth * pixelRatio);
    const int height = int(cssHeight * pixelRatio);
    if (width <= 0 || height <= 0)
    {
        if (!app.pipeline)
        {
            ofg::Game::frame(deltaSeconds, nullptr);
        }
        return SLANG_OK;
    }

    const auto config = app.surface->getConfig();
    if (!config || config->width != uint32_t(width) || config->height != uint32_t(height))
    {
        emscripten_set_canvas_element_size("#canvas", width, height);
        SurfaceConfig resized = {};
        resized.width = width;
        resized.height = height;
        resized.format = app.surface->getInfo().preferredFormat;
        SLANG_RETURN_ON_FAIL(app.surface->configure(resized));
        if (!app.pipeline)
        {
            TextureDesc target{};
            target.type = TextureType::Texture2D;
            target.size = {uint32_t(width), uint32_t(height), 1};
            target.format = resized.format;
            target.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
            target.defaultState = ResourceState::RenderTarget;
            SLANG_RETURN_ON_FAIL(app.device->createTexture(target, nullptr, app.sceneTarget.writeRef()));
        }
    }

    // Uploads and uniform staging may yield. Canvas textures expire across event-loop turns, so acquire only
    // after rendering into host-owned storage; the final image-load shader has no uniforms/maps that can yield.
    if (!app.pipeline)
    {
        if (EM_ASM_INT({ return Module.cancelTexture ? 1 : 0; }))
        {
            ofg::Game::setScene(std::make_unique<ofg::Scene>());
            app.texture.reset();
            EM_ASM({
                Module.cancelTexture = false;
                Module.textureCancelled = true;
            });
        }
        ofg::Game::frame(deltaSeconds, app.sceneTarget);
        const bool ready = app.texture && app.texture->isLoaded();
        EM_ASM({ Module.textureReady = Boolean($0); }, ready);
    }
    ComPtr<ITexture> image;
    SLANG_RETURN_ON_FAIL(app.surface->acquireNextImage(image.writeRef()));
    if (image)
    {
        if (app.pipeline)
        {
            SLANG_RETURN_ON_FAIL(drawCheckerboard(app.queue, app.pipeline, image));
        }
        else
        {
            SLANG_RETURN_ON_FAIL(drawPresentation(app.queue, app.presentPipeline, app.sceneTarget, image));
        }
        SLANG_RETURN_ON_FAIL(app.surface->present());
        EM_ASM({ Module.frameRendered(); });
    }
    return SLANG_OK;
}

// The main loop owns the app until failure or page teardown; cancel before releasing callback state.
static void browserFrame(void* context)
{
    auto app = static_cast<BrowserApp*>(context);
    Result result = SLANG_FAIL;
    try
    {
        result = drawBrowserFrame(*app);
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "OFG scene: %s\n", error.what());
    }
    if (SLANG_FAILED(result))
    {
        emscripten_cancel_main_loop();
        reportFailure(result);
        delete app;
    }
}

// Initializes once, then transfers lifetime to the requestAnimationFrame-driven Emscripten loop.
int main()
{
    auto app = std::make_unique<BrowserApp>();
    Result result = SLANG_FAIL;
    try
    {
        result = initializeBrowser(*app);
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "OFG initialization: %s\n", error.what());
    }
    if (SLANG_FAILED(result))
    {
        reportFailure(result);
        return 1;
    }
    emscripten_set_main_loop_arg(browserFrame, app.release(), 0, false);
    return 0;
}
