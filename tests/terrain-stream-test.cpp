// Streaming contracts are tested through production requests and snapshots, with an independent grid oracle.
#include "doctest.h"
#include "terrain/terrain-stream.h"
#include "terrain/terrain-profile.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <filesystem>
#include <fstream>

using namespace ofg::terrain;

TEST_CASE("Cancelled revision handoff restores only retained roots" * doctest::test_suite("terrain-fast"))
{
    StreamSettings settings;
    settings.maximumDepth = 0;
    TerrainStream stream(settings);
    REQUIRE(stream.requestRoot({}));
    stream.update();
    const auto first = stream.takeRequests().at(0);
    REQUIRE(stream.acceptGenerated(first.id, 10));
    REQUIRE(stream.complete(first.id, {first.id, 10, 20}));
    stream.update();
    const auto retained = stream.cut();
    stream.reset();
    CHECK(stream.diagnostics().retiredCpuBytes == 10);
    stream.restoreRoots(retained);
    stream.update();
    REQUIRE(stream.cut().size() == 1);
    CHECK(stream.cut()[0].payload->id == first.id);
    CHECK(stream.takeRequests().empty());
    CHECK(stream.takeRetirements().empty());
    CHECK(stream.diagnostics().residentCpuBytes == 10);
    CHECK(stream.diagnostics().retiredCpuBytes == 0);
    CHECK_NOTHROW(stream.validate());
    CHECK_THROWS(stream.restoreRoots(retained));
    stream.reset();
    stream.releasePayload(first.id);
    CHECK_THROWS(stream.restoreRoots(retained));
}

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
std::shared_ptr<ReadyContent> payload(bool empty = false, bool certified = false)
{
    auto result = std::make_shared<ReadyContent>();
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
    REQUIRE(stream.complete(request.id, *payload(empty, certified)));
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
        if (entry.address.depth > maximumDepth || !expectedRoots.contains(entry.address.cell))
        {
            return false;
        }
        auto& grid = grids[entry.address.cell];
        if (grid.empty())
        {
            grid.resize(side * side * side);
        }
        const auto& a = entry.address;
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

// Independently compares exact finest-grid AABBs; no production adjacency or ancestry helpers are used.
bool topologyValid(const std::vector<CutEntry>& snapshot, unsigned maximumDepth)
{
    struct Box
    {
        std::array<int64_t, 3> minimum, maximum;
    };
    std::vector<Box> boxes;
    const int64_t side = int64_t(1) << maximumDepth;
    for (const auto& entry : snapshot)
    {
        if (!entry.payload || entry.address.depth > maximumDepth)
            return false;
        const int64_t scale = int64_t(1) << (maximumDepth - entry.address.depth);
        const auto& a = entry.address;
        Box box{{a.cell.x * side + a.x * scale, a.cell.y * side + a.y * scale, a.cell.z * side + a.z * scale}, {}};
        for (unsigned axis = 0; axis < 3; ++axis)
            box.maximum[axis] = box.minimum[axis] + scale;
        boxes.push_back(box);
    }
    std::vector<uint8_t> masks(snapshot.size());
    for (size_t i = 0; i < boxes.size(); ++i)
        for (size_t j = i + 1; j < boxes.size(); ++j)
        {
            const auto& a = boxes[i];
            const auto& b = boxes[j];
            std::array<bool, 3> overlap;
            for (unsigned axis = 0; axis < 3; ++axis)
                overlap[axis] = std::max(a.minimum[axis], b.minimum[axis]) < std::min(a.maximum[axis], b.maximum[axis]);
            if (overlap[0] && overlap[1] && overlap[2])
                return false;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                if (!overlap[(axis + 1) % 3] || !overlap[(axis + 2) % 3])
                    continue;
                const bool positive = a.maximum[axis] == b.minimum[axis];
                if (!positive && a.minimum[axis] != b.maximum[axis])
                    continue;
                const int delta = int(snapshot[i].address.depth) - int(snapshot[j].address.depth);
                if (std::abs(delta) > 1)
                    return false;
                if (delta == 1)
                    masks[i] |= uint8_t(1u << (2 * axis + positive));
                if (delta == -1)
                    masks[j] |= uint8_t(1u << (2 * axis + !positive));
            }
        }
    for (size_t i = 0; i < snapshot.size(); ++i)
        if (snapshot[i].transitionFaces != masks[i])
            return false;
    return true;
}

