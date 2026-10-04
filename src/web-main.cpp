// Browser host: WebGPU device/canvas setup and callback-driven presentation of the shared scene and selectable
// checkerboard.
#include "checkerboard.h"
#include "game.h"
#include "resources/resources.h"
#include "render/texture-renderer.h"
#include "render/graphics.h"
#include "render/present.h"
#include "lab/scene-fixture.h"
#include "lab/pbr-fixture.h"
#include "lab/model-fixture.h"
#include "lab/fly-camera.h"
#include "ui/workspace.h"
#include "ui/browser-input.h"

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
    bool pbr = false;
    std::unique_ptr<ofg::ModelFixture> model;
    ofg::FlyCamera camera;
    std::unique_ptr<ofg::Workspace> workspace;
    std::unique_ptr<ofg::BrowserInput> input;
    // Releases application and graphics state when the callback stops; browser owns submitted GPU work.
    ~BrowserApp()
    {
        input.reset();
        workspace.reset();
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
            app.presentPipeline.writeRef(),
            EM_ASM_INT({ return Module.ui ? 1 : 0; }) != 0
        ));
        ofg::Graphics::initialize(app.device, app.queue);
        ofg::TextureRenderer formats(app.device, app.queue);
        const bool fp32 = formats.supports(ofg::TextureFormat::RGBA32Float);
        EM_ASM({ Module.fp32Supported = Boolean($0); }, fp32);
        app.pbr = EM_ASM_INT({ return Module.pbr ? 1 : 0; });
        if (EM_ASM_INT({ return Module.model ? 1 : 0; }))
        {
            ofg::Game::initialize(ofg::createModelFixtureScene());
            app.model = std::make_unique<ofg::ModelFixture>(emscripten_run_script_string("Module.modelPath"));
        }
        else if (app.pbr)
        {
            ofg::Game::initialize(ofg::createPbrFixture(EM_ASM_INT({ return Module.maximumLayout ? 1 : 0; })));
        }
        else
        {
            const int floatBits = EM_ASM_INT({ return Module.floatBits; });
            app.texture = floatBits ? ofg::createFloatFixtureTexture(floatBits == 32)
                                    : ofg::Resources::loadResourceAsync<ofg::Texture>(
                                          emscripten_run_script_string("Module.texturePath")
                                      );
            ofg::Game::initialize(ofg::createSceneFixture(app.texture));
        }
    }
    if (!app.pipeline && EM_ASM_INT({ return Module.ui ? 1 : 0; }))
    {
        app.workspace = std::make_unique<ofg::Workspace>(app.device, app.queue);
        app.workspace->loadLayout(emscripten_run_script_string("Module.loadUiLayout()"));
        app.input = std::make_unique<ofg::BrowserInput>(*app.workspace);
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
        if (!app.pipeline && !app.workspace)
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
        if (app.model && EM_ASM_INT({ return Module.cancelModel ? 1 : 0; }))
        {
            app.model.reset();
            ofg::Game::setScene(ofg::createModelFixtureScene());
            EM_ASM({
                Module.cancelModel = false;
                Module.modelCancelled = true;
            });
        }
        if (app.model)
        {
            app.model->update(ofg::Game::scene());
            const auto& status = app.model->status();
            EM_ASM({ Module.modelStatus = UTF8ToString($0); }, status.c_str());
        }
        if (app.workspace)
        {
            auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(float(cssWidth), float(cssHeight));
            io.DisplayFramebufferScale = ImVec2(float(width / cssWidth), float(height / cssHeight));
            app.workspace->begin(ofg::Game::scene(), deltaSeconds);
        }
        if (app.pbr || app.model)
        {
            auto& scene = ofg::Game::scene();
            ofg::FlyCameraInput input;
            if (app.input)
            {
                input = app.input->cameraInput(scene);
            }
            else
            {
                float values[8]{};
                EM_ASM({ Module.readFlyInput($0); }, values);
                input.movement = {values[0], values[1], values[2]};
                input.lookPixels = {values[3], values[4]};
                input.fast = values[5] != 0;
                input.reset = values[6] != 0;
                input.closeup = values[7] != 0;
                scene.lighting.debugView = EM_ASM_INT({ return Module.debugView; });
            }
            if (app.model && input.reset)
            {
                input.closeup = true;
            }
            app.camera.update(*scene.activeCamera()->entity(), input, deltaSeconds);
            const auto p = scene.activeCamera()->entity()->localTransform().position;
            EM_ASM({ Module.cameraPosition = Array($0, $1, $2); }, p.x, p.y, p.z);
        }
        ofg::Game::frame(deltaSeconds, app.workspace ? app.workspace->sceneTarget() : app.sceneTarget.get());
        if (app.workspace)
        {
            app.sceneTarget = app.workspace->finish();
            if (ImGui::GetIO().WantSaveIniSettings)
            {
                const auto settings = app.workspace->saveLayout();
                EM_ASM({ Module.saveUiLayout(UTF8ToString($0)); }, settings.c_str());
            }
            auto rect = app.workspace->sceneRectangle();
            auto selected = app.workspace->selection();
            EM_ASM(
                { Module.uiFrame($0, $1, $2, $3, $4, $5, $6); },
                rect.x,
                rect.y,
                rect.z,
                rect.w,
                ofg::Game::scene().lighting.exposure,
                ofg::Game::scene().lighting.debugView,
                selected ? int(selected->id()) : -1
            );
        }
        const bool ready = app.pbr || (app.model && app.model->ready()) || (app.texture && app.texture->isLoaded());
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
