// Observable LOD bands on a plane isolate distance policy from noise, meshing and graphics timing.
#include "terrain/terrain-stream.h"
#include <doctest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

using namespace ofg::terrain;

TEST_SUITE("terrain-bands")
{
    TEST_CASE("Seven distance bands cover 16 through 1024 metre cells with parents generated first")
    {
        StreamSettings settings;
        settings.maximumPayloadBytes = 4 << 20;
        settings.cpuBudget = 128 << 20;
        settings.maximumJobs = 32;
        TerrainStream stream(settings);
        const WorldPosition observer{{}, {123, 5, 231}};
        constexpr double surfaceY = 3;
        stream.setObserver(observer);
        for (int z = -4; z <= 4; ++z)
        {
            for (int x = -4; x <= 4; ++x)
            {
                stream.requestRoot({x, 0, z});
            }
        }
        bool settled = false;
        for (unsigned step = 0; step < 2000; ++step)
        {
            stream.update();
            const auto requests = stream.takeRequests();
            // Check the entire dispatch before completing anything: ordering within one batch cannot hide a violation.
            for (const auto& request : requests)
            {
                if (request.key.address.depth)
                {
                    REQUIRE(stream.state({parentAddress(request.key.address), 0}) == BuildState::Loaded);
                }
            }
            for (const auto& request : requests)
            {
                const auto& a = request.key.address;
                const double width = std::ldexp(1024., -a.depth);
                const double y = a.y * width;
                const bool empty = y > surfaceY || y + width < surfaceY;
                auto payload = std::make_shared<PreparedPayload>();
                payload->empty = payload->certifiedEmpty = empty;
                payload->cpuBytes = payload->gpuBytes = empty ? 0 : 1;
                if (!empty)
                {
                    REQUIRE(stream.acceptGenerated(request.id, 1));
                }
                REQUIRE(stream.complete(request.id, std::move(payload)));
            }
            for (const auto& id : stream.takeCancellations())
            {
                stream.acknowledgeCancellation(id);
            }
            const auto d = stream.diagnostics();
            if (!d.jobs && !d.loadingRoots && !d.unresolvedRefinements)
            {
                settled = true;
                break;
            }
        }
        REQUIRE(settled);
        CHECK_NOTHROW(stream.validate());
        std::set<unsigned> depths;
        const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path() / "artifacts/terrain/bands";
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / "cut.csv");
        output << "x,z,width,depth\n";
        for (const auto& entry : stream.cut())
        {
            if (entry.payload->empty)
            {
                continue;
            }
            const auto& a = entry.key.address;
            const double width = std::ldexp(1024., -a.depth);
            depths.insert(a.depth);
            output << double(a.cell.x) * 1024 + a.x * width << ',' << double(a.cell.z) * 1024 + a.z * width << ','
                   << width << ',' << unsigned(a.depth) << '\n';
        }
        REQUIRE(depths == std::set<unsigned>{0, 1, 2, 3, 4, 5, 6});
        for (unsigned angle = 0; angle < 64; ++angle)
        {
            for (double radius : {0., 8., 16., 32., 64., 128., 256., 512., 1024., 2048., 3072., 4000.})
            {
                const double direction = angle * 6.283185307179586 / 64;
                const auto point = normalizePosition(
                    {{}, {123 + radius * std::cos(direction), surfaceY, 231 + radius * std::sin(direction)}},
                    1024
                );
                unsigned matches = 0, actualDepth = 0;
                for (const auto& entry : stream.cut())
                {
                    const auto& a = entry.key.address;
                    if (a.cell != point.cell)
                    {
                        continue;
                    }
                    const double width = std::ldexp(1024., -a.depth);
                    if (point.local[0] >= a.x * width && point.local[0] < (a.x + 1) * width &&
                        point.local[1] >= a.y * width && point.local[1] < (a.y + 1) * width &&
                        point.local[2] >= a.z * width && point.local[2] < (a.z + 1) * width)
                    {
                        ++matches;
                        actualDepth = a.depth;
                    }
                }
                REQUIRE(matches == 1);
                // Independent analytic AABB-distance subdivision along this sample's path. Balancing may add
                // one finer level at a neighbor boundary, but coarse holes inside the required band are forbidden.
                unsigned expectedDepth = 0;
                for (unsigned depth = 0; depth < 6; ++depth)
                {
                    const double width = std::ldexp(1024., -int(depth));
                    double squared = 0;
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        const int64_t cell = axis == 0 ? point.cell.x : axis == 1 ? point.cell.y : point.cell.z;
                        const double minimum = double(cell) * 1024 + std::floor(point.local[axis] / width) * width;
                        const double value = observer.local[axis];
                        const double distance = value < minimum           ? minimum - value
                                                : value > minimum + width ? value - minimum - width
                                                                          : 0;
                        squared += distance * distance;
                    }
                    if (squared >= 4 * width * width)
                    {
                        break;
                    }
                    expectedDepth = depth + 1;
                }
                CAPTURE(angle);
                CAPTURE(radius);
                CAPTURE(actualDepth);
                CAPTURE(expectedDepth);
                CHECK(actualDepth >= expectedDepth);
                CHECK(actualDepth <= expectedDepth + 1);
                if (radius <= 16)
                {
                    CHECK(actualDepth == 6);
                }
                // Beyond 2 root widths plus a root diagonal, no containing root intersects the refinement sphere.
                if (radius >= 4000)
                {
                    CHECK(actualDepth == 0);
                }
            }
        }
        CHECK(bool(output));
    }
}
