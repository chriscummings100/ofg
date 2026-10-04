// Native GPU readback and raster tests for the portable compute-to-vertex boundary.
#include "compute-vertex-proof.h"
#include <doctest.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
struct ComputeDiagnostics : rhi::IDebugCallback
{
    // Keeps asynchronous validation failures visible to doctest.
    void SLANG_MCALL handleMessage(
        rhi::DebugMessageType type,
        rhi::DebugMessageSource,
        const char* message
    ) noexcept override
    {
        std::fprintf(stderr, "RHI: %s\n", message);
        if (type == rhi::DebugMessageType::Error)
        {
            ++errors;
        }
    }
    std::atomic<int> errors{0};
};
} // namespace

TEST_CASE("Compute vertex proof preserves packing and orders uploads before rendering")
{
    ComputeDiagnostics diagnostics;
    rhi::DeviceDesc desc{};
    desc.deviceType = rhi::DeviceType::D3D12;
    desc.enableValidation = true;
    desc.debugCallback = &diagnostics;
    rhi::ComPtr<rhi::IDevice> device;
    REQUIRE(SLANG_SUCCEEDED(rhi::getRHI()->createDevice(desc, device.writeRef())));
    std::printf("Compute vertex proof on %s.\n", device->getInfo().adapterName);
    rhi::ComPtr<rhi::ICommandQueue> queue;
    REQUIRE(SLANG_SUCCEEDED(device->getQueue(rhi::QueueType::Graphics, queue.writeRef())));
    ofg::ComputeVertexProof proof(device, queue, rhi::Format::RGBA8Unorm);
    rhi::ComPtr<rhi::ITexture> targets[2];
    for (auto& target : targets)
    {
        rhi::TextureDesc texture{};
        texture.size = {256, 256, 1};
        texture.format = rhi::Format::RGBA8Unorm;
        texture.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
        texture.defaultState = rhi::ResourceState::RenderTarget;
        REQUIRE(SLANG_SUCCEEDED(device->createTexture(texture, nullptr, target.writeRef())));
    }
    // The second upload must not overwrite the first draw's data while its submission is in flight.
    proof.draw(targets[0], 0);
    proof.draw(targets[1], .8f);
    REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
    auto vertices = ofg::ComputeVertexProof::sourceVertices();
    decltype(vertices) actual{};
    REQUIRE(SLANG_SUCCEEDED(device->readBuffer(proof.output(), 0, sizeof(actual), actual.data())));
    for (size_t i = 0; i < actual.size(); ++i)
    {
        CAPTURE(i);
        CHECK(std::abs(actual[i].position.x - (vertices[i].position.x + .8f)) <= 1e-4f);
        CHECK(actual[i].position.y == vertices[i].position.y);
        CHECK(actual[i].position.z == vertices[i].position.z);
        CHECK(std::memcmp(&actual[i].normal, &vertices[i].normal, sizeof(ofg::Vertex) - 12) == 0);
    }
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/animation/native";
    std::filesystem::create_directories(folder);
    for (int frame = 0; frame < 2; ++frame)
    {
        rhi::ComPtr<slang::IBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(targets[frame], 0, 0, pixels.writeRef(), &layout)));
        const auto* bytes = static_cast<const uint8_t*>(pixels->getBufferPointer());
        const int expectedX = frame == 0 ? 64 : 166;
        const int emptyX = frame == 0 ? 166 : 64;
        CHECK(bytes[128 * layout.rowPitch + expectedX * 4 + 1] == 255);
        CHECK(bytes[128 * layout.rowPitch + emptyX * 4 + 1] == 0);
        std::ofstream image(folder / ("compute-" + std::to_string(frame) + ".ppm"), std::ios::binary);
        image << "P6\n256 256\n255\n";
        for (int y = 0; y < 256; ++y)
        {
            for (int x = 0; x < 256; ++x)
            {
                image.write(reinterpret_cast<const char*>(bytes + y * layout.rowPitch + x * 4), 3);
            }
        }
        REQUIRE(image.good());
    }
    CHECK(diagnostics.errors == 0);
}
