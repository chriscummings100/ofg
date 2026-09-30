// GPU integration test: render the same shader/draw used by the window and verify every RGBA8 pixel.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "checkerboard.h"
#include <slang-rhi/agility-sdk.h>

#include <atomic>
#include <cstdio>

SLANG_RHI_EXPORT_AGILITY_SDK

using namespace rhi;

struct TestDiagnostics : IDebugCallback
{
    // Retains validation errors even when the API returns success, so they cannot silently pass the test.
    void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char* message) noexcept override
    {
        std::fprintf(stderr, "RHI: %s\n", message);
        if (type == DebugMessageType::Error)
        {
            ++errorCount;
        }
    }

    std::atomic<int> errorCount = 0;
};

// Odd, non-square targets expose partial cells, viewport mistakes and padded readback rows.
TEST_CASE("Checkerboard draw covers the target with exact 64-pixel squares")
{
    TestDiagnostics diagnostics;
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = DeviceType::D3D12;
    deviceDesc.enableValidation = true;
    deviceDesc.debugCallback = &diagnostics;
    ComPtr<IDevice> device;
    REQUIRE(SLANG_SUCCEEDED(getRHI()->createDevice(deviceDesc, device.writeRef())));
    std::printf("Checkerboard test on %s.\n", device->getInfo().adapterName);

    ComPtr<ICommandQueue> queue;
    REQUIRE(SLANG_SUCCEEDED(device->getQueue(QueueType::Graphics, queue.writeRef())));
    ComPtr<IRenderPipeline> pipeline;
    REQUIRE(SLANG_SUCCEEDED(createCheckerboardPipeline(device, Format::RGBA8Unorm, pipeline.writeRef())));

    const uint32_t sizes[][2] = {{1, 1}, {129, 97}, {259, 193}};
    for (const auto& size : sizes)
    {
        CAPTURE(size[0]);
        CAPTURE(size[1]);
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.format = Format::RGBA8Unorm;
        desc.size = {size[0], size[1], 1};
        desc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        desc.defaultState = ResourceState::RenderTarget;
        ComPtr<ITexture> texture;
        REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, nullptr, texture.writeRef())));
        const Result drawResult = drawCheckerboard(queue, pipeline, texture);
        REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
        REQUIRE(SLANG_SUCCEEDED(drawResult));

        ComPtr<ISlangBlob> pixels;
        SubresourceLayout layout = {};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(texture, 0, 0, pixels.writeRef(), &layout)));
        const auto data = static_cast<const uint8_t*>(pixels->getBufferPointer());
        REQUIRE(layout.rowPitch >= size[0] * 4);
        REQUIRE(pixels->getBufferSize() >= (size[1] - 1) * layout.rowPitch + size[0] * 4);
        for (uint32_t y = 0; y < size[1]; ++y)
        {
            for (uint32_t x = 0; x < size[0]; ++x)
            {
                const auto pixel = data + y * layout.rowPitch + x * 4;
                const uint8_t expected = ((x / 64 + y / 64) % 2 == 0) ? 32 : 224;
                if (pixel[0] != expected || pixel[1] != expected || pixel[2] != expected || pixel[3] != 255)
                {
                    FAIL(
                        "Unexpected checkerboard pixel at (",
                        x,
                        ", ",
                        y,
                        "): ",
                        int(pixel[0]),
                        ", ",
                        int(pixel[1]),
                        ", ",
                        int(pixel[2]),
                        ", ",
                        int(pixel[3]),
                        "; expected gray ",
                        int(expected)
                    );
                }
            }
        }
    }
    CHECK(diagnostics.errorCount == 0);
}
