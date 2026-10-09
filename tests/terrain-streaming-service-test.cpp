// Real coordinator/mesher threads exercise value handoff, coalesced observers, cancellation and retirement.
#include "doctest.h"
#include "terrain/terrain-streaming-service.h"
#include <latch>
#include <functional>

using namespace ofg::terrain;
namespace {
// Keeps real meshing cheap while preserving sibling readiness and multi-level streaming behavior.
StreamSettings serviceSettings()
{
    StreamSettings settings;
    settings.rootWidth = 8;
    settings.maximumDepth = 2;
    settings.maximumPayloadBytes = 1 << 20;
    settings.cpuBudget = settings.gpuBudget = 32 << 20;
    settings.maximumJobs = 8;
    settings.maximumNodes = 1024;
    return settings;
}
// A plane crossing the root produces real geometry and certified empty descendants.
GeneratorSettings serviceGenerator()
{
    GeneratorSettings generator;
    generator.rootWidth = 8;
    generator.heightOffset = 3;
    generator.amplitude = 0;
    generator.intervals = 4;
    return generator;
}
struct ServiceFixture
{
    TerrainStreamingService service{serviceSettings(), serviceGenerator(), 0};
    StreamingBatch latest;
    std::shared_ptr<const RenderSnapshot> active;
    std::map<RequestId, ReadyContent> records;
    std::map<RequestId, uint64_t> retired;
    bool uploadEnabled = true, releaseEnabled = true;

