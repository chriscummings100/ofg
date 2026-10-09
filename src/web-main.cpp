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
#include "lab/outdoor-fixture.h"
#include "lab/terrain-laboratory.h"
#include "lab/model-fixture.h"
#include "lab/character-fixture.h"
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
    std::unique_ptr<ofg::CharacterFixture> character;
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
        if (EM_ASM_INT({ return Module.terrain ? 1 : 0; }))
        {
            ofg::Game::initialize(ofg::createTerrainScene());
            ofg::TerrainLaunchSettings source;
            source.serviceUrl = emscripten_run_script_string(
                "(function(){const v=new URLSearchParams(location.search).get('terrainService');return v?new "
                "URL(v,location.origin).href:'';})()"
            );
            source.island =
                emscripten_run_script_string("new URLSearchParams(location.search).get('island') || 'demo'");
            source.skipCache = emscripten_run_script_int("new URLSearchParams(location.search).has('skipCache')") != 0;
            ofg::Game::setTerrain(std::make_unique<ofg::TerrainLaboratory>(app.device, app.queue, std::move(source)));
        }
        else if (EM_ASM_INT({ return Module.character ? 1 : 0; }))
        {
            ofg::Game::initialize(ofg::createCharacterFixtureScene());
            app.character =
                std::make_unique<ofg::CharacterFixture>("assets/models/character", EM_ASM_INT({
                                                                                       return Module.characterPair ? 1
                                                                                                                   : 0;
                                                                                   }) != 0);
        }
        else if (EM_ASM_INT({ return Module.model ? 1 : 0; }))
        {
            ofg::Game::initialize(ofg::createModelFixtureScene());
            app.model = std::make_unique<ofg::ModelFixture>(emscripten_run_script_string("Module.modelPath"));
        }
        else if (EM_ASM_INT({ return Module.outdoor ? 1 : 0; }))
        {
            auto outdoor = ofg::createOutdoorFixture();
            outdoor->lighting.outdoor->dayCycle.timeHours = EM_ASM_DOUBLE({ return Module.outdoorHours; });
            outdoor->lighting.outdoor->clouds.coverage = float(EM_ASM_DOUBLE({ return Module.outdoorClouds; }));
            ofg::Game::initialize(std::move(outdoor));
            app.camera.setResetPositions({0, 3, -15}, {0, 2, -5});
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
        if (app.character && EM_ASM_INT({ return Module.cancelCharacter ? 1 : 0; }))
        {
            app.character.reset();
            ofg::Game::setScene(ofg::createCharacterFixtureScene());
            EM_ASM({
                Module.cancelCharacter = false;
                Module.characterCancelled = true;
            });
        }
        if (app.character)
        {
            app.character->update(ofg::Game::scene());
            EM_ASM({ Module.characterReady = Boolean($0); }, app.character->ready());
        }
        if (app.workspace)
        {
            auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(float(cssWidth), float(cssHeight));
            io.DisplayFramebufferScale = ImVec2(float(width / cssWidth), float(height / cssHeight));
            app.workspace->begin(ofg::Game::scene(), deltaSeconds);
        }
        if (app.pbr || app.model || app.character)
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
            if (ofg::Game::terrain() && (input.reset || input.closeup))
            {
                if (input.closeup)
                {
                    ofg::Game::terrain()->teleportToSurface({{}, {0, 0, -300}});
                }
                else
                {
                    ofg::Game::terrain()->teleport({{}, {0, 180, -300}});
                }
            }
            app.camera
                .update(*scene.activeCamera()->entity(), input, deltaSeconds * (ofg::Game::terrain() ? 32.f : 1.f));
            if (app.character && (input.reset || input.closeup))
            {
                ofg::frameCharacter(*scene.activeCamera()->entity());
            }
            const auto p = scene.activeCamera()->entity()->localTransform().position;
            EM_ASM({ Module.cameraPosition = Array($0, $1, $2); }, p.x, p.y, p.z);
        }
        ofg::Game::frame(deltaSeconds, app.workspace ? app.workspace->sceneTarget() : app.sceneTarget.get());
        if (auto* terrain = ofg::Game::terrain())
        {
            const auto stats = terrain->diagnostics();
            const auto cache = terrain->cacheCounts();
            if (const auto* generation = terrain->generation())
                EM_ASM(
                    {
                        Module.terrainGeneration = ({
                            displayed : UTF8ToString($0),
                            published : UTF8ToString($1),
                            state : UTF8ToString($2),
                            error : UTF8ToString($3),
                            replacing : Boolean($4)
                        });
                    },
                    terrain->displayedRevision().c_str(),
                    generation->latestRevision.c_str(),
                    generation->state.c_str(),
                    generation->error.c_str(),
                    terrain->replacing()
                );
            EM_ASM(
                {
                    Module.terrainError = UTF8ToString($0);
                    Module.terrainCache = ([ $1, $2, $3 ]);
                },
                terrain->error().c_str(),
                cache[0],
                cache[1],
                cache[2]
            );
            EM_ASM(
                {
                    Module.terrainState = ({
                        roots : $0,
                        loadingRoots : $1,
                        selected : $2,
                        jobs : $3,
                        failed : $4,
                        cpu : $5,
                        gpu : $6,
                        retired : $7,
                        publications : $8,
                        surfaceDepth : $9,
                        unresolved : $10,
                        idle : $11,
                        displayedLeaves : $12
                    });
                },
                stats.admittedRoots,
                stats.loadingRoots,
                stats.selected,
                stats.jobs,
                stats.failed,
                double(stats.residentCpuBytes + stats.reservedCpuBytes + stats.retiredCpuBytes),
                double(stats.residentGpuBytes + stats.reservedGpuBytes + stats.retiredGpuBytes),
                double(stats.retiredGpuBytes),
                double(stats.publications),
                int(stats.deepestSurfaceDepth),
                int(stats.unresolvedRefinements),
                int(stats.planningIdle),
                terrain->displayedLeafCount()
            );
            const int command = EM_ASM_INT({
                const value = Module.terrainCommand || 0;
                Module.terrainCommand = 0;
                return value;
            });
            if (command == 1)
            {
                terrain->teleportToSurface({{-1000000000, 0, -1000000000}, {512, 0, 512}});
            }
            else if (command == 4)
                terrain->frameIsland();
            else if (command == 5 && terrain->generation())
            {
                auto recipe = terrain->generation()->accepted;
                recipe.seed += 5;
                recipe.plateauHeight += 80;
                terrain->regenerate(recipe);
            }
            if (command == 2)
            {
                terrain->teleportToSurface({{}, {0, 0, -300}});
            }
            if (command == 3)
            {
                terrain->teleport({{0, 1000000, 0}, {}});
            }
            const double routeSeconds = EM_ASM_DOUBLE({
                return Number.isFinite(Module.terrainRouteSeconds) ? Module.terrainRouteSeconds : -1;
            });
            if (routeSeconds >= 0)
            {
                terrain->traverse(routeSeconds);
            }
        }
        if (ofg::Game::scene().lighting.outdoor)
        {
            auto stats = ofg::Graphics::outdoorDiagnostics();
            EM_ASM(
                { Module.iblUpdate = ({steps : $0, passes : $1, publications : $2}); },
                stats.environmentSteps,
                stats.environmentPasses,
                stats.environmentPublications
            );
        }
        if (app.character && app.character->ready())
        {
            // Read-only smoke diagnostics; playback input goes through the actual workspace controls.
            EM_ASM({ Module.animations = []; });
            for (const auto& animator : ofg::Game::scene().animators())
            {
                const auto selected = animator->selectedAnimation();
                const char* name = selected ? animator->animations()[*selected]->desc().name.c_str() : "";
                EM_ASM(
                    {
                        Module.animations.push({
                            name : UTF8ToString($0),
                            index : $1,
                            time : $2,
                            duration : $3,
                            playing : Boolean($4),
                            looping : Boolean($5),
                            speed : $6
                        });
                    },
                    name,
                    selected ? int(*selected) : -1,
                    animator->timeSeconds(),
                    animator->durationSeconds(),
                    animator->isPlaying(),
                    animator->isLooping(),
                    animator->playbackSpeed()
                );
            }
        }
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
        const bool ready = app.pbr || (app.model && app.model->ready()) || (app.character && app.character->ready()) ||
                           (app.texture && app.texture->isLoaded());
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
