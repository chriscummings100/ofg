// Native entry point: D3D12 startup, a GLFW window, resize handling and scene presentation and selectable checkerboard
// diagnostic.
#include "checkerboard.h"
#include "game.h"
#include "core/engine-error.h"
#include "render/graphics.h"
#include "lab/scene-fixture.h"
#include "lab/pbr-fixture.h"
#include "lab/model-fixture.h"
#include "lab/character-fixture.h"
#include "lab/fly-camera.h"
#include "ui/workspace.h"
#include "render/present.h"
#include <backends/imgui_impl_glfw.h>
#include <fstream>
#include <cstdlib>
#include <slang-rhi/agility-sdk.h>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <slang-rhi/glfw.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <filesystem>
#include <utility>

SLANG_RHI_EXPORT_AGILITY_SDK

using namespace rhi;

struct NativeActions
{
    bool escape = false, reset = false, closeup = false;
};
// Retains brief action presses without making movement keys sticky across capture/focus changes.
static void recordKeyAction(GLFWwindow* window, int key, int, int action, int)
{
    if (action != GLFW_PRESS)
    {
        return;
    }
    auto& input = *static_cast<NativeActions*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_ESCAPE)
    {
        input.escape = true;
    }
    if (key == GLFW_KEY_R)
    {
        input.reset = true;
    }
    if (key == GLFW_KEY_F)
    {
        input.closeup = true;
    }
}
// Focus loss discards queued actions; GLFW itself releases held key/button state.
static void clearActionsOnBlur(GLFWwindow* window, int focused)
{
    if (!focused)
    {
        *static_cast<NativeActions*>(glfwGetWindowUserPointer(window)) = {};
    }
}

