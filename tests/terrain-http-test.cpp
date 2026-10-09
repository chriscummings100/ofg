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
#include <functional>

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
    TEST_CASE("Revision handoff preserves complete coverage through delay cancellation failure and queued updates")
    {
        int scenario = 0;
        SUBCASE("Held candidate cancels back to retained old roots")
        {
            scenario = 0;
        }
        SUBCASE("Newer publications wait for the active candidate")
        {
            scenario = 1;
        }
        SUBCASE("Failed candidate preserves old coverage and can retry")
        {
            scenario = 2;
        }
        SUBCASE("Insufficient minimal-cut budget preserves the old view")
        {
            scenario = 3;
        }
        const auto* base = std::getenv("OFG_TERRAIN_TEST_URL");
        REQUIRE(base);
        // Decode independently published revisions from the owned real HTTP service.
        const auto manifest = [&](const char* variable)
        {
            REQUIRE(std::getenv(variable));
            auto response = http(std::string(base) + std::getenv(variable));
            REQUIRE(response.status == 200);
            return std::make_shared<TerrainManifest>(
                decodeTerrainManifest({reinterpret_cast<const char*>(response.bytes.data()), response.bytes.size()})
            );
        };
        auto original = manifest("OFG_TERRAIN_TEST_MANIFEST");
        auto replacement = manifest("OFG_TERRAIN_TEST_REPLACEMENT");
        auto following = manifest("OFG_TERRAIN_TEST_FOLLOWING");
        StreamSettings settings;
        settings.rootWidth = original->rootWidth;
        settings.maximumDepth = scenario == 3 ? 0 : 1;
        settings.maximumPayloadBytes = 4 << 20;
        settings.maximumJobs = 8;
        settings.cpuBudget = settings.gpuBudget = (scenario == 3 ? 32ull : 96ull) << 20;
        GeneratorSettings generator;
        generator.rootWidth = settings.rootWidth;
        generator.intervals = 4;
        TerrainStreamingService
            service(settings, generator, 0, {base, std::getenv("OFG_TERRAIN_TEST_CACHE"), original, false});
        service.setObserver({{}, {0, 0, 0}});
        StreamingBatch latest;
        std::shared_ptr<const RenderSnapshot> active;
        std::map<RequestId, uint64_t> retired;
        std::set<RequestId> records;
        std::vector<std::string> seen;
        std::string failure;
        size_t candidateUploads = 0;
        // Model renderer adoption and completion independently of the coordinator's internal phase.
        const auto pump = [&]
        {
            latest = service.takeBatch();
            if (!latest.error.empty())
                failure = latest.error;
            for (auto& upload : latest.uploads)
            {
                candidateUploads += upload.source->revision == replacement->revisionBytes;
                ReadyContent content;
                content.id = upload.request.id;
                content.cpuBytes = upload.geometry.allocatedBytes();
                content.gpuBytes =
                    upload.geometry.vertices.size() * sizeof(Vertex) + upload.geometry.indices.size() * 4;
                content.source = upload.source;
                content.sourceGpuBytes = terrainTextureBytes;
                records.insert(content.id);
                service.uploadReady(content.id, content);
            }
            if (latest.snapshot)
            {
                active = latest.snapshot;
                service.selectionAdopted(active->revision);
                if (seen.empty() || seen.back() != active->manifest->revision)
                    seen.push_back(active->manifest->revision);
            }
            if (active)
                for (const auto& entry : active->leaves)
                {
                    if (!entry.payload->empty)
                        REQUIRE(records.contains(entry.payload->id));
                    if (entry.payload->source)
                        CHECK(entry.payload->source->revision == active->manifest->revisionBytes);
                }
            for (auto [id, serial] : latest.retirements)
                retired[id] = serial;
            for (auto i = retired.begin(); i != retired.end();)
            {
                if (active && active->revision >= i->second)
                {
                    for (const auto& entry : active->leaves)
                        REQUIRE(entry.payload->id != i->first);
                    records.erase(i->first);
                    service.payloadReleased(i->first);
                    i = retired.erase(i);
                }
                else
                    ++i;
            }
            const auto d = latest.diagnostics;
            REQUIRE(d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes <= settings.cpuBudget);
            REQUIRE(d.residentGpuBytes + d.reservedGpuBytes + d.retiredGpuBytes <= settings.gpuBudget);
        };
        // Explicit notifications drive progress; the deadline only detects a deadlock.
        const auto until = [&](const std::function<bool()>& predicate)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            for (;;)
            {
                const auto sequence = service.wakeSequence();
                pump();
                if (predicate())
                    return;
                REQUIRE(std::chrono::steady_clock::now() < deadline);
                REQUIRE(service.waitForChange(sequence, std::chrono::seconds(5)));
            }
        };
        until(
            [&]
            {
                return active && latest.diagnostics.admittedRoots == 8 && !latest.diagnostics.jobs &&
                       latest.diagnostics.planningIdle;
            }
        );
        service.injectNext(scenario < 2, scenario == 2);
        service.replaceRevision(replacement);
        if (scenario == 3)
        {
            until(
                [&]
                {
                    return !failure.empty() && !latest.replacing;
                }
            );
            CHECK(failure.find("both minimal root cuts") != std::string::npos);
            CHECK(active->manifest->revision == original->revision);
        }
        else if (scenario == 2)
        {
            until(
                [&]
                {
                    return latest.diagnostics.failed > 0;
                }
            );
            CHECK(active->manifest->revision == original->revision);
            service.retryFailures();
            until(
                [&]
                {
                    return active->manifest->revision == replacement->revision && !latest.replacing;
                }
            );
        }
        else
        {
            until(
                [&]
                {
                    return candidateUploads && latest.diagnostics.jobs == 1;
                }
            );
            REQUIRE(active->manifest->revision == original->revision);
            REQUIRE(active->leaves.size() == 8);
            service.setObserver({{1, 0, 1}, {0, 0, 0}});
            if (scenario == 0)
            {
                service.cancelReplacement();
                service.releaseHeld();
                until(
                    [&]
                    {
                        return !latest.replacing;
                    }
                );
                CHECK(active->manifest->revision == original->revision);
                CHECK(seen.size() == 1);
            }
            else
            {
                service.replaceRevision(following);
                service.releaseHeld();
                until(
                    [&]
                    {
                        return active->manifest->revision == following->revision && !latest.replacing;
                    }
                );
                REQUIRE(seen.size() == 3);
                CHECK(seen[1] == replacement->revision);
                CHECK(seen[2] == following->revision);
            }
        }
        service.stop();
        until(
            [&]
            {
                return latest.stopped;
            }
        );
        CHECK(records.empty());
        CHECK(latest.diagnostics.retiredCpuBytes == 0);
    }

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