    // Models renderer ownership without a GPU; releaseEnabled is an explicitly controlled fence.
    void pump()
    {
        latest = service.takeBatch();
        REQUIRE_MESSAGE(latest.error.empty(), latest.error);
        for (auto& upload : latest.uploads)
        {
            ReadyContent content;
            content.id = upload.request.id;
            content.cpuBytes = upload.geometry.allocatedBytes();
            content.gpuBytes = upload.geometry.indices.size() * sizeof(uint32_t) +
                               upload.geometry.vertices.size() * sizeof(ofg::Vertex);
            records.emplace(content.id, content);
            if (uploadEnabled)
                service.uploadReady(content.id, content);
        }
        if (latest.snapshot)
            active = latest.snapshot;
        for (auto [id, revision] : latest.retirements)
            retired[id] = revision;
        if (active)
        {
            for (const auto& leaf : active->leaves)
            {
                if (!leaf.payload->empty)
                    REQUIRE(records.contains(leaf.payload->id));
            }
        }
        if (releaseEnabled)
        {
            for (auto i = retired.begin(); i != retired.end();)
            {
                if (active && active->revision >= i->second)
                {
                    const auto id = i->first;
                    for (const auto& leaf : active->leaves)
                        REQUIRE(leaf.payload->id != id);
                    records.erase(id);
                    service.payloadReleased(id);
                    i = retired.erase(i);
                }
                else
                    ++i;
            }
        }
    }
    // Waits for actual events, with a wall-clock guard solely to report a deadlock instead of hanging CTest.
    void until(const std::function<bool()>& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;)
        {
            const auto sequence = service.wakeSequence();
            pump();
            if (predicate())
                return;
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            REQUIRE(service.waitForChange(sequence, std::chrono::seconds(3)));
        }
    }
};
} // namespace
TEST_SUITE("terrain-fast")
{
    TEST_CASE("Synchronous and threaded reconciliation converge to the same geometry coverage and residency")
    {
        ServiceFixture fixture;
        TerrainStream synchronous(serviceSettings());
        REQUIRE(synchronous.requestRoot({}));
        for (const auto& local : {std::array<double, 3>{4, 4, 4}, {1, 7, 1}, {7, 1, 7}})
        {
            const WorldPosition observer{{}, local};
            synchronous.setObserver(observer);
            for (unsigned pass = 0; pass < 100; ++pass)
            {
                synchronous.update();
                for (const auto id : synchronous.takeCancellations())
                    synchronous.acknowledgeCancellation(id);
                for (const auto& [id, revision] : synchronous.takeRetirements())
                    synchronous.releasePayload(id);
                for (const auto& request : synchronous.takeRequests())
                {
                    TerrainMesher mesher(request.address, serviceGenerator(), request.byteLimit);
                    while (!mesher.step())
                    {
                    }
                    const auto geometry = mesher.takeGeometry();
                    ReadyContent content;
                    content.empty = geometry.indices.empty();
                    content.certifiedEmpty = geometry.certifiedEmpty;
                    if (!content.empty)
                    {
                        content.cpuBytes = geometry.allocatedBytes();
                        content.gpuBytes =
                            geometry.indices.size() * sizeof(uint32_t) + geometry.vertices.size() * sizeof(ofg::Vertex);
                        REQUIRE(synchronous.acceptGenerated(request.id, content.cpuBytes));
                    }
                    REQUIRE(synchronous.complete(request.id, content));
                }
                if (synchronous.diagnostics().planningIdle)
                    break;
            }
            REQUIRE(synchronous.diagnostics().planningIdle);
            fixture.service.setObserver(observer);
            // A gate acknowledges consumption of this observer before accepting an idle diagnostic.
            fixture.service.holdReconciliation(true);
            fixture.until(
                [&]
                {
                    return fixture.latest.paused;
                }
            );
            fixture.service.holdReconciliation(false);
            fixture.until(
                [&]
                {
                    return !fixture.latest.paused && fixture.latest.diagnostics.planningIdle;
                }
            );
            REQUIRE(fixture.active);
            const auto& actual = fixture.active->leaves;
            const auto& expected = synchronous.cut();
            REQUIRE(actual.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i)
            {
                CHECK(actual[i].address == expected[i].address);
                CHECK(actual[i].transitionFaces == expected[i].transitionFaces);
                CHECK(actual[i].payload->empty == expected[i].payload->empty);
            }
            CHECK(fixture.latest.diagnostics.nodes == synchronous.diagnostics().nodes);
            CHECK(fixture.latest.diagnostics.residentCpuBytes == synchronous.diagnostics().residentCpuBytes);
            CHECK(fixture.latest.diagnostics.residentGpuBytes == synchronous.diagnostics().residentGpuBytes);
        }
        fixture.service.stop();
        fixture.until(
            [&]
            {
                return fixture.latest.stopped;
            }
        );
        CHECK(fixture.records.empty());
    }
    TEST_CASE("Service reset rejects an incompatible generator before changing the epoch")
    {
        ServiceFixture fixture;
        auto generator = serviceGenerator();
        generator.rootWidth *= 2;
        CHECK_THROWS(fixture.service.reset(generator));
        fixture.service.stop();
        fixture.until(
            [&]
            {
                return fixture.latest.stopped;
            }
        );
    }
    TEST_CASE("T3 sequence notifications survive signals before waiting and at the wait boundary")
    {
        StreamingWake wake;
        auto sequence = wake.sequence();
        wake.signal();
        CHECK(wake.waitFor(sequence, std::chrono::milliseconds(0)));
        sequence = wake.sequence();
        std::latch ready(1);
        bool received = false;
        std::thread waiter(
            [&]
            {
                ready.count_down();
                received = wake.waitFor(sequence, std::chrono::seconds(3));
            }
        );
        ready.wait();
        wake.signal();
        waiter.join();
        CHECK(received);
        CHECK_FALSE(wake.waitFor(wake.sequence(), std::chrono::milliseconds(0)));
    }
    TEST_CASE("T1 T2 real streaming service retains renderer leases across snapshot changes")
    {
        ServiceFixture fixture;
        fixture.service.setObserver({{}, {4, 4, 4}});
        fixture.until(
            [&]
            {
                return fixture.latest.diagnostics.selected > 1 && fixture.latest.diagnostics.planningIdle;
            }
        );
        const auto old = fixture.active;
        REQUIRE(old);
        REQUIRE_FALSE(fixture.records.empty());
        fixture.releaseEnabled = false;
        fixture.service.setObserver({{0, 100, 0}, {4, 4, 4}});
        fixture.until(
            [&]
            {
                return fixture.active->leaves.empty() && !fixture.retired.empty();
            }
        );
        CHECK_FALSE(old->leaves.empty());
        CHECK_FALSE(fixture.records.empty());
        CHECK(fixture.latest.diagnostics.retiredGpuBytes > 0);
        fixture.releaseEnabled = true;
        fixture.service.stop();
        fixture.until(
            [&]
            {
                return fixture.latest.stopped;
            }
        );
        CHECK(fixture.records.empty());
        CHECK(fixture.retired.empty());
        CHECK(fixture.latest.diagnostics.jobs == 0);
        CHECK(fixture.latest.diagnostics.retiredGpuBytes == 0);
    }
    TEST_CASE("T2 T3 held traversal preserves immutable coverage and consumes the latest observer on release")
    {
        ServiceFixture fixture;
        fixture.service.setObserver({{}, {4, 4, 4}});
        fixture.until(
            [&]
            {
                return fixture.latest.diagnostics.selected > 1 && fixture.latest.diagnostics.planningIdle;
            }
        );
        const auto previous = fixture.active;
        fixture.service.holdReconciliation(true);
        fixture.until(
            [&]
            {
                return fixture.latest.paused;
            }
        );
        for (int i = 0; i < 100; ++i)
        {
            fixture.service.setObserver({{i + 1, 0, 0}, {4, 4, 4}});
            fixture.pump();
            CHECK(fixture.active == previous);
        }
        fixture.service.holdReconciliation(false);
        fixture.until(
            [&]
            {
                return !fixture.latest.paused && fixture.active->revision > previous->revision &&
                       fixture.latest.diagnostics.planningIdle && !fixture.active->leaves.empty();
            }
        );
        for (const auto& leaf : fixture.active->leaves)
            CHECK(leaf.address.cell.x == 100);
        fixture.service.stop();
        fixture.until(
            [&]
            {
                return fixture.latest.stopped;
            }
        );
    }
    TEST_CASE("T4 reset detaches in-progress uploads and rejects later readiness from the old epoch")
    {
        ServiceFixture fixture;
        fixture.uploadEnabled = false;
        fixture.service.setObserver({{}, {4, 4, 4}});
        fixture.until(
            [&]
            {
                return !fixture.records.empty();
            }
        );
        const auto old = fixture.records.begin()->second;
        auto generator = serviceGenerator();
        ++generator.seed;
        fixture.service.reset(generator);
        fixture.service.setObserver({{}, {4, 4, 4}});
        fixture.uploadEnabled = true;
        fixture.service.uploadReady(old.id, old);
        fixture.until(
            [&]
            {
                return fixture.latest.diagnostics.selected > 1 && fixture.latest.diagnostics.planningIdle;
            }
        );
        for (const auto& leaf : fixture.active->leaves)
            CHECK(leaf.payload->id.epoch != old.id.epoch);
        fixture.service.stop();
        fixture.until(
            [&]
            {
                return fixture.latest.stopped;
            }
        );
        CHECK(fixture.records.empty());
    }
    TEST_CASE("T5 shutdown cancels a held real mesher and a replacement service converges")
    {
        for (unsigned iteration = 0; iteration < 2; ++iteration)
        {
            ServiceFixture fixture;
            fixture.service.injectNext(true, false);
            fixture.service.setObserver({{}, {4, 4, 4}});
            fixture.until(
                [&]
                {
                    return fixture.latest.diagnostics.jobs == 1;
                }
            );
            CHECK(fixture.latest.diagnostics.selected == 0);
            if (iteration)
            {
                fixture.service.releaseHeld();
                fixture.until(
                    [&]
                    {
                        return fixture.latest.diagnostics.selected > 1 && fixture.latest.diagnostics.planningIdle;
                    }
                );
            }
            fixture.service.stop();
            fixture.until(
                [&]
                {
                    return fixture.latest.stopped;
                }
            );
            CHECK(fixture.records.empty());
        }
    }
}
