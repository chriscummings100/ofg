// Streaming contracts are tested through production requests and snapshots, with an independent grid oracle.
#include "doctest.h"
#include "terrain/terrain-stream.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <filesystem>
#include <fstream>

using namespace ofg::terrain;

namespace {
// Small byte limits make reservation failures deterministic without allocating large fixtures.
StreamSettings settings(size_t budget = 4096, uint8_t depth = 3)
{
    StreamSettings result;
    result.rootWidth = 8;
    result.maximumDepth = depth;
    result.maximumPayloadBytes = 8;
    result.cpuBudget = result.gpuBudget = budget;
    return result;
}

// Provides a symbolic renderer-prepared payload; real geometry is covered by meshing/GPU integration tests.
std::shared_ptr<PreparedPayload> payload(bool empty = false, bool certified = false)
{
    auto result = std::make_shared<PreparedPayload>();
    result->cpuBytes = result->gpuBytes = empty ? 0 : 1;
    result->empty = empty;
    result->certifiedEmpty = certified;
    return result;
}

// Finishes both I/O phases while preserving the production identity checks.
void finish(TerrainStream& stream, const BuildRequest& request, bool empty = false, bool certified = false)
{
    if (!empty)
    {
        REQUIRE(stream.acceptGenerated(request.id, 1));
    }
    REQUIRE(stream.complete(request.id, payload(empty, certified)));
}

// Counts finest-grid coverage without following controller nodes or using its ancestry helper.
bool coverageValid(
    const std::vector<CutEntry>& snapshot,
    const std::set<CellAddress>& expectedRoots,
    unsigned maximumDepth
)
{
    const unsigned side = 1u << maximumDepth;
    std::map<CellAddress, std::vector<unsigned>> grids;
    for (const auto& entry : snapshot)
    {
        if (entry.key.address.depth > maximumDepth || !expectedRoots.contains(entry.key.address.cell))
        {
            return false;
        }
        auto& grid = grids[entry.key.address.cell];
        if (grid.empty())
        {
            grid.resize(side * side * side);
        }
        const auto& a = entry.key.address;
        if (a.x >= (1u << a.depth) || a.y >= (1u << a.depth) || a.z >= (1u << a.depth))
        {
            return false;
        }
        const unsigned scale = 1u << (maximumDepth - a.depth);
        for (unsigned z = a.z * scale; z < (a.z + 1) * scale; ++z)
        {
            for (unsigned y = a.y * scale; y < (a.y + 1) * scale; ++y)
            {
                for (unsigned x = a.x * scale; x < (a.x + 1) * scale; ++x)
                {
                    ++grid[x + side * (y + side * z)];
                }
            }
        }
    }
    for (const auto& [cell, grid] : grids)
    {
        if (!std::all_of(
                grid.begin(),
                grid.end(),
                [](auto count)
                {
                    return count == 1;
                }
            ))
        {
            return false;
        }
    }
    return grids.size() == expectedRoots.size();
}

// Combines the independent spatial oracle with controller ownership/accounting checks after each event.
void checkCoverage(const TerrainStream& stream, unsigned maximumDepth = 3)
{
    std::set<CellAddress> roots;
    for (const auto& entry : stream.cut())
    {
        roots.insert(entry.key.address.cell);
    }
    REQUIRE(coverageValid(stream.cut(), roots, maximumDepth));
    CHECK(roots.size() == stream.diagnostics().admittedRoots);
    CHECK_NOTHROW(stream.validate());
}

// Loads a root without distance refinement; tests opt into individual splits explicitly.
NodeAddress root(TerrainStream& stream, CellAddress cell = {})
{
    const NodeAddress address{cell};
    stream.requestRoot(cell);
    stream.setRefinement(address, false);
    stream.update();
    const auto requests = stream.takeRequests();
    REQUIRE(requests.size() == 1);
    finish(stream, requests[0]);
    stream.update();
    checkCoverage(stream);
    return address;
}

// Advances fair successful I/O until a fixed desired cut stops requesting work.
void settle(TerrainStream& stream, unsigned limit = 64)
{
    for (unsigned i = 0; i < limit; ++i)
    {
        stream.update();
        for (auto id : stream.takeCancellations())
        {
            stream.acknowledgeCancellation(id);
        }
        auto requests = stream.takeRequests();
        for (const auto& request : requests)
        {
            finish(stream, request);
        }
        checkCoverage(stream);
        if (requests.empty() && !stream.diagnostics().jobs)
        {
            stream.update();
            return;
        }
    }
    FAIL("Streaming failed to settle under successful finite work");
}

// Keeps descendants coarse so a test exercises exactly one sibling replacement.
std::vector<BuildRequest> split(TerrainStream& stream, NodeAddress address)
{
    stream.setRefinement(address, true);
    for (const auto& child : childAddresses(address))
    {
        stream.setRefinement(child, false);
    }
    stream.update();
    return stream.takeRequests();
}
} // namespace

