// Real native-thread tests use held jobs and condition variables, never sleeps as evidence of concurrency.
#include "doctest.h"
#include "terrain/terrain-workers.h"

using namespace ofg::terrain;

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Native terrain workers finish independent jobs while another job is held")
    {
        TerrainWorkers workers;
        GeneratorSettings settings;
        settings.intervals = 8;
        const BuildRequest held{{1, 1}, {}, 4 << 20};
        const BuildRequest free{{1, 2}, {}, 4 << 20};
        workers.submit(held, settings, true);
        workers.submit(free, settings);
        REQUIRE(workers.waitForResult(std::chrono::seconds(10)));
        auto result = workers.takeResults();
        REQUIRE(result.size() == 1);
        CHECK(result[0].request.id == free.id);
        CHECK(result[0].outcome == WorkerOutcome::Geometry);
        CHECK_FALSE(result[0].geometry.indices.empty());
        workers.releaseHeld();
        REQUIRE(workers.waitForResult(std::chrono::seconds(10)));
        auto second = workers.takeResults();
        REQUIRE(second.size() == 1);
        CHECK(second[0].request.id == held.id);
        CHECK(second[0].worker != result[0].worker);
    }

    TEST_CASE("S38 native worker cancellation and held-job shutdown release their tasks")
    {
        TerrainWorkers workers;
        GeneratorSettings settings;
        const BuildRequest request{{1, 1}, {}, 4 << 20};
        workers.submit(request, settings, true);
        workers.cancel(request.id);
        REQUIRE(workers.waitForResult(std::chrono::seconds(10)));
        auto result = workers.takeResults();
        REQUIRE(result.size() == 1);
        CHECK(result[0].outcome == WorkerOutcome::Cancelled);
        CHECK(result[0].geometry.allocatedBytes() == 0);
        workers.submit({{1, 2}, {}, 4 << 20}, settings, true);
        // Destruction must wake and join the held job; CTest's timeout detects a broken shutdown.
    }

    TEST_CASE("Native worker exceptions produce terminal diagnostics")
    {
        TerrainWorkers workers;
        workers.submit({{1, 1}, {}, 4 << 20}, {}, false, true);
        REQUIRE(workers.waitForResult(std::chrono::seconds(10)));
        auto result = workers.takeResults();
        REQUIRE(result.size() == 1);
        CHECK(result[0].outcome == WorkerOutcome::Failed);
        CHECK_FALSE(result[0].error.empty());
    }
}
