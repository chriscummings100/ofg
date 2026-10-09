// Real loopback WinHTTP and disk-cache contracts, driven by tools/terrain-client-test.py with an owned server.
#include <doctest.h>
#include "terrain/terrain-data-cache.h"
#include "platform/http-client.h"
#include "terrain/terrain-streaming-service.h"
#include "terrain/terrain-shading.h"
#include <set>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace ofg;
using namespace ofg::terrain;
namespace {
// Complete a real asynchronous HTTP operation using its wake signal, never timing-dependent polling sleeps.
HttpResponse http(std::string url, std::string body = {}, size_t limit = 256 << 10)
{
    auto wake = std::make_shared<StreamingWake>();
    auto request = HttpRequest::start(std::move(url), limit, true, std::move(body), wake);
    for (;;)
    {
        const auto sequence = wake->sequence();
        if (auto response = request->take())
            return std::move(*response);
        REQUIRE(wake->waitFor(sequence, std::chrono::seconds(20)));
    }
}

// Wait for exactly one acquisition outcome, retaining the separate request identity.
TerrainDataResult result(TerrainDataCache& cache)
{
    REQUIRE(cache.waitForResult(std::chrono::seconds(20)));
    auto results = cache.takeResults();
    REQUIRE(results.size() == 1);
    return std::move(results.front());
}
} // namespace