TEST_SUITE("terrain-fast")
{
    TEST_CASE("S01 S05 S06 root requires prepared geometry")
    {
        TerrainStream stream(settings());
        stream.requestRoot({});
        stream.setRefinement({}, false);
        CHECK(stream.state({}) == BuildState::Pending);
        stream.update();
        auto jobs = stream.takeRequests();
        REQUIRE(jobs.size() == 1);
        CHECK(stream.state({}) == BuildState::Loading);
        CHECK(stream.cut().empty());
        CHECK(stream.acceptGenerated(jobs[0].id, 1));
        stream.update();
        CHECK(stream.cut().empty());
        CHECK(stream.takeRequests().empty());
        CHECK(stream.complete(jobs[0].id, payload()));
        stream.update();
        REQUIRE(stream.cut().size() == 1);
        checkCoverage(stream);
    }

    TEST_CASE("S02 S03 every child readiness subset preserves atomic coverage")
    {
        for (unsigned mask = 0; mask < 256; ++mask)
        {
            CAPTURE(mask);
            TerrainStream stream(settings());
            auto parent = root(stream);
            auto jobs = split(stream, parent);
            REQUIRE(jobs.size() == 8);
            for (unsigned child = 0; child < 8; ++child)
            {
                if (mask & (1u << child))
                {
                    finish(stream, jobs[child]);
                }
            }
            stream.update();
            CHECK(stream.cut().size() == (mask == 255 ? 8 : 1));
            checkCoverage(stream);
        }
    }

    TEST_CASE("S07 S08 S09 empty payloads and certified empty regions")
    {
        TerrainStream stream(settings());
        auto parent = root(stream);
        auto jobs = split(stream, parent);
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            finish(stream, jobs[i], i != 7, i == 0);
        }
        stream.update();
        REQUIRE(stream.cut().size() == 8);
        stream.setRefinement(jobs[0].key.address, true);
        stream.update();
        CHECK(stream.takeRequests().empty());
        stream.setRefinement(jobs[1].key.address, true);
        stream.update();
        CHECK_FALSE(stream.takeRequests().empty());
        checkCoverage(stream);
    }

    TEST_CASE("S10 S11 S12 independent refinement and retained parent coarsening")
    {
        TerrainStream stream(settings());
        auto parent = root(stream);
        auto jobs = split(stream, parent);
        for (const auto& job : jobs)
        {
            finish(stream, job);
        }
        stream.update();
        auto first = jobs[0].key.address;
        split(stream, first); // Fetching requests transfers ownership, so complete the retained identities below.
        // Newly requested variants include seams on neighboring siblings as well as the eight child bases.
        for (const auto& child : childAddresses(first))
        {
            stream.setRefinement(child, false);
        }
        // Restart this independent phase by explicitly completing every current slot identity.
        for (const auto& child : childAddresses(first))
        {
            for (uint8_t mask = 0; mask < 64; ++mask)
            {
                if (auto request = stream.request({child, mask}))
                {
                    finish(stream, {*request, {child, mask}, 8});
                }
            }
        }
        settle(stream);
        CHECK(stream.cut().size() == 15);
        stream.setRefinement(first, false);
        settle(stream);
        CHECK(stream.cut().size() == 8);
        stream.setRefinement(parent, false);
        settle(stream);
        REQUIRE(stream.cut().size() == 1);
        CHECK(stream.cut()[0].key.address == parent);
        CHECK(stream.state({parent, 0}) == BuildState::Loaded);
    }

    TEST_CASE("S14 S15 S16 S19 cancellation incarnation duplicate and retry")
    {
        TerrainStream stream(settings());
        stream.requestRoot({});
        stream.setRefinement({}, false);
        stream.update();
        const auto old = stream.takeRequests().at(0);
        stream.withdrawRoot({});
        stream.update();
        CHECK(stream.diagnostics().reservedCpuBytes == 8);
        stream.requestRoot({});
        stream.setRefinement({}, false);
        stream.update();
        const auto fresh = stream.takeRequests().at(0);
        CHECK_FALSE(old.id == fresh.id);
        CHECK_FALSE(stream.complete(old.id, payload(true)));
        CHECK(stream.request({}) == fresh.id);
        stream.fail(fresh.id, "injected failure");
        CHECK(stream.state({}) == BuildState::Failed);
        stream.update();
        CHECK(stream.takeRequests().empty());
        stream.retry({});
        stream.update();
        auto retry = stream.takeRequests().at(0);
        stream.fail(fresh.id, "duplicate old failure");
        CHECK(stream.request({}) == retry.id);
        finish(stream, retry);
        CHECK_FALSE(stream.complete(retry.id, payload(true)));
        stream.update();
        checkCoverage(stream);
    }

    TEST_CASE("S17 each failed child retains parent and does not retry automatically")
    {
        for (size_t failed = 0; failed < 8; ++failed)
        {
            TerrainStream stream(settings());
            auto parent = root(stream);
            auto jobs = split(stream, parent);
            for (size_t i = 0; i < jobs.size(); ++i)
            {
                if (i == failed)
                {
                    stream.fail(jobs[i].id, "fixture failure");
                }
                else
                {
                    finish(stream, jobs[i]);
                }
            }
            stream.update();
            REQUIRE(stream.cut().size() == 1);
            CHECK(stream.cut()[0].key.address == parent);
            CHECK(stream.takeRequests().empty());
            stream.retry(jobs[failed].key);
            settle(stream);
            CHECK(stream.cut().size() == 8);
        }
    }

    TEST_CASE("S28 S29 S37 whole replacement reservation and budget boundaries")
    {
        for (size_t budget : {size_t(0), size_t(7), size_t(8)})
        {
            TerrainStream stream(settings(budget));
            stream.requestRoot({});
            stream.setRefinement({}, false);
            stream.update();
            CHECK(stream.takeRequests().size() == (budget == 8 ? 1 : 0));
            checkCoverage(stream);
        }
        for (size_t budget : {size_t(64), size_t(65)})
        {
            TerrainStream stream(settings(budget));
            auto parent = root(stream);
            auto jobs = split(stream, parent);
            CHECK(jobs.size() == (budget == 65 ? 8 : 0));
            CHECK(stream.diagnostics().jobs == jobs.size());
            checkCoverage(stream);
        }
    }

    TEST_CASE("S30 oversized generation does not publish")
    {
        TerrainStream stream(settings());
        stream.requestRoot({});
        stream.update();
        auto job = stream.takeRequests().at(0);
        CHECK_FALSE(stream.acceptGenerated(job.id, 9));
        CHECK(stream.state({}) == BuildState::Failed);
        CHECK(stream.diagnostics().jobs == 0);
        CHECK(stream.cut().empty());
    }

    TEST_CASE("S31 S32 S33 S34 retirement waits for GPU completion and snapshot ownership")
    {
        TerrainStream stream(settings());
        root(stream);
        auto snapshot = stream.cut();
        std::weak_ptr<PreparedPayload> weak = snapshot[0].payload;
        stream.submitted(snapshot, 1);
        stream.withdrawRoot({});
        stream.update();
        CHECK(stream.cut().empty());
        CHECK(stream.diagnostics().retiredGpuBytes == 1);
        stream.completedSubmission(1);
        CHECK_FALSE(weak.expired());
        snapshot.clear();
        stream.completedSubmission(1);
        CHECK(weak.expired());
        CHECK(stream.diagnostics().retiredGpuBytes == 0);
    }

    TEST_CASE("S27 S36 reset isolates epochs and settled terrain performs no work")
    {
        TerrainStream stream(settings());
        root(stream);
        const auto publications = stream.diagnostics().publications;
        for (int i = 0; i < 20; ++i)
        {
            stream.update();
            CHECK(stream.takeRequests().empty());
            CHECK(stream.diagnostics().publications == publications);
        }
        stream.reset();
        stream.update();
        CHECK(stream.diagnostics().nodes == 0);
        CHECK(stream.diagnostics().residentCpuBytes == 0);
        CHECK(stream.diagnostics().retiredCpuBytes == 0);
    }

    TEST_CASE("S39 exact negative and distant addressing")
    {
        auto p = normalizePosition({{0, 0, 0}, {-0.25, 8, -8.25}}, 8);
        CHECK(p.cell == CellAddress{-1, 1, -2});
        CHECK(p.local == std::array<double, 3>{7.75, 0, 7.75});
        const CellAddress distant{1000000000000, -1000000000000, 1000000000000};
        auto children = childAddresses({distant});
        for (unsigned i = 0; i < 8; ++i)
        {
            CHECK(parentAddress(children[i]) == NodeAddress{distant});
            CHECK(isAncestor({distant}, children[i]));
            CHECK(children[i].x == (i & 1));
            CHECK(children[i].y == ((i >> 1) & 1));
            CHECK(children[i].z == ((i >> 2) & 1));
        }
        CHECK(sharedFace(children[0], children[1]) == 1);
        CHECK(sharedFace(children[0], children[3]) == -1);
        CHECK(sharedFace(NodeAddress{{INT64_MAX, 0, 0}}, NodeAddress{{INT64_MIN, 0, 0}}) == -1);
        CHECK(relativeMinimum(children[7], {distant, {1, 2, 3}}, 8) == std::array<double, 3>{3, 2, 1});
    }

    TEST_CASE("S13 hysteresis preserves demand at both thresholds")
    {
        TerrainStream stream(settings());
        auto parent = root(stream);
        stream.setRefinement(parent, {});
        stream.setObserver({{}, {100, 4, 4}});
        stream.update();
        stream.setObserver({{}, {24, 4, 4}}); // Root max X=8: exactly two widths away.
        stream.update();
        CHECK(stream.takeRequests().empty());
        stream.setObserver({{}, {23.99, 4, 4}});
        auto children = childAddresses(parent);
        for (auto child : children)
        {
            stream.setRefinement(child, false);
        }
        settle(stream);
        CHECK(stream.cut().size() == 8);
        stream.setObserver({{}, {28, 4, 4}}); // Exactly 2.5 widths away preserves split demand.
        stream.update();
        CHECK(stream.cut().size() == 8);
        stream.setObserver({{}, {28.01, 4, 4}});
        settle(stream);
        CHECK(stream.cut().size() == 1);
    }

    TEST_CASE("S18 upload failure after worker success cannot be confused with a duplicate worker failure")
    {
        TerrainStream stream(settings());
        stream.requestRoot({});
        stream.setRefinement({}, false);
        stream.update();
        auto job = stream.takeRequests().at(0);
        REQUIRE(stream.acceptGenerated(job.id, 1));
        stream.fail(job.id, "duplicate worker failure");
        CHECK(stream.request({}) == job.id);
        CHECK(stream.state({}) == BuildState::Loading);
        stream.fail(job.id, "upload allocation failure", FailureStage::Upload);
        CHECK(stream.state({}) == BuildState::Failed);
        CHECK(stream.cut().empty());
        CHECK(stream.diagnostics().jobs == 0);
    }

    TEST_CASE("S20 S21 S22 cross root refinement waits for balancing and correct boundary variants")
    {
        TerrainStream stream(settings(8192));
        auto a = root(stream);
        auto b = root(stream, {1, 0, 0});
        for (auto child : childAddresses(b))
        {
            stream.setRefinement(child, false);
        }
        auto initial = split(stream, a);
        for (const auto& job : initial)
        {
            finish(stream, job);
        }
        settle(stream);
        REQUIRE(stream.cut().size() == 9);

        auto rightChild = childAddresses(a)[1];
        const auto jobs = split(stream, rightChild);
        REQUIRE(jobs.size() >= 16); // Neighbor's full sibling group plus the requested child group.
        std::vector<BuildRequest> boundaries;
        for (const auto& job : jobs)
        {
            if (job.key.transitionFaces)
            {
                boundaries.push_back(job);
            }
            else
            {
                finish(stream, job);
            }
        }
        REQUIRE_FALSE(boundaries.empty());
        stream.update();
        CHECK(stream.cut().size() == 9);
        checkCoverage(stream);
        for (const auto& job : boundaries)
        {
            finish(stream, job);
        }
        settle(stream);
        CHECK(stream.cut().size() == 23);
        checkCoverage(stream);

        // New neighbor demand can replace previously valid masks; the oracle checks actual adjacency each frame.
        for (const auto& entry : stream.cut())
        {
            if (entry.key.address.depth == 2)
            {
                stream.setRefinement(parentAddress(entry.key.address), false);
            }
        }
        stream.setRefinement(a, false);
        stream.setRefinement(b, false);
        settle(stream);
        CHECK(stream.cut().size() == 2);
    }

    TEST_CASE("S23 S25 S26 independent coverage progresses while another split is held")
    {
        TerrainStream stream(settings());
        auto a = root(stream);
        const auto held = split(stream, a);
        REQUIRE(held.size() == 8);
        const CellAddress destination{1000000000, 0, -1000000000};
        stream.requestRoot(destination);
        stream.setRefinement({destination}, false);
        stream.update();
        auto jobs = stream.takeRequests();
        REQUIRE(jobs.size() == 1);
        finish(stream, jobs[0]);
        stream.update();
        CHECK(stream.diagnostics().admittedRoots == 2);
        stream.withdrawRoot(a.cell);
        stream.update();
        CHECK(stream.diagnostics().admittedRoots == 1);
        for (const auto& job : held)
        {
            CHECK_FALSE(stream.complete(job.id, payload(true)));
        }
        REQUIRE(stream.cut().size() == 1);
        CHECK(stream.cut()[0].key.address.cell == destination);
        checkCoverage(stream);
    }
}