// Combines the independent spatial oracle with controller ownership/accounting checks after each event.
void checkCoverage(const TerrainStream& stream, unsigned maximumDepth = 3)
{
    std::set<CellAddress> roots;
    for (const auto& entry : stream.cut())
    {
        roots.insert(entry.address.cell);
    }
    REQUIRE(coverageValid(stream.cut(), roots, maximumDepth));
    REQUIRE(topologyValid(stream.cut(), maximumDepth));
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
        for (auto [id, revision] : stream.takeRetirements())
            stream.releasePayload(id);
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
    TEST_CASE("S9 a full generation queue cannot truncate demand after observer movement")
    {
        auto config = settings(4096, 1);
        config.maximumJobs = 8;
        TerrainStream stream(config);
        const auto a = root(stream);
        const auto b = root(stream, {5, 0, 0});
        stream.setRefinement(b, {});
        const auto held = split(stream, a);
        REQUIRE(held.size() == 8);
        stream.setObserver({{5, 0, 0}, {4, 4, 4}});
        stream.update();
        CHECK(stream.takeRequests().empty());
        CHECK(stream.takeCancellations().empty());
        CHECK(stream.diagnostics().nodes == 18);
        for (const auto& child : childAddresses(b))
            CHECK(stream.state(child) == BuildState::Pending);
        for (const auto& job : held)
            finish(stream, job);
        stream.update();
        const auto next = stream.takeRequests();
        REQUIRE(next.size() == 8);
        for (const auto& job : next)
        {
            CHECK(parentAddress(job.address) == b);
            finish(stream, job);
        }
        settle(stream);
        CHECK(stream.cut().size() == 16);
    }
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
        CHECK(stream.complete(jobs[0].id, *payload()));
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
        stream.setRefinement(jobs[0].address, true);
        stream.update();
        CHECK(stream.takeRequests().empty());
        stream.setRefinement(jobs[1].address, true);
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
        auto first = jobs[0].address;
        for (const auto& job : split(stream, first))
        {
            finish(stream, job);
        }
        settle(stream);
        CHECK(stream.cut().size() == 15);
        stream.setRefinement(first, false);
        settle(stream);
        CHECK(stream.cut().size() == 8);
        stream.setRefinement(parent, false);
        settle(stream);
        REQUIRE(stream.cut().size() == 1);
        CHECK(stream.cut()[0].address == parent);
        CHECK(stream.state(parent) == BuildState::Loaded);
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
        CHECK_FALSE(stream.complete(old.id, *payload(true)));
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
        CHECK_FALSE(stream.complete(retry.id, *payload(true)));
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
            CHECK(stream.cut()[0].address == parent);
            CHECK(stream.takeRequests().empty());
            stream.retry(jobs[failed].address);
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
            for (bool constrainCpu : {false, true})
            {
                auto config = settings();
                (constrainCpu ? config.cpuBudget : config.gpuBudget) = budget;
                TerrainStream stream(config);
                auto parent = root(stream);
                auto jobs = split(stream, parent);
                CHECK(jobs.size() == (budget == 65 ? 8 : 0));
                CHECK(stream.diagnostics().jobs == jobs.size());
                checkCoverage(stream);
            }
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

    TEST_CASE("S31 retirement charges survive skipped snapshots until renderer release")
    {
        TerrainStream stream(settings());
        root(stream);
        auto snapshot = stream.cut();
        const auto id = snapshot[0].payload->id;
        stream.withdrawRoot({});
        stream.update();
        CHECK(stream.cut().empty());
        CHECK(stream.diagnostics().retiredGpuBytes == 1);
        const auto removedRevision = stream.diagnostics().publications;
        root(stream, {2, 0, 0}); // Renderer may skip the removal-only snapshot and adopt this later one.
        CHECK(stream.diagnostics().publications > removedRevision);
        const auto retirements = stream.takeRetirements();
        REQUIRE(retirements.size() == 1);
        CHECK(retirements[0].first == id);
        CHECK(retirements[0].second == removedRevision);
        stream.releasePayload(id);
        stream.releasePayload(id);
        CHECK(stream.diagnostics().retiredGpuBytes == 0);
        CHECK(snapshot[0].payload->id == id); // Old metadata remains readable without retaining GPU resources.
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
        for (auto [id, revision] : stream.takeRetirements())
            stream.releasePayload(id);
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
        CHECK(stream.diagnostics().jobs == 1);
        stream.releasePayload(job.id);
        CHECK(stream.diagnostics().jobs == 0);
    }

    TEST_CASE("S20 S21 S22 cross root refinement waits for balanced siblings then selects prebuilt boundaries")
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
        REQUIRE(jobs.size() == 16); // Both sibling groups; boundary selection needs no additional jobs.
        for (size_t i = 0; i + 1 < jobs.size(); ++i)
        {
            finish(stream, jobs[i]);
        }
        stream.update();
        // The complete coarse neighbour group can display independently; the held fine group keeps its parent.
        CHECK(stream.cut().size() == 16);
        CHECK(stream.state(rightChild) == BuildState::Loaded);
        CHECK(
            std::any_of(
                stream.cut().begin(),
                stream.cut().end(),
                [&](const auto& entry)
                {
                    return entry.address == rightChild;
                }
            )
        );
        checkCoverage(stream);
        finish(stream, jobs.back());
        settle(stream);
        CHECK(stream.cut().size() == 23);
        checkCoverage(stream);

        // New neighbor demand can replace previously valid masks; the oracle checks actual adjacency each frame.
        for (const auto& entry : stream.cut())
        {
            if (entry.address.depth == 2)
            {
                stream.setRefinement(parentAddress(entry.address), false);
            }
        }
        stream.setRefinement(a, false);
        stream.setRefinement(b, false);
        settle(stream);
        CHECK(stream.cut().size() == 2);
    }

    TEST_CASE("Neighbor refinement and coarsening toggle prebuilt faces without rebuilding loaded nodes")
    {
        TerrainStream stream(settings());
        const auto a = root(stream);
        const auto b = root(stream, {1, 0, 0});
        for (const auto& job : split(stream, a))
        {
            finish(stream, job);
        }
        settle(stream);
        const auto initial = stream.cut();
        size_t faces = 0;
        for (const auto& entry : initial)
        {
            if (entry.address.cell == a.cell && entry.address.x == 1)
            {
                CHECK(entry.transitionFaces == 2);
                ++faces;
            }
        }
        REQUIRE(faces == 4);
        const auto jobs = split(stream, b);
        REQUIRE(jobs.size() == 8);
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            CHECK(jobs[i].address.cell == b.cell);
            finish(stream, jobs[i]);
            stream.update();
            if (i + 1 < jobs.size())
            {
                CHECK(stream.cut().size() == 9);
            }
        }
        REQUIRE(stream.cut().size() == 16);
        CHECK(stream.takeRequests().empty());
        for (const auto& entry : stream.cut())
        {
            CHECK(entry.transitionFaces == 0);
            if (entry.address.cell == a.cell)
            {
                const auto original = std::find_if(
                    initial.begin(),
                    initial.end(),
                    [&](const auto& value)
                    {
                        return value.address == entry.address;
                    }
                );
                REQUIRE(original != initial.end());
                CHECK(entry.payload == original->payload);
            }
        }
        stream.setRefinement(b, false);
        stream.update();
        REQUIRE(stream.cut().size() == initial.size());
        CHECK(stream.takeRequests().empty());
        for (size_t i = 0; i < initial.size(); ++i)
        {
            CHECK(stream.cut()[i].address == initial[i].address);
            CHECK(stream.cut()[i].payload == initial[i].payload);
            CHECK(stream.cut()[i].transitionFaces == initial[i].transitionFaces);
        }
        checkCoverage(stream);
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
            CHECK_FALSE(stream.complete(job.id, *payload(true)));
        }
        REQUIRE(stream.cut().size() == 1);
        CHECK(stream.cut()[0].address.cell == destination);
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
            const auto retained = stream.cut().front().payload;
            const auto jobs = split(stream, parent);
            REQUIRE(jobs.size() == 8);
            for (size_t i = 0; i < 8; ++i)
            {
                finish(stream, jobs[order[i]]);
                stream.update();
                REQUIRE(stream.cut().size() == (i == 7 ? 8 : 1));
                checkCoverage(stream, 1);
            }
            CHECK(stream.state(parent) == BuildState::Loaded);
            stream.setRefinement(parent, false);
            stream.update();
            REQUIRE(stream.cut().size() == 1);
            CHECK(stream.cut().front().payload == retained);
            CHECK(stream.takeRequests().empty());
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
        for (uint32_t seed = 1; seed <= 32; ++seed)
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
            std::set<NodeAddress> failures;
            std::vector<RequestId> cancelled;
            for (unsigned operation = 0; operation < 5000; ++operation)
            {
                CAPTURE(operation);
                const auto choice = random() % 8;
                trace << operation << " choice " << choice;
                if (choice < 2 && !stream.cut().empty())
                {
                    auto address = stream.cut()[random() % stream.cut().size()].address;
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
                        failures.insert(job.request.address);
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
                            stream.complete(job.request.id, *payload());
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
                else if (choice == 7)
                {
                    const double x = double(random() % 64) - 24;
                    trace << " observer " << x << ",4,4";
                    stream.setObserver({{}, {x, 4, 4}});
                }
                trace << '\n';
                stream.update();
                for (auto [id, revision] : stream.takeRetirements())
                    stream.releasePayload(id);
                for (const auto& job : stream.takeRequests())
                {
                    jobs.emplace(job.id, ExternalJob{job});
                }
                for (const auto& id : stream.takeCancellations())
                {
                    cancelled.push_back(id);
                }
                CHECK(stream.diagnostics().admittedRoots == 2);
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
                if (stream.request(job.request.address) == id)
                {
                    if (job.generated || stream.acceptGenerated(id, 1))
                    {
                        stream.complete(id, *payload());
                    }
                }
            }
            settle(stream, 1024);
            CHECK(stream.cut().size() == 2);
            CHECK(stream.diagnostics().jobs == 0);
            stream.withdrawRoot({});
            stream.withdrawRoot({1, 0, 0});
            settle(stream);
            CHECK(stream.cut().empty());
            CHECK(stream.diagnostics().nodes == 0);
            CHECK(stream.diagnostics().residentCpuBytes == 0);
            CHECK(stream.diagnostics().residentGpuBytes == 0);
            CHECK(stream.diagnostics().retiredCpuBytes == 0);
            CHECK(stream.diagnostics().retiredGpuBytes == 0);
            CHECK(bool(trace));
        }
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Bounded planning preserves held dependencies and progresses independent roots")
    {
        auto config = settings();
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

    TEST_CASE("S18 partial upload failure keeps reservation until renderer release")
    {
        TerrainStream stream(settings());
        auto a = root(stream);
        const auto jobs = split(stream, a);
        REQUIRE(stream.acceptGenerated(jobs[0].id, 1));
        stream.fail(jobs[0].id, "partial upload failure", FailureStage::Upload);
        stream.fail(jobs[0].id, "duplicate failure", FailureStage::Upload);
        stream.update();
        CHECK(stream.cut().size() == 1);
        CHECK(stream.diagnostics().jobs == 8);
        stream.releasePayload(jobs[0].id);
        CHECK(stream.diagnostics().jobs == 7);
        stream.releasePayload(jobs[0].id);
        CHECK(stream.diagnostics().jobs == 7);
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
            if (job.address.cell == CellAddress{1, 0, 0})
            {
                CHECK(job.address.depth == 0);
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
            snapshot.push_back({child, payload()});
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
        broken.front().address.x = 2;
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
        CHECK(stream.cut().front().address.cell.x == 1000000000);
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Root dispatch proceeds outward after each completion changes the candidate list")
    {
        auto config = settings();
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
                const auto distance = distanceToNode(request.address, observer, config.rootWidth);
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
                REQUIRE(request.address.depth == 1);
                const auto distance = distanceToNode(parentAddress(request.address), observer, config.rootWidth);
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
        CHECK(requests[0].address.cell == CellAddress{10, 0, 0});
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Continuous observer motion cannot starve bottom up merges behind an ineligible ancestor")
    {
        auto config = settings();
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

#ifdef OFG_TERRAIN_FUNCTION_PROFILE
TEST_CASE("Function timers count nested scopes and ignore disabled scopes" * doctest::test_suite("terrain-fast"))
{
    using namespace ofg::terrainProfile;
    auto& parent = counters[size_t(Function::TerrainStream_reconcile)];
    auto& child = counters[size_t(Function::TerrainStream_discoverDemand)];
    const auto beforeParent = parent.calls.load(), beforeChild = child.calls.load();
    const auto beforeInclusive = parent.inclusive.load(), beforeExclusive = parent.exclusive.load();
    const auto beforeChildInclusive = child.inclusive.load();
    enabled = true;
    {
        Scope outer(Function::TerrainStream_reconcile);
        {
            Scope inner(Function::TerrainStream_discoverDemand);
        }
    }
    enabled = false;
    {
        Scope ignored(Function::TerrainStream_discoverDemand);
    }
    CHECK(parent.calls.load() == beforeParent + 1);
    CHECK(child.calls.load() == beforeChild + 1);
    CHECK(
        parent.inclusive.load() - beforeInclusive ==
        parent.exclusive.load() - beforeExclusive + child.inclusive.load() - beforeChildInclusive
    );
}
#endif

TEST_SUITE("terrain-fast")
{
    TEST_CASE("L1 L2 depth 13 sparse negative-root coverage retains all fourteen size levels")
    {
        auto config = settings(1 << 20, 13);
        config.rootWidth = 131072;
        config.maximumNodes = 4096;
        TerrainStream stream(config);
        const CellAddress cell{-1000000000, 0, -1000000000};
        NodeAddress address{cell};
        stream.requestRoot(cell);
        for (unsigned depth = 0; depth < 13; ++depth)
        {
            CHECK(std::ldexp(config.rootWidth, -int(depth)) == double(131072u >> depth));
            stream.setRefinement(address, true);
            for (auto child : childAddresses(address))
                stream.setRefinement(child, false);
            address = childAddresses(address)[0];
        }
        CHECK(std::ldexp(config.rootWidth, -int(address.depth)) == 16);
        for (unsigned pass = 0; pass < 40; ++pass)
        {
            stream.update();
            auto jobs = stream.takeRequests();
            for (const auto& job : jobs)
                finish(stream, job);
            if (jobs.empty() && stream.diagnostics().planningIdle)
                break;
        }
        REQUIRE(stream.state(address) == BuildState::Loaded);
        CHECK(stream.diagnostics().nodes == 1 + 8 * 13);
        CHECK(stream.diagnostics().unresolvedRefinements == 0);
        uint64_t volume = 0;
        for (const auto& entry : stream.cut())
            volume += uint64_t(1) << (3 * (13 - entry.address.depth));
        CHECK(volume == (uint64_t(1) << 39));
        CHECK(topologyValid(stream.cut(), 13));
        while (address.depth)
        {
            address = parentAddress(address);
            CHECK(stream.state(address) == BuildState::Loaded);
        }
        stream.setRefinement(address, false);
        stream.update();
        CHECK(stream.cut().size() == 1);
        CHECK(stream.takeRequests().empty());
    }
    TEST_CASE("S11 seven remaining slots cannot partially dispatch an eight-child group")
    {
        auto config = settings();
        config.maximumJobs = 8;
        TerrainStream stream(config);
        auto parent = root(stream);
        const CellAddress other{10, 0, 0};
        stream.requestRoot(other);
        stream.setRefinement({other}, false);
        stream.update();
        const auto held = stream.takeRequests();
        REQUIRE(held.size() == 1);
        CHECK(split(stream, parent).empty());
        CHECK(stream.diagnostics().jobs == 1);
        finish(stream, held[0]);
        stream.update();
        CHECK(stream.takeRequests().size() == 8);
    }
    TEST_CASE("S12 metadata cap never installs seven children and unchanged blocked inputs remain idle")
    {
        auto config = settings();
        config.maximumNodes = 8;
        TerrainStream stream(config);
        const auto parent = root(stream);
        CHECK(split(stream, parent).empty());
        CHECK(stream.diagnostics().nodes == 1);
        CHECK(stream.cut().size() == 1);
        const auto revision = stream.diagnostics().publications;
        for (int i = 0; i < 10; ++i)
            stream.update();
        CHECK(stream.diagnostics().publications == revision);
        CHECK(stream.diagnostics().budgetBlocked);
    }
    TEST_CASE("Metadata refusal remains visible when unrelated readiness wakes selection")
    {
        auto config = settings();
        config.maximumNodes = 8;
        TerrainStream stream(config);
        const auto parent = root(stream);
        CHECK(split(stream, parent).empty());
        REQUIRE(stream.requestRoot({1, 0, 0}));
        stream.setRefinement(NodeAddress{{1, 0, 0}}, false);
        stream.update();
        const auto jobs = stream.takeRequests();
        REQUIRE(jobs.size() == 1);
        finish(stream, jobs.front());
        stream.update();
        CHECK(stream.diagnostics().budgetBlocked);
        CHECK(stream.diagnostics().nodes == 2);
    }
    TEST_CASE("S8 exact hysteresis boundaries retain both prior demand states")
    {
        constexpr double width = 8, epsilon = 1e-6 * width;
        for (bool previouslySplit : {false, true})
        {
            for (double distance :
                 {2 * width - epsilon,
                  2 * width,
                  2 * width + epsilon,
                  2.5 * width - epsilon,
                  2.5 * width,
                  2.5 * width + epsilon})
            {
                CAPTURE(previouslySplit);
                CAPTURE(distance);
                TerrainStream stream(settings());
                const auto parent = root(stream);
                for (const auto& child : childAddresses(parent))
                    stream.setRefinement(child, false);
                stream.setRefinement(parent, {});
                stream.setObserver({{}, {previouslySplit ? 9. : 100., 4, 4}});
                settle(stream);
                stream.setObserver({{}, {width + distance, 4, 4}});
                settle(stream);
                const bool expected = previouslySplit ? distance <= 2.5 * width : distance < 2 * width;
                CHECK(stream.cut().size() == (expected ? 8 : 1));
            }
        }
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("S4 ready fine children remain hidden beside delayed coarse coverage without rebuilding parents")
    {
        TerrainStream stream(settings(8192));
        const auto a = root(stream);
        const auto b = root(stream, {1, 0, 0});
        for (auto child : childAddresses(b))
            stream.setRefinement(child, false);
        for (auto job : split(stream, a))
            finish(stream, job);
        settle(stream);
        const auto parent = childAddresses(a)[1];
        auto old = std::find_if(
                       stream.cut().begin(),
                       stream.cut().end(),
                       [&](const auto& entry)
                       {
                           return entry.address == parent;
                       }
        )->payload;
        const auto requests = split(stream, parent);
        REQUIRE(requests.size() == 16);
        for (auto job : requests)
            if (job.address.depth == 2)
                finish(stream, job);
        stream.update();
        REQUIRE(stream.cut().size() == 9);
        CHECK(stream.state(parent) == BuildState::Loaded);
        CHECK(
            std::find_if(
                stream.cut().begin(),
                stream.cut().end(),
                [&](const auto& entry)
                {
                    return entry.address == parent;
                }
            )->payload == old
        );
        checkCoverage(stream);
        for (auto job : requests)
            if (job.address.depth == 1)
                finish(stream, job);
        settle(stream);
        CHECK(stream.cut().size() == 23);
        CHECK(stream.takeRequests().empty());
    }
    TEST_CASE("S12 failed boundary closure coarsens the target and does not recreate discarded groups at rest")
    {
        auto config = settings(8192);
        config.maximumNodes = 18;
        TerrainStream stream(config);
        const auto a = root(stream);
        const auto b = root(stream, {1, 0, 0});
        for (auto child : childAddresses(b))
            stream.setRefinement(child, false);
        for (auto job : split(stream, a))
            finish(stream, job);
        settle(stream);
        CHECK(split(stream, childAddresses(a)[1]).empty());
        REQUIRE(stream.cut().size() == 9);
        CHECK(stream.diagnostics().nodes == 10);
        const auto revision = stream.diagnostics().publications;
        for (int i = 0; i < 20; ++i)
        {
            stream.update();
            CHECK(stream.takeRequests().empty());
            CHECK(stream.diagnostics().nodes == 10);
            CHECK(stream.diagnostics().publications == revision);
        }
        checkCoverage(stream);
    }
    TEST_CASE("S13 renderer release resumes an otherwise affordable sibling group without camera movement")
    {
        TerrainStream stream(settings(65));
        const auto a = root(stream);
        const auto b = root(stream, {10, 0, 0});
        CHECK(split(stream, a).empty()); // Two resident bytes plus 64 reserved bytes exceed 65.
        stream.withdrawRoot(b.cell);
        stream.update();
        CHECK(stream.takeRequests().empty());
        const auto retirements = stream.takeRetirements();
        REQUIRE(retirements.size() == 1);
        stream.releasePayload(retirements[0].first);
        stream.update();
        CHECK(stream.takeRequests().size() == 8);
    }
}
