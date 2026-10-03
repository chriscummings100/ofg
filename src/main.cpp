// Native entry point: D3D12 startup, a GLFW window, resize handling and scene presentation and selectable checkerboard
// diagnostic.
#include "checkerboard.h"
#include "game.h"
#include "render/graphics.h"
#include "lab/scene-fixture.h"
#include <slang-rhi/agility-sdk.h>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <slang-rhi/glfw.h>

#include <cstdio>
#include <cstring>
#include <memory>

SLANG_RHI_EXPORT_AGILITY_SDK

using namespace rhi;

// Processes events and submits frames; minimized windows sleep until events resume.
static Result runFrames(GLFWwindow* window, ISurface* surface, ICommandQueue* queue, IRenderPipeline* pipeline)
{
    double previousTime = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwWindowShouldClose(window) || glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            break;
        }

        const double now = glfwGetTime();
        const float deltaSeconds = float(now - previousTime);
        previousTime = now;
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        {
            if (!pipeline)
            {
                ofg::Game::frame(deltaSeconds, nullptr);
            }
            glfwWaitEvents();
            previousTime = glfwGetTime();
            continue;
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

        ComPtr<ITexture> image;
        SLANG_RETURN_ON_FAIL(surface->acquireNextImage(image.writeRef()));
        if (!pipeline)
        {
            ofg::Game::frame(deltaSeconds, image);
        }
        if (image)
        {
            if (pipeline)
            {
                SLANG_RETURN_ON_FAIL(drawCheckerboard(queue, pipeline, image));
            }
            SLANG_RETURN_ON_FAIL(surface->present());
        }
    }
    return SLANG_OK;
}

// Owns the window and presentation resources, draining GPU work before either is destroyed.
static Result runWindow(IDevice* device, bool checkerboard)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(
            960,
            640,
            checkerboard ? "OFG | D3D12 checkerboard" : "OFG | D3D12 scene objects",
            nullptr,
            nullptr
        ),
        glfwDestroyWindow
    );
    if (!window)
    {
        return SLANG_FAIL;
    }
    // Preserve brief key presses until runFrames polls them, even if release arrives in the same frame.
    glfwSetInputMode(window.get(), GLFW_STICKY_KEYS, GLFW_TRUE);

    ComPtr<ISurface> surface;
    SLANG_RETURN_ON_FAIL(device->createSurface(getWindowHandleFromGLFW(window.get()), surface.writeRef()));
    ComPtr<ICommandQueue> queue;
    SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));
    ComPtr<IRenderPipeline> pipeline;
    Result renderResult = SLANG_OK;
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
            ofg::Game::initialize(ofg::createSceneFixture());
        }
        renderResult = runFrames(window.get(), surface, queue, pipeline);
    } catch (const std::exception& error)
    {
        std::fprintf(stderr, "OFG scene failed: %s\n", error.what());
        renderResult = SLANG_FAIL;
    }
    ofg::Game::shutdown();
    const Result idleResult = queue->waitOnHost();
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
    const bool checkDevice = argc == 2 && std::strcmp(argv[1], "--check-device") == 0;
    const bool checkerboard = argc == 2 && std::strcmp(argv[1], "--checkerboard") == 0;
    if (argc != 1 && !checkDevice && !checkerboard)
    {
        std::fprintf(stderr, "Usage: ofg [--check-device | --checkerboard]\n");
        return 1;
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
    result = runWindow(device, checkerboard);
    glfwTerminate();
    if (SLANG_FAILED(result))
    {
        std::fprintf(stderr, "OFG rendering failed (result 0x%08X).\n", unsigned(result));
        return 1;
    }
    return 0;
}