TEST_SUITE("terrain-exhaustive")
{
    TEST_CASE("S04 all 40320 completion permutations")
    {
        std::array<unsigned, 8> order;
        std::iota(order.begin(), order.end(), 0);
        size_t permutations = 0;
        do
        {
            TerrainStream stream(settings(4096, 1));
            const auto parent = root(stream);
            const auto jobs = split(stream, parent);
            REQUIRE(jobs.size() == 8);
            for (size_t i = 0; i < 8; ++i)
            {
                finish(stream, jobs[order[i]]);
                stream.update();
                REQUIRE(stream.cut().size() == (i == 7 ? 8 : 1));
                checkCoverage(stream, 1);
            }
            ++permutations;
        }
        while (std::next_permutation(order.begin(), order.end()));
        CHECK(permutations == 40320);
    }

} // exhaustive suite
TEST_SUITE("terrain-randomized")
{
    TEST_CASE("Streaming randomized event traces preserve coverage identity and bounded residency")
    {
        struct ExternalJob
        {
            BuildRequest request;
            bool generated = false;
        };
        for (uint32_t seed = 0; seed < 32; ++seed)
        {
            CAPTURE(seed);
            const auto directory =
                std::filesystem::path(__FILE__).parent_path().parent_path() / "artifacts/terrain/traces";
            std::filesystem::create_directories(directory);
            std::ofstream trace(directory / ("seed-" + std::to_string(seed) + ".txt"));
            REQUIRE(bool(trace));
            trace << "seed " << seed << " operations 5000 depth 3 budget 512\n";
            std::mt19937 random(seed);
            TerrainStream stream(settings(512));
            root(stream);
            root(stream, {1, 0, 0});
            std::map<RequestId, ExternalJob> jobs;
            std::set<NodeAddress> touched{{}, NodeAddress{{1, 0, 0}}};
            std::set<ContentKey> failures;
            std::vector<RequestId> cancelled;
            for (unsigned operation = 0; operation < 5000; ++operation)
            {
                CAPTURE(operation);
                const auto choice = random() % 8;
                trace << operation << " choice " << choice;
                if (choice < 2 && !stream.cut().empty())
                {
                    auto address = stream.cut()[random() % stream.cut().size()].key.address;
                    trace << " node " << address.cell.x << ',' << address.cell.y << ',' << address.cell.z << '/'
                          << unsigned(address.depth) << '/' << address.x << ',' << address.y << ',' << address.z;
                    if (choice == 0 && address.depth < 3 && stream.cut().size() < 40)
                    {
                        stream.setRefinement(address, true);
                        for (const auto& child : childAddresses(address))
                        {
                            stream.setRefinement(child, false);
                            touched.insert(child);
                        }
                    }
                    else if (address.depth)
                    {
                        stream.setRefinement(parentAddress(address), false);
                    }
                    touched.insert(address);
                }
                else if (choice < 6 && !jobs.empty())
                {
                    auto entry = jobs.begin();
                    std::advance(entry, random() % jobs.size());
                    auto& job = entry->second;
                    trace << " job " << job.request.id.epoch << ':' << job.request.id.sequence << " generated "
                          << job.generated;
                    if (choice == 2 && !job.generated)
                    {
                        job.generated = stream.acceptGenerated(job.request.id, 1);
                        if (!job.generated)
                        {
                            jobs.erase(entry);
                        }
                    }
                    else if (choice == 3)
                    {
                        failures.insert(job.request.key);
                        stream.fail(
                            job.request.id,
                            "seeded failure",
                            job.generated ? FailureStage::Upload : FailureStage::Generation
                        );
                        jobs.erase(entry);
                    }
                    else
                    {
                        if (job.generated || stream.acceptGenerated(job.request.id, 1))
                        {
                            stream.complete(job.request.id, payload());
                        }
                        jobs.erase(entry);
                    }
                }
                else if (choice == 6 && !failures.empty())
                {
                    auto entry = failures.begin();
                    stream.retry(*entry);
                    failures.erase(entry);
                }
                else if (choice == 7 && !cancelled.empty())
                {
                    stream.acknowledgeCancellation(cancelled.back());
                    jobs.erase(cancelled.back());
                    cancelled.pop_back();
                }
                trace << '\n';
                stream.update();
                for (const auto& job : stream.takeRequests())
                {
                    jobs.emplace(job.id, ExternalJob{job});
                }
                for (const auto& id : stream.takeCancellations())
                {
                    cancelled.push_back(id);
                }
                checkCoverage(stream);
            }
            for (const auto& address : touched)
            {
                stream.setRefinement(address, false);
            }
            for (const auto& key : failures)
            {
                stream.retry(key);
            }
            stream.update();
            for (const auto& id : stream.takeCancellations())
            {
                stream.acknowledgeCancellation(id);
            }
            for (const auto& id : cancelled)
            {
                stream.acknowledgeCancellation(id);
            }
            for (const auto& [id, job] : jobs)
            {
                if (stream.request(job.request.key) == id)
                {
                    if (job.generated || stream.acceptGenerated(id, 1))
                    {
                        stream.complete(id, payload());
                    }
                }
            }
            settle(stream, 1024);
            CHECK(stream.cut().size() == 2);
            CHECK(stream.diagnostics().jobs == 0);
            CHECK(bool(trace));
        }
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Bounded planning preserves held dependencies and progresses independent roots")
    {
        auto config = settings();
        config.maximumPlansPerUpdate = 1;
        TerrainStream stream(config);
        const auto a = root(stream);
        auto held = split(stream, a);
        REQUIRE(held.size() == 8);
        stream.requestRoot({5, 0, 0});
        stream.setRefinement({{5, 0, 0}}, false);
        for (unsigned step = 0; step < 16; ++step)
        {
            stream.update();
            CHECK(stream.takeCancellations().empty());
            for (const auto& job : stream.takeRequests())
            {
                finish(stream, job);
            }
            checkCoverage(stream);
        }
        CHECK(stream.diagnostics().admittedRoots == 2);
        for (const auto& job : held)
        {
            finish(stream, job);
        }
        settle(stream);
        CHECK(stream.cut().size() == 9);
    }

    TEST_CASE("S18 S34 partial upload failure retains submitted memory until actual completion")
    {
        TerrainStream stream(settings());
        auto a = root(stream);
        const auto jobs = split(stream, a);
        auto partial = payload();
        REQUIRE(stream.acceptGenerated(jobs[0].id, partial->cpuBytes));
        stream.submitted({{jobs[0].key, partial}}, 1);
        stream.failUpload(jobs[0].id, partial, "injected partial upload allocation failure");
        stream.failUpload(jobs[0].id, partial, "duplicate upload failure");
        partial.reset();
        stream.update();
        CHECK(stream.cut().size() == 1);
        CHECK(stream.diagnostics().retiredGpuBytes == 1);
        stream.completedSubmission(0);
        CHECK(stream.diagnostics().retiredGpuBytes == 1);
        stream.completedSubmission(1);
        CHECK(stream.diagnostics().retiredGpuBytes == 0);
        checkCoverage(stream);
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("S24 S25 root admission beside detailed coverage and neighbor withdrawal remain compatible")
    {
        TerrainStream stream(settings(8192));
        auto a = root(stream);
        for (const auto& job : split(stream, a))
        {
            finish(stream, job);
        }
        settle(stream);
        const auto boundaryChild = childAddresses(a)[1];
        for (const auto& job : split(stream, boundaryChild))
        {
            finish(stream, job);
        }
        settle(stream);
        REQUIRE(stream.cut().size() == 15);
        stream.requestRoot({1, 0, 0});
        stream.setRefinement({{1, 0, 0}}, false);
        for (const auto& child : childAddresses({{1, 0, 0}}))
        {
            stream.setRefinement(child, false);
        }
        stream.update();
        const auto admission = stream.takeRequests();
        REQUIRE_FALSE(admission.empty());
        for (const auto& job : admission)
        {
            if (job.key.address.cell == CellAddress{1, 0, 0})
            {
                CHECK(job.key.address.depth == 0);
            }
        }
        for (size_t i = 0; i < admission.size(); ++i)
        {
            finish(stream, admission[i]);
            stream.update();
            if (i + 1 < admission.size())
            {
                CHECK(stream.diagnostics().admittedRoots == 1);
            }
            checkCoverage(stream);
        }
        settle(stream);
        CHECK(stream.diagnostics().admittedRoots == 2);
        CHECK(stream.cut().size() == 23);
        stream.withdrawRoot({1, 0, 0});
        settle(stream);
        CHECK(stream.diagnostics().admittedRoots == 1);
        CHECK(stream.cut().size() == 15);
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Independent coverage oracle rejects missing roots holes overlap and illegal addresses")
    {
        const std::set<CellAddress> expected{{}};
        std::vector<CutEntry> snapshot;
        for (const auto& child : childAddresses({}))
        {
            snapshot.push_back({{child}, payload()});
        }
        REQUIRE(coverageValid(snapshot, expected, 3));
        auto broken = snapshot;
        broken.pop_back();
        CHECK_FALSE(coverageValid(broken, expected, 3));
        broken.push_back(broken.front()); // Total volume is one; coverage still has a hole and overlap.
        CHECK_FALSE(coverageValid(broken, expected, 3));
        broken = snapshot;
        broken.push_back({{}, payload()});
        CHECK_FALSE(coverageValid(broken, expected, 3));
        CHECK_FALSE(coverageValid({}, expected, 3));
        broken = snapshot;
        broken.front().key.address.x = 2;
        CHECK_FALSE(coverageValid(broken, expected, 3));
        CHECK_FALSE(coverageValid(snapshot, {{1, 0, 0}}, 3));
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("S26 S28 root node capacity defers a teleport without destroying old coverage")
    {
        auto config = settings();
        config.maximumNodes = 1;
        TerrainStream stream(config);
        root(stream);
        CHECK_FALSE(stream.requestRoot({1000000000, 0, 0}));
        CHECK(stream.cut().size() == 1);
        checkCoverage(stream);
        stream.withdrawRoot({});
        stream.update();
        CHECK(stream.requestRoot({1000000000, 0, 0}));
        stream.setRefinement({{1000000000, 0, 0}}, false);
        settle(stream);
        CHECK(stream.cut().size() == 1);
        CHECK(stream.cut().front().key.address.cell.x == 1000000000);
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Root dispatch proceeds outward after each completion changes the candidate list")
    {
        auto config = settings();
        config.maximumPlansPerUpdate = 1;
        TerrainStream stream(config);
        const WorldPosition observer{{}, {4, 4, 4}};
        stream.setObserver(observer);
        for (CellAddress cell :
             {CellAddress{10, 0, 0}, CellAddress{0, 0, 0}, CellAddress{5, 0, 0}, CellAddress{-2, 0, 0}})
        {
            stream.requestRoot(cell);
            stream.setRefinement({cell}, false);
        }
        double previous = -1;
        size_t dispatched = 0;
        for (unsigned step = 0; step < 20; ++step)
        {
            stream.update();
            for (const auto& request : stream.takeRequests())
            {
                const auto distance = distanceToNode(request.key.address, observer, config.rootWidth);
                CHECK(distance >= previous);
                previous = distance;
                ++dispatched;
                finish(stream, request);
            }
        }
        CHECK(dispatched == 4);
        CHECK(stream.diagnostics().admittedRoots == 4);
    }

    TEST_CASE("Refinement dispatch proceeds outward in complete sibling groups as nearby cuts publish")
    {
        auto config = settings();
        config.maximumPlansPerUpdate = 1;
        config.maximumJobs = 8;
        TerrainStream stream(config);
        const WorldPosition observer{{}, {4, 4, 4}};
        stream.setObserver(observer);
        const CellAddress cells[] = {{10, 0, 0}, {0, 0, 0}, {5, 0, 0}, {-2, 0, 0}};
        for (const auto& cell : cells)
        {
            root(stream, cell);
        }
        for (const auto& cell : cells)
        {
            stream.setRefinement({cell}, true);
            for (const auto& child : childAddresses({cell}))
            {
                stream.setRefinement(child, false);
            }
        }
        double previous = -1;
        size_t dispatched = 0;
        for (unsigned step = 0; step < 32; ++step)
        {
            stream.update();
            const auto requests = stream.takeRequests();
            if (!requests.empty())
            {
                REQUIRE(requests.size() == 8);
            }
            for (const auto& request : requests)
            {
                REQUIRE(request.key.address.depth == 1);
                const auto distance = distanceToNode(parentAddress(request.key.address), observer, config.rootWidth);
                CHECK(distance >= previous);
                previous = distance;
                ++dispatched;
                finish(stream, request);
            }
            checkCoverage(stream);
        }
        CHECK(dispatched == 32);
        CHECK(stream.cut().size() == 32);
    }

    TEST_CASE("Balanced replacement builds in sibling waves when its closure exceeds the job limit")
    {
        auto config = settings(1024);
        config.maximumJobs = 8;
        TerrainStream stream(config);
        const auto a = root(stream);
        const auto b = root(stream, {1, 0, 0});
        for (const auto& child : childAddresses(b))
        {
            stream.setRefinement(child, false);
        }
        for (const auto& request : split(stream, a))
        {
            finish(stream, request);
        }
        settle(stream);
        REQUIRE(stream.cut().size() == 9);
        for (const auto& request : split(stream, childAddresses(a)[1]))
        {
            finish(stream, request);
        }
        for (unsigned step = 0; step < 64 && stream.cut().size() != 23; ++step)
        {
            stream.update();
            checkCoverage(stream);
            CHECK(stream.diagnostics().jobs <= 8);
            for (const auto& request : stream.takeRequests())
            {
                finish(stream, request);
            }
        }
        CHECK(stream.cut().size() == 23);
        checkCoverage(stream);
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Terminal failure of a cancelled job wakes work blocked on its reservation")
    {
        TerrainStream stream(settings(8));
        stream.requestRoot({});
        stream.setRefinement({}, false);
        stream.update();
        const auto obsolete = stream.takeRequests().at(0);
        stream.withdrawRoot({});
        stream.requestRoot({10, 0, 0});
        stream.setRefinement({{10, 0, 0}}, false);
        stream.update();
        REQUIRE(stream.takeRequests().empty());
        stream.fail(obsolete.id, "Worker failed after cancellation");
        stream.update();
        const auto requests = stream.takeRequests();
        REQUIRE(requests.size() == 1);
        CHECK(requests[0].key.address.cell == CellAddress{10, 0, 0});
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Continuous observer motion cannot starve bottom up merges behind an ineligible ancestor")
    {
        auto config = settings();
        config.maximumPlansPerUpdate = 1;
        TerrainStream stream(config);
        const auto parent = root(stream);
        for (const auto& job : split(stream, parent))
        {
            finish(stream, job);
        }
        settle(stream);
        const auto child = childAddresses(parent)[0];
        for (const auto& job : split(stream, child))
        {
            finish(stream, job);
        }
        settle(stream);
        REQUIRE(stream.cut().size() == 15);
        stream.setRefinement(parent, false);
        stream.setRefinement(child, false);
        for (unsigned frame = 0; frame < 32 && stream.cut().size() != 1; ++frame)
        {
            stream.setObserver({{10, 10, 10}, {1 + frame * .001, 1, 1}});
            stream.update();
            for (const auto& request : stream.takeRequests())
            {
                finish(stream, request);
            }
            checkCoverage(stream);
        }
        CHECK(stream.cut().size() == 1);
    }
}