// Processes events and submits frames; minimized windows sleep until events resume.
static Result runFrames(
    GLFWwindow* window,
    ISurface* surface,
    ICommandQueue* queue,
    IRenderPipeline* pipeline,
    bool pbr,
    ofg::ModelFixture* model,
    ofg::CharacterFixture* character,
    ofg::Workspace* workspace,
    IRenderPipeline* presentPipeline
)
{
    auto& actions = *static_cast<NativeActions*>(glfwGetWindowUserPointer(window));
    double previousTime = glfwGetTime();
    ofg::FlyCamera camera;
    bool captured = false, previousMouse = false;
    double mouseX = 0, mouseY = 0;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        const bool escape = std::exchange(actions.escape, false);
        if (glfwWindowShouldClose(window) || (escape && !captured && !workspace))
        {
            break;
        }

        const double now = glfwGetTime();
        const float deltaSeconds = float(now - previousTime);
        previousTime = now;
        if (model)
        {
            const bool wasReady = model->ready();
            model->update(ofg::Game::scene());
            if (!wasReady && model->ready())
            {
                glfwSetWindowTitle(window, model->status().c_str());
            }
        }
        int width = 0, height = 0;
        if (character)
        {
            character->update(ofg::Game::scene());
        }
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            if (!pipeline)
            {
                ofg::Game::frame(deltaSeconds, nullptr);
            }
            captured = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwWaitEvents();
            previousTime = glfwGetTime();
            continue;
        }
        if (workspace)
        {
            ImGui_ImplGlfw_NewFrame();
            float scaleX = 1, scaleY = 1;
            glfwGetWindowContentScale(window, &scaleX, &scaleY);
            workspace->begin(ofg::Game::scene(), deltaSeconds, scaleX);
        }
        if (pbr || model || character)
        {
            const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
            const bool mouse = focused && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            double cursorX = 0, cursorY = 0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            if (mouse && !previousMouse && !escape &&
                (!workspace || workspace->canCaptureAt(float(cursorX), float(cursorY))))
            {
                captured = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                glfwGetCursorPos(window, &mouseX, &mouseY);
            }
            if (!focused || escape || !mouse || (workspace && !workspace->sceneTarget()))
            {
                captured = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            ofg::FlyCameraInput input;
            if (focused && (!workspace || captured || workspace->cameraKeyboardAllowed()))
            {
                for (int view = 0; view <= 4; ++view)
                {
                    if (glfwGetKey(window, GLFW_KEY_0 + view) == GLFW_PRESS)
                    {
                        ofg::Game::scene().lighting.debugView = view;
                    }
                }
                input.reset = std::exchange(actions.reset, false);
                input.closeup = std::exchange(actions.closeup, false);
                if (model && input.reset)
                {
                    input.closeup = true;
                }
            }
            actions.reset = actions.closeup = false;
            if (captured)
            {
                double x, y;
                glfwGetCursorPos(window, &x, &y);
                input.lookPixels = {float(x - mouseX), float(y - mouseY)};
                mouseX = x;
                mouseY = y;
                input.movement = {
                    float(
                        (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) - (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
                    ),
                    float(
                        (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) - (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
                    ),
                    float(
                        (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) - (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
                    )
                };
                input.fast = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            }
            if (auto active = ofg::Game::scene().activeCamera())
            {
                camera.update(*active->entity(), input, deltaSeconds);
                if (character && (input.reset || input.closeup))
                {
                    ofg::frameCharacter(*active->entity());
                }
            }
            previousMouse = mouse;
        }
        const auto config = surface->getConfig();
        if (!config || config->width != uint32_t(width) || config->height != uint32_t(height))
        {
            // Release all in-flight references before recreating the swap chain for the new pixel size.
            SLANG_RETURN_ON_FAIL(queue->waitOnHost());
            SurfaceConfig resized = {};
            resized.format = surface->getInfo().preferredFormat;
            resized.width = uint32_t(width);
            resized.height = uint32_t(height);
            SLANG_RETURN_ON_FAIL(surface->configure(resized));
        }

        ITexture* composition = nullptr;
        if (workspace)
        {
            ofg::Game::frame(deltaSeconds, workspace->sceneTarget());
            composition = workspace->finish();
        }
        ComPtr<ITexture> image;
        SLANG_RETURN_ON_FAIL(surface->acquireNextImage(image.writeRef()));
        if (!pipeline && !workspace)
        {
            ofg::Game::frame(deltaSeconds, image);
        }
        if (image)
        {
            if (pipeline)
            {
                SLANG_RETURN_ON_FAIL(drawCheckerboard(queue, pipeline, image));
            }
            if (composition)
            {
                SLANG_RETURN_ON_FAIL(drawPresentation(queue, presentPipeline, composition, image));
            }
            SLANG_RETURN_ON_FAIL(surface->present());
        }
    }
    return SLANG_OK;
}

// Owns the window and presentation resources, draining GPU work before either is destroyed.
static Result runWindow(IDevice* device, bool checkerboard, bool pbr, const char* modelPath, int characterMode, bool ui)
{
    ui = ui && !checkerboard;
    NativeActions actions; // Lives through window destruction, including exceptions and early RHI failures.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, ui ? GLFW_TRUE : GLFW_FALSE);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(
            ui ? 1280 : 960,
            ui ? 800 : 640,
            checkerboard ? "OFG | D3D12 checkerboard"
            : ui         ? "OFG | Terrain Laboratory"
            : pbr        ? "OFG | PBR spheres - RMB + WASDQE, Shift, R reset, F close-up"
                         : "OFG | D3D12 scene objects",
            nullptr,
            nullptr
        ),
        glfwDestroyWindow
    );
    if (!window)
    {
        return SLANG_FAIL;
    }
    // Movement keys must reflect current physical state and must not replay after focus/capture loss.
    glfwSetInputMode(window.get(), GLFW_STICKY_KEYS, GLFW_FALSE);
    glfwSetWindowUserPointer(window.get(), &actions);
    glfwSetKeyCallback(window.get(), recordKeyAction);
    glfwSetWindowFocusCallback(window.get(), clearActionsOnBlur);

    ComPtr<ISurface> surface;
    SLANG_RETURN_ON_FAIL(device->createSurface(getWindowHandleFromGLFW(window.get()), surface.writeRef()));
    ComPtr<ICommandQueue> queue;
    SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));
    ComPtr<IRenderPipeline> pipeline, presentPipeline;
    std::unique_ptr<ofg::Workspace> workspace;
    std::filesystem::path layoutPath;
    bool platformInitialized = false;
    Result renderResult = SLANG_OK;
    std::unique_ptr<ofg::ModelFixture> model;
    std::unique_ptr<ofg::CharacterFixture> character;
    try
    {
        if (checkerboard)
        {
            SLANG_RETURN_ON_FAIL(
                createCheckerboardPipeline(device, surface->getInfo().preferredFormat, pipeline.writeRef())
            );
        }
        else
        {
            ofg::Graphics::initialize(device, queue);
            wchar_t executable[32768];
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (!length || length == 32768)
            {
                throw ofg::EngineError("Cannot resolve executable asset directory.");
            }
            auto path = std::filesystem::path(executable).parent_path() / "assets/checker.png";
            if (characterMode)
            {
                ofg::Game::initialize(ofg::createCharacterFixtureScene());
                character = std::make_unique<ofg::CharacterFixture>(
                    (std::filesystem::path(executable).parent_path() / "assets/models/character").string(),
                    characterMode == 2
                );
                glfwSetWindowTitle(window.get(), "OFG | Animated superhero");
            }
            else if (modelPath)
            {
                ofg::Game::initialize(ofg::createModelFixtureScene());
                model = std::make_unique<ofg::ModelFixture>(modelPath);
                glfwSetWindowTitle(window.get(), "OFG | Loading model...");
            }
            else
            {
                ofg::Game::initialize(pbr ? ofg::createPbrFixture() : ofg::createSceneFixture(path.string()));
            }
        }
        if (ui && !checkerboard)
        {
            workspace = std::make_unique<ofg::Workspace>(device, queue);
            if (!ImGui_ImplGlfw_InitForOther(window.get(), true))
            {
                throw ofg::EngineError("Initialize ImGui GLFW input failed.");
            }
            platformInitialized = true;
            if (auto local = std::getenv("LOCALAPPDATA"))
            {
                layoutPath = std::filesystem::path(local) / "OFG" / "workspace.ini";
                std::ifstream file(layoutPath);
                if (file)
                {
                    workspace->loadLayout(std::string(std::istreambuf_iterator<char>(file), {}));
                }
            }
            auto result = createPresentationPipeline(
                device,
                surface->getInfo().preferredFormat,
                presentPipeline.writeRef(),
                true
            );
            if (SLANG_FAILED(result))
            {
                throw ofg::EngineError("Create workspace presentation failed.");
            }
        }
        renderResult = runFrames(
            window.get(),
            surface,
            queue,
            pipeline,
            pbr,
            model.get(),
            character.get(),
            workspace.get(),
            presentPipeline
        );
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "OFG scene failed: %s\n", error.what());
        renderResult = SLANG_FAIL;
    }
    if (workspace && !layoutPath.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(layoutPath.parent_path(), error);
        std::ofstream file(layoutPath);
        if (!error && file)
        {
            file << workspace->saveLayout();
        }
        if (error || !file)
        {
            std::fprintf(stderr, "Could not save OFG UI layout.\n");
        }
    }
    if (platformInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
    }
    ofg::Game::shutdown();
    const Result idleResult = queue->waitOnHost();
    workspace.reset();
    ofg::Graphics::shutdown();
    return SLANG_FAILED(renderResult) ? renderResult : idleResult;
}

