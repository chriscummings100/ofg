// Real D3D12 outdoor passes and HDR/readback smoke; screenshots are durable review evidence.
#include "render/graphics.h"
#include "lab/outdoor-fixture.h"
#include "render/sky-renderer.h"
#include "render/environment-renderer.h"
#include "render/resource-gpu-data.h"
#include "render/lighting-pass.h"
#include "math/quat.h"
#include <doctest.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <iterator>
namespace {
struct LightingDiagnostics : rhi::IDebugCallback
{
    std::atomic<int> errors{0};
    // Preserves validation output and counts errors for the integration assertion.
    void SLANG_MCALL handleMessage(
        rhi::DebugMessageType type,
        rhi::DebugMessageSource,
        const char* message
    ) noexcept override
    {
        std::fprintf(stderr, "Lighting RHI: %s\n", message);
        if (type == rhi::DebugMessageType::Error)
        {
            ++errors;
        }
    }
};
} // namespace
TEST_CASE("Aerial sampling interpolates physical distances and keeps exposure when disabled")
{
    rhi::DeviceDesc deviceDesc{};
    deviceDesc.deviceType = rhi::DeviceType::D3D12;
    rhi::ComPtr<rhi::IDevice> device;
    rhi::ComPtr<rhi::ICommandQueue> queue;
    REQUIRE(SLANG_SUCCEEDED(rhi::getRHI()->createDevice(deviceDesc, device.writeRef())));
    REQUIRE(SLANG_SUCCEEDED(device->getQueue(rhi::QueueType::Graphics, queue.writeRef())));
    // Compile the actual production composition function, with a synthetic linear-in-distance volume.
    std::string source;
    for (const char* path : {"shaders/sky/clouds.slang", "shaders/shadows/sampling.slang"})
    {
        std::ifstream file(std::filesystem::path(OFG_SOURCE_DIR) / path);
        REQUIRE(file.good());
        source.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        source += '\n';
    }
    source += R"(
// Fullscreen diagnostic: the left half covers 0..10 metres, the right half 0..10000 metres.
[shader("vertex")] float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
// A linear radiance ramp must remain linear after reconstructing quadratic depth slices.
[shader("fragment")] float4 fragmentMain(float4 pixel : SV_Position) : SV_Target
{
    float distance = pixel.x < 32 ? pixel.x / 32 * 10 : (pixel.x - 32) / 32 * 10000;
    return float4(applyAerialPerspective(float3(2), float3(0, 0, distance), pixel.xy), 1);
}
)";
    auto program = ofg::lightingProgram(device, source.c_str(), "aerial-sampling-regression");
    auto pipeline = ofg::lightingPipeline(device, program, rhi::Format::RGBA32Float);
    std::array<std::array<float, 4>, 32> pixels{};
    std::array<rhi::SubresourceData, 32> subresources{};
    for (uint32_t slice = 0; slice < 32; ++slice)
    {
        float distance = float(slice * slice) / (31 * 31) * 10000;
        pixels[slice] = {distance, distance, distance, 0};
        subresources[slice] = {pixels[slice].data(), 16, 16};
    }
    rhi::TextureDesc desc{};
    desc.type = rhi::TextureType::Texture2DArray;
    desc.arrayLength = 32;
    desc.size = {1, 1, 1};
    desc.format = rhi::Format::RGBA32Float;
    desc.usage = rhi::TextureUsage::ShaderResource;
    rhi::ComPtr<rhi::ITexture> volume, target;
    REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, subresources.data(), volume.writeRef())));
    desc.type = rhi::TextureType::Texture2D;
    desc.arrayLength = 1;
    desc.size = {64, 1, 1};
    desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
    REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, nullptr, target.writeRef())));
    rhi::SamplerDesc samplerDesc{};
    samplerDesc.minFilter = samplerDesc.magFilter = rhi::TextureFilteringMode::Point;
    rhi::ComPtr<rhi::ISampler> sampler;
    REQUIRE(SLANG_SUCCEEDED(device->createSampler(samplerDesc, sampler.writeRef())));
    for (bool enabled : {true, false})
    {
        rhi::ComPtr<rhi::IShaderObject> root;
        REQUIRE(SLANG_SUCCEEDED(device->createRootShaderObject(program, root.writeRef())));
        auto cursor = rhi::ShaderCursor(root);
        const float exposure[]{1, 2, 0, 0}, camera[]{0, 0, 0, 0}, range[]{0, 1, 0, 10000};
        const float viewport[]{64, 1, float(enabled), 0};
        REQUIRE(SLANG_SUCCEEDED(cursor["outdoor"]["enabledExposure"].setData(exposure, sizeof(exposure))));
        REQUIRE(SLANG_SUCCEEDED(cursor["outdoor"]["viewPositionNear"].setData(camera, sizeof(camera))));
        REQUIRE(SLANG_SUCCEEDED(cursor["outdoor"]["lightDirection"].setData(range, sizeof(range))));
        REQUIRE(SLANG_SUCCEEDED(cursor["outdoor"]["viewport"].setData(viewport, sizeof(viewport))));
        REQUIRE(SLANG_SUCCEEDED(cursor["aerialPerspective"].setBinding(volume)));
        REQUIRE(SLANG_SUCCEEDED(cursor["environmentSampler"].setBinding(sampler)));
        rhi::ComPtr<rhi::ICommandEncoder> encoder;
        REQUIRE(SLANG_SUCCEEDED(queue->createCommandEncoder(encoder.writeRef())));
        ofg::drawLightingPass(encoder, pipeline, root, target->getDefaultView(), 64, 1);
        ofg::submitLighting(queue, encoder);
        REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
        rhi::ComPtr<slang::IBlob> result;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(target, 0, 0, result.writeRef(), &layout)));
        auto values = static_cast<const float*>(result->getBufferPointer());
        float maxError = 0;
        for (uint32_t x = 0; x < 64; ++x)
        {
            float distance = x < 32 ? (x + .5f) / 32 * 10 : (x - 32 + .5f) / 32 * 10000;
            float expected = enabled ? (2 + distance) * 2 : 4;
            maxError = std::max(maxError, std::abs(values[x * 4] - expected));
        }
        INFO("Haze enabled: ", enabled, ", maximum error: ", maxError);
        // 0.005 allows FP32 arithmetic at 20,000 output units; the old weighting errs by about 5 units.
        CHECK(maxError < .005f);
    }
}
TEST_CASE("Outdoor renders noon twilight and moon with real atmosphere IBL and shadows")
{
    LightingDiagnostics diagnostics;
    rhi::DeviceDesc deviceDesc{};
    deviceDesc.deviceType = rhi::DeviceType::D3D12;
    deviceDesc.enableValidation = true;
    deviceDesc.debugCallback = &diagnostics;
    rhi::ComPtr<rhi::IDevice> device;
    rhi::ComPtr<rhi::ICommandQueue> queue;
    REQUIRE(SLANG_SUCCEEDED(rhi::getRHI()->createDevice(deviceDesc, device.writeRef())));
    REQUIRE(SLANG_SUCCEEDED(device->getQueue(rhi::QueueType::Graphics, queue.writeRef())));
    ofg::Graphics::initialize(device, queue);
    // Always release static graphics ownership, including when shader compilation throws.
    struct Cleanup
    {
        rhi::ICommandQueue* queue;
        // Drains commands before resetting Graphics and dependent GPU handles.
        ~Cleanup()
        {
            queue->waitOnHost();
            ofg::Graphics::shutdown();
        }
    } cleanup{queue};
    rhi::TextureDesc desc{};
    desc.size = {640, 400, 1};
    desc.format = rhi::Format::RGBA8Unorm;
    desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
    rhi::ComPtr<rhi::ITexture> target;
    REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, nullptr, target.writeRef())));
    auto scene = ofg::createOutdoorFixture();
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/lighting/native";
    std::filesystem::create_directories(folder);
    // Exercise the real capture/filter job without scene draws: compare every published face/mip, excluding padding.
    {
        ofg::SkyRenderer captureSky(device, queue);
        ofg::EnvironmentRenderer environments(device, queue);
        auto frozen = ofg::buildDrawList(*scene, *scene->activeCamera(), 1.6f);
        captureSky.update(frozen);
        // Reads only the published slot and strips backend-specific row padding for an exact GPU comparison.
        const auto readGeneration = [&]()
        {
            REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
            std::vector<std::byte> result;
            auto texture = environments.dynamic().cubes;
            uint32_t firstLayer = uint32_t(environments.blend().y) * 18;
            for (uint32_t layer = firstLayer; layer < firstLayer + 18; ++layer)
            {
                for (uint32_t mip = 0; mip < 7; ++mip)
                {
                    rhi::ComPtr<slang::IBlob> pixels;
                    rhi::SubresourceLayout layout{};
                    REQUIRE(SLANG_SUCCEEDED(device->readTexture(texture, layer, mip, pixels.writeRef(), &layout)));
                    auto data = static_cast<const std::byte*>(pixels->getBufferPointer());
                    for (uint32_t row = 0; row < (64u >> mip); ++row)
                    {
                        result.insert(
                            result.end(),
                            data + row * layout.rowPitch,
                            data + row * layout.rowPitch + (64u >> mip) * 8
                        );
                    }
                }
            }
            return result;
        };
        // Startup never exposes incomplete storage, and unchanged paused frames subsequently do no work.
        for (uint32_t step = 0; step < 24; ++step)
        {
            environments.update(frozen, captureSky);
            CHECK(environments.lastPassCount() == (step < 6 ? 1 : 7));
            CHECK(environments.hasDynamic() == (step == 23));
        }
        auto reference = readGeneration();
        environments.update(frozen, captureSky);
        CHECK(environments.lastPassCount() == 0);
        CHECK(environments.publicationCount() == 1);

        auto edited = frozen;
        edited.lighting.outdoor->dayCycle.timeHours = 0;
        edited.outdoor = ofg::evaluateOutdoorLighting(*edited.lighting.outdoor, edited.cameraPosition.y);
        // Stale daylight keeps its original display exposure when the clock jumps to night.
        auto scales = environments.exposureScales(edited);
        CHECK(scales.y * edited.outdoor->exposureMultiplier == doctest::Approx(frozen.outdoor->exposureMultiplier));
        auto manual = edited;
        manual.lighting.outdoor->exposure.automatic = false;
        manual.lighting.outdoor->exposure.manualEv = 4;
        manual.outdoor = ofg::evaluateOutdoorLighting(*manual.lighting.outdoor, manual.cameraPosition.y);
        CHECK(environments.exposureScales(manual).y == doctest::Approx(1));
        captureSky.update(edited);
        environments.update(edited, captureSky);
        CHECK(environments.completedSteps() == 1);
        CHECK(readGeneration() == reference);
        for (uint32_t step = 1; step < 24; ++step)
        {
            environments.update(edited, captureSky);
        }
        auto night = readGeneration();
        CHECK(night != reference);
        CHECK(environments.publicationCount() == 2);
        CHECK(environments.exposureScales(edited).y == doctest::Approx(1));

        // Start the original snapshot, then alter live atmosphere/clouds/time/camera during capture and filtering.
        captureSky.update(frozen);
        environments.update(frozen, captureSky);
        edited.lighting.outdoor->atmosphere.mie = 2;
        edited.lighting.outdoor->clouds.coverage = .95f;
        edited.lighting.outdoor->dayCycle.elapsedSeconds = 120;
        edited.cameraPosition.y += 50;
        edited.outdoor = ofg::evaluateOutdoorLighting(*edited.lighting.outdoor, edited.cameraPosition.y);
        captureSky.update(edited);
        for (uint32_t step = 1; step < 24; ++step)
        {
            environments.update(edited, captureSky);
            CHECK(environments.lastPassCount() == (step < 6 ? 1 : 7));
            CHECK(environments.publicationCount() == (step == 23 ? 3 : 2));
            if (step == 12)
            {
                CHECK(readGeneration() == night);
            }
        }
        CHECK(readGeneration() == reference);
        // The next job picks up the latest edit rather than dropping it or restarting unfinished work.
        for (uint32_t step = 0; step < 24; ++step)
        {
            environments.update(edited, captureSky);
        }
        CHECK(environments.publicationCount() == 4);
        CHECK(readGeneration() != night);
        CHECK(diagnostics.errors == 0);
    }
    for (auto hour : {12.0, 0.0, 17.9, 18.5})
    {
        scene->lighting.outdoor->dayCycle.timeHours = hour;
        auto list = ofg::buildDrawList(*scene, *scene->activeCamera(), 1.6f);
        for (int step = 0; step < 24; ++step)
        {
            ofg::Graphics::render(list, target);
            auto stats = ofg::Graphics::outdoorDiagnostics();
            CHECK(stats.environmentPasses <= 7);
            if (hour == 12.0)
            {
                CHECK(stats.ready == (step == 23));
            }
            if (hour == 0.0 && step == 3)
            {
                // Noon IBL is still active under midnight exposure; it must not turn the ground/objects white.
                REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
                rhi::ComPtr<slang::IBlob> pending;
                rhi::SubresourceLayout pendingLayout{};
                REQUIRE(SLANG_SUCCEEDED(device->readTexture(target, 0, 0, pending.writeRef(), &pendingLayout)));
                auto data = static_cast<const unsigned char*>(pending->getBufferPointer());
                size_t white = 0;
                for (size_t y = 0; y < 400; ++y)
                {
                    for (size_t x = 0; x < 640; ++x)
                    {
                        auto pixel = data + y * pendingLayout.rowPitch + x * 4;
                        white += pixel[0] + pixel[1] + pixel[2] > 750;
                    }
                }
                CHECK(white < 640 * 400 / 10);
            }
        }
        REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
        rhi::ComPtr<slang::IBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        REQUIRE(layout.colPitch == 4);
        std::ofstream file(folder / ("hour-" + std::to_string(int(hour * 10)) + ".ppm"), std::ios::binary);
        file << "P6\n640 400\n255\n";
        auto bytes = static_cast<const unsigned char*>(pixels->getBufferPointer());
        size_t nonblack = 0, white = 0;
        for (size_t y = 0; y < 400; ++y)
        {
            for (size_t x = 0; x < 640; ++x)
            {
                auto p = bytes + y * layout.rowPitch + x * 4;
                file.write(reinterpret_cast<const char*>(p), 3);
                nonblack += p[0] + p[1] + p[2] > 3;
                white += p[0] + p[1] + p[2] > 750;
            }
        }
        CHECK(nonblack > 640 * 400 / 2);
        CHECK(white < 640 * 400 / 3);
        CHECK(diagnostics.errors == 0);
    }
    // Compare identical IBL snapshots with/without geometry shadows; only direct lighting may change.
    scene->lighting.outdoor->dayCycle.timeHours = 12;
    scene->lighting.outdoor->clouds.coverage = 0;
    // Settle the new indirect light before isolating direct-shadow differences.
    for (int step = 0; step < 24; ++step)
    {
        ofg::Graphics::render(ofg::buildDrawList(*scene, *scene->activeCamera(), 1.6f), target);
    }
    const auto renderPixels = [&](bool shadows, bool direct)
    {
        scene->lighting.outdoor->shadows.enabled = shadows;
        auto list = ofg::buildDrawList(*scene, *scene->activeCamera(), 1.6f);
        if (!direct)
        {
            list.lighting.lights[0].intensity = 0;
        }
        ofg::Graphics::render(list, target);
        queue->waitOnHost();
        rhi::ComPtr<slang::IBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        std::vector<unsigned char> result;
        result.reserve(640 * 400 * 3);
        auto data = static_cast<const unsigned char*>(pixels->getBufferPointer());
        for (size_t y = 0; y < 400; ++y)
        {
            for (size_t x = 0; x < 640; ++x)
            {
                for (size_t c = 0; c < 3; ++c)
                {
                    result.push_back(data[y * layout.rowPitch + x * 4 + c]);
                }
            }
        }
        return result;
    };
    auto shaded = renderPixels(true, true), unshadowed = renderPixels(false, true);
    size_t differences = 0;
    for (size_t i = 0; i < shaded.size(); ++i)
    {
        differences += std::abs(int(shaded[i]) - int(unshadowed[i])) > 3;
    }
    CHECK(differences > 500);
    for (const auto& [name, pixels] : {std::pair{"shadows-on", &shaded}, std::pair{"shadows-off", &unshadowed}})
    {
        std::ofstream image(folder / (std::string(name) + ".ppm"), std::ios::binary);
        image << "P6\n640 400\n255\n";
        image.write(reinterpret_cast<const char*>(pixels->data()), pixels->size());
    }

    auto indirectShaded = renderPixels(true, false), indirectUnshadowed = renderPixels(false, false);
    int maxDifference = 0;
    for (size_t i = 0; i < shaded.size(); ++i)
    {
        maxDifference = std::max(maxDifference, std::abs(int(indirectShaded[i]) - int(indirectUnshadowed[i])));
    }
    CHECK(maxDifference <= 1);
    // CPU wall time includes encoding, submission and a GPU drain; it is not a GPU timestamp measurement.
    scene->lighting.outdoor->shadows.enabled = true;
    auto frame = ofg::buildDrawList(*scene, *scene->activeCamera(), 1.6f);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 30; ++i)
    {
        ofg::Graphics::render(frame, target);
        queue->waitOnHost();
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 30;
    std::ofstream report(folder / "timing.txt");
    report
        << "D3D12 640x400 outdoor, paused clear noon, 1024 cascades; 30 warm frames, encode+submit+GPU-drain wall ms: "
        << ms << "\n";
    // A low manual EV aimed directly at the sun must saturate rather than store FP16 infinities/NaNs.
    auto& outdoor = *scene->lighting.outdoor;
    outdoor.atmosphere.sunIlluminance = 200000;
    outdoor.exposure.automatic = false;
    outdoor.exposure.manualEv = -2;
    auto body = ofg::evaluateOutdoorLighting(outdoor, 3);
    auto camera = scene->activeCamera()->entity();
    auto position = camera->localTransform().position;
    std::string error;
    auto rotation = ofg::math::quatLookAtLh(position, ofg::math::add(position, body.sunDirection), {0, 1, 0}, error);
    REQUIRE(rotation);
    camera->setLocalRotation(*rotation);
    rhi::TextureDesc hdrDesc = desc;
    hdrDesc.size = {65, 65, 1};
    hdrDesc.format = rhi::Format::RGBA16Float;
    rhi::ComPtr<rhi::ITexture> hdr;
    REQUIRE(SLANG_SUCCEEDED(device->createTexture(hdrDesc, nullptr, hdr.writeRef())));
    ofg::SkyRenderer sky(device, queue);
    auto solarFrame = ofg::buildDrawList(*scene, *scene->activeCamera(), 1);
    sky.update(solarFrame);
    sky.drawBackground(solarFrame, hdr);
    REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
    rhi::ComPtr<slang::IBlob> solarPixels;
    rhi::SubresourceLayout solarLayout{};
    REQUIRE(SLANG_SUCCEEDED(device->readTexture(hdr, 0, 0, solarPixels.writeRef(), &solarLayout)));
    bool finite = true;
    auto data = static_cast<const std::byte*>(solarPixels->getBufferPointer());
    for (size_t y = 0; y < 65; ++y)
    {
        auto row = reinterpret_cast<const uint16_t*>(data + y * solarLayout.rowPitch);
        for (size_t x = 0; x < 65 * 4; ++x)
            finite = finite && (row[x] & 0x7c00) != 0x7c00;
    }
    CHECK(finite);
    auto center = reinterpret_cast<const uint16_t*>(data + 32 * solarLayout.rowPitch) + 32 * 4;
    CHECK(center[0] == 0x7bff);
    // A vacuum has neither haze radiance nor opacity, even where a view ray intersects the planet.
    solarFrame.camera.worldFromView = ofg::math::mat4Identity();
    solarFrame.cameraPosition = {0, 3, 0};
    auto& vacuum = *solarFrame.lighting.outdoor;
    vacuum.dayCycle.timeHours = 12;
    vacuum.atmosphere.rayleigh = vacuum.atmosphere.mie = vacuum.atmosphere.ozone = 0;
    vacuum.atmosphere.groundAlbedo = 1;
    solarFrame.outdoor = ofg::evaluateOutdoorLighting(vacuum, 3);
    sky.update(solarFrame);
    REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
    for (uint32_t slice : {0u, 1u, 2u, 16u, 31u})
    {
        rhi::ComPtr<slang::IBlob> haze;
        rhi::SubresourceLayout hazeLayout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(sky.aerialPerspective(), slice, 0, haze.writeRef(), &hazeLayout)));
        auto bytes = static_cast<const std::byte*>(haze->getBufferPointer());
        size_t nonzero = 0;
        for (uint32_t y = 0; y < 32; ++y)
        {
            auto row = reinterpret_cast<const uint16_t*>(bytes + y * hazeLayout.rowPitch);
            for (uint32_t x = 0; x < 32 * 4; ++x)
            {
                nonzero += (row[x] & 0x7fff) != 0;
            }
        }
        INFO("Vacuum aerial slice ", slice);
        CHECK(nonzero == 0);
    }
    CHECK(diagnostics.errors == 0);
}