TEST_SUITE("terrain-http")
{
    TEST_CASE("HTTP streaming acquires each coarse footprint and inherits terminal source allocations")
    {
        const auto* base = std::getenv("OFG_TERRAIN_TEST_URL");
        REQUIRE(base);
        auto response = http(std::string(base) + std::getenv("OFG_TERRAIN_TEST_MANIFEST"));
        REQUIRE(response.status == 200);
        auto manifest = std::make_shared<TerrainManifest>(
            decodeTerrainManifest({reinterpret_cast<const char*>(response.bytes.data()), response.bytes.size()})
        );
        StreamSettings settings;
        settings.rootWidth = 131072;
        settings.maximumDepth = 6;
        settings.maximumPayloadBytes = 4 << 20;
        settings.maximumJobs = 8;
        GeneratorSettings generator;
        generator.rootWidth = settings.rootWidth;
        generator.intervals = 4;
        double viewRange = 0;
        SUBCASE("Small source inheritance fixture") {}
        SUBCASE("Complete laboratory footprint fits its residency budget")
        {
            settings.maximumDepth = 13;
            settings.maximumNodes = 32768;
            settings.maximumJobs = 32;
            settings.cpuBudget = 512ull << 20;
            settings.refinementRange = 4096;
            generator.intervals = 32;
            viewRange = 4096;
        }
        TerrainStreamingService service(
            settings,
            generator,
            viewRange,
            {base, std::string(std::getenv("OFG_TERRAIN_TEST_CACHE")) + "/stream", manifest, false}
        );
        service.setObserver({{}, {1, 48, 1}});
        std::set<const TerrainTile*> terminalSources;
        size_t inherited = 0, acquisitions = 0;
        bool stopping = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        for (;;)
        {
            const auto sequence = service.wakeSequence();
            auto batch = service.takeBatch();
            REQUIRE_MESSAGE(batch.error.empty(), batch.error);
            REQUIRE(batch.deepestAcquisitionDepth <= manifest->terminalDepth);
            for (auto& upload : batch.uploads)
            {
                REQUIRE(upload.source);
                CHECK(upload.source->address.depth <= manifest->terminalDepth);
                if (upload.request.address.depth <= manifest->terminalDepth)
                    CHECK(
                        upload.source->address == terrainTileAddress(upload.request.address, manifest->terminalDepth)
                    );
                if (upload.request.address.depth == manifest->terminalDepth)
                    terminalSources.insert(upload.source.get());
                if (upload.request.address.depth > manifest->terminalDepth)
                {
                    CHECK(terminalSources.contains(upload.source.get()));
                    ++inherited;
                }
                ReadyContent content;
                content.id = upload.request.id;
                content.cpuBytes = upload.geometry.allocatedBytes();
                content.source = upload.source;
                content.sourceGpuBytes = terrainTextureBytes;
                content.gpuBytes = upload.geometry.vertices.size() * sizeof(Vertex) +
                                   upload.geometry.indices.size() * sizeof(uint32_t);
                service.uploadReady(content.id, content);
            }
            for (const auto& [id, revision] : batch.retirements)
                service.payloadReleased(id);
            CHECK(
                batch.diagnostics.residentCpuBytes + batch.diagnostics.reservedCpuBytes +
                    batch.diagnostics.retiredCpuBytes <=
                settings.cpuBudget
            );
            REQUIRE_FALSE(
                (batch.diagnostics.planningIdle && batch.diagnostics.jobs == 0 && batch.diagnostics.budgetBlocked)
            );
            if (!stopping && batch.diagnostics.planningIdle &&
                batch.diagnostics.deepestSurfaceDepth == settings.maximumDepth)
            {
                REQUIRE(inherited > 0);
                std::printf(
                    "Service residency: CPU %zu; GPU %zu; decoded %zu; texture %zu\n",
                    batch.diagnostics.residentCpuBytes,
                    batch.diagnostics.residentGpuBytes,
                    batch.diagnostics.sourceCpuBytes,
                    batch.diagnostics.sourceTextureBytes
                );
                acquisitions = batch.cacheHits + batch.cacheMisses;
                CHECK(acquisitions > 1);
                CHECK(batch.diagnostics.unresolvedRefinements == 0);
                service.stop();
                stopping = true;
            }
            if (batch.stopped)
            {
                CHECK(batch.diagnostics.residentCpuBytes == 0);
                CHECK(batch.diagnostics.retiredCpuBytes == 0);
                CHECK(batch.cacheHits + batch.cacheMisses == acquisitions);
                break;
            }
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            REQUIRE(service.waitForChange(sequence, std::chrono::seconds(10)));
        }
    }
    TEST_CASE("Terrain cache uses persistent hits bypasses storage and survives corruption and write failures")
    {
        const auto* baseValue = std::getenv("OFG_TERRAIN_TEST_URL");
        const auto* manifestValue = std::getenv("OFG_TERRAIN_TEST_MANIFEST");
        const auto* cacheValue = std::getenv("OFG_TERRAIN_TEST_CACHE");
        REQUIRE(baseValue != nullptr);
        REQUIRE(manifestValue != nullptr);
        REQUIRE(cacheValue != nullptr);
        const std::string base(baseValue), directory(cacheValue);
        const auto response = http(base + manifestValue);
        REQUIRE(response.error.empty());
        REQUIRE(response.status == 200);
        auto manifest = std::make_shared<TerrainManifest>(
            decodeTerrainManifest({reinterpret_cast<const char*>(response.bytes.data()), response.bytes.size()})
        );
        const NodeAddress node{{}, 0, 0, 0, manifest->terminalDepth};
        uint64_t sequence = 1;
        {
            TerrainDataCache cache(base, directory);
            cache.request({1, sequence++}, manifest, node, false);
            auto miss = result(cache);
            REQUIRE(miss.error.empty());
            REQUIRE(miss.tile);
            CHECK_FALSE(miss.hit);
        }
        REQUIRE(http(base + "/probe/state", "{\"online\":false}").status == 200);
        {
            TerrainDataCache cache(base, directory);
            cache.request({1, sequence++}, manifest, node, false);
            const auto hit = result(cache);
            REQUIRE(hit.tile);
            CHECK(hit.hit);
            cache.request({1, sequence++}, manifest, node, true);
            const auto bypass = result(cache);
            CHECK_FALSE(bypass.error.empty());
            CHECK_FALSE(bypass.tile);
            cache.request({1, sequence++}, manifest, node, false);
            CHECK(result(cache).hit);
        }
        REQUIRE(http(base + "/probe/state", "{\"online\":true}").status == 200);
        for (auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.path().extension() == ".tile")
                std::ofstream(entry.path(), std::ios::binary) << "broken";
        {
            TerrainDataCache cache(base, directory);
            cache.request({1, sequence++}, manifest, node, false);
            const auto recovered = result(cache);
            REQUIRE(recovered.tile);
            CHECK_FALSE(recovered.hit);
            cache.request({1, sequence++}, manifest, node, true);
            auto bypass = result(cache);
            REQUIRE(bypass.tile);
            CHECK(bypass.bypassed);
            CHECK_FALSE(bypass.hit);
        }
        const auto invalidDirectory = std::filesystem::path(directory) / "file-not-directory";
        std::ofstream(invalidDirectory) << "occupied";
        {
            TerrainDataCache cache(base, invalidDirectory.string());
            cache.request({1, sequence++}, manifest, node, false);
            auto downloaded = result(cache);
            REQUIRE(downloaded.tile);
            CHECK(downloaded.error.empty());
            CHECK_FALSE(downloaded.warning.empty());
        }
        CHECK_FALSE(http(base + "/probe/oversized", {}, 1024).error.empty());
        auto cancelled = HttpRequest::start(base + "/probe/held", 1024);
        cancelled->cancel();
        auto outcome = cancelled->take();
        REQUIRE(outcome);
        CHECK(outcome->cancelled);
        REQUIRE(http(base + "/probe/release", "{}").status == 200);
        {
            TerrainDataCache cache(base, directory);
            RequestId id{1, sequence++};
            cache.request(id, manifest, node, true);
            cache.cancel(id);
            CHECK(result(cache).cancelled);
        }
    }
}