// Reports GLFW setup/window errors with their original diagnostic text.
static void reportWindowError(int code, const char* message)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
}

// Opens the scene by default; --checkerboard selects the original diagnostic; --check-device retains the finite,
// window-free startup test.
int main(int argc, char** argv)
{
    bool checkDevice = false, checkerboard = false, scene = false, ui = true;
    int character = 0;
    const char* modelPath = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--check-device") == 0)
        {
            checkDevice = true;
        }
        else if (std::strcmp(argv[i], "--checkerboard") == 0)
        {
            checkerboard = true;
        }
        else if (std::strcmp(argv[i], "--scene") == 0)
        {
            scene = true;
        }
        else if (std::strcmp(argv[i], "--character") == 0)
        {
            character = 1;
        }
        else if (std::strcmp(argv[i], "--character-pair") == 0)
        {
            character = 2;
        }
        else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc)
        {
            modelPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--no-ui") == 0)
        {
            ui = false;
        }
        else
        {
            std::fprintf(
                stderr,
                "Usage: ofg [--check-device | --checkerboard | --scene | --character | --model <path>] [--no-ui]\n"
            );
            return 1;
        }
    }

    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = DeviceType::D3D12;
    deviceDesc.enableValidation = true;
    ComPtr<IDevice> device;
    Result result = getRHI()->createDevice(deviceDesc, device.writeRef());
    if (SLANG_FAILED(result))
    {
        std::fprintf(stderr, "OFG could not create a D3D12 device (result 0x%08X).\n", unsigned(result));
        return 1;
    }
    std::printf("OFG initialized D3D12 on %s.\n", device->getInfo().adapterName);
    if (checkDevice)
    {
        return 0;
    }

    glfwSetErrorCallback(reportWindowError);
    if (!glfwInit())
    {
        return 1;
    }
    result = runWindow(device, checkerboard, !checkerboard && !scene, modelPath, character, ui);
    glfwTerminate();
    if (SLANG_FAILED(result))
    {
        std::fprintf(stderr, "OFG rendering failed (result 0x%08X).\n", unsigned(result));
        return 1;
    }
    return 0;
}
