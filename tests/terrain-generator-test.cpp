// Geometry tests check surface boundaries and worker output contracts independently of Transvoxel's tables.
#include "doctest.h"
#include "terrain/terrain-generator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace ofg::terrain;

namespace {
// Builds a small fixture using bounded steps, as the production worker does.
TerrainGeometry mesh(NodeAddress node, uint8_t mask, GeneratorSettings settings)
{
    TerrainMesher mesher(node, mask, settings, 16 << 20);
    size_t steps = 0;
    while (!mesher.step(37))
    {
        REQUIRE(++steps < 100000);
    }
    return mesher.takeGeometry();
}

using Point = std::array<int64_t, 3>;
using Edge = std::pair<Point, Point>;

// Welds geometric positions only for the independent topological oracle, at one millionth of a metre.
Point point(const ofg::math::Vec3& value, std::array<double, 3> origin)
{
    return {
        std::llround((origin[0] + value.x) * 1e6),
        std::llround((origin[1] + value.y) * 1e6),
        std::llround((origin[2] + value.z) * 1e6)
    };
}

// Counts triangle incidence after independently welding output positions; table edge IDs are never used.
void addEdges(std::map<Edge, unsigned>& edges, const TerrainGeometry& geometry, std::array<double, 3> origin)
{
    for (size_t i = 0; i < geometry.indices.size(); i += 3)
    {
        std::array<Point, 3> vertices;
        for (int j = 0; j < 3; ++j)
        {
            vertices[j] = point(geometry.vertices[geometry.indices[i + j]].position, origin);
        }
        for (int j = 0; j < 3; ++j)
        {
            auto a = vertices[j], b = vertices[(j + 1) % 3];
            if (b < a)
            {
                std::swap(a, b);
            }
            if (a != b)
            {
                ++edges[{a, b}];
            }
        }
    }
}

// Welds within five micrometres (1e-5 of this fixture's finest 0.5 m sample spacing).
// Rounding each node-local float before adding a different origin can differ by one float ULP;
// rounding into fixed buckets alone incorrectly separates points on opposite bucket boundaries.
std::map<Edge, unsigned> weldEdges(const std::map<Edge, unsigned>& edges)
{
    std::map<Point, Point> canonical;
    std::set<Point> representatives;
    for (const auto& [edge, count] : edges)
    {
        for (const auto& point : {edge.first, edge.second})
        {
            if (canonical.contains(point))
            {
                continue;
            }
            Point chosen = point;
            for (auto i = representatives.lower_bound({point[0] - 5, INT64_MIN, INT64_MIN});
                 i != representatives.end() && (*i)[0] <= point[0] + 5;
                 ++i)
            {
                int64_t squared = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const auto delta = (*i)[axis] - point[axis];
                    squared += delta * delta;
                }
                if (squared <= 25)
                {
                    chosen = *i;
                    break;
                }
            }
            canonical.emplace(point, chosen);
            representatives.insert(chosen);
        }
    }
    std::map<Edge, unsigned> result;
    for (const auto& [edge, count] : edges)
    {
        auto a = canonical.at(edge.first), b = canonical.at(edge.second);
        if (b < a)
        {
            std::swap(a, b);
        }
        if (a != b)
        {
            result[{a, b}] += count;
        }
    }
    return result;
}
} // namespace

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Terrain noise samples agree exactly across negative distant root boundaries")
    {
        GeneratorSettings s;
        for (int64_t x : {-1000000000ll, -1ll, 0ll, 1000000000ll})
        {
            for (int z = -2; z <= 2; ++z)
            {
                CHECK(terrainHeight({x, 0, z}, 1024, 257.25, s) == terrainHeight({x + 1, 0, z}, 0, 257.25, s));
            }
        }
    }

    TEST_CASE("Terrain planes stay inside exact 3D bounds and use outward winding")
    {
        GeneratorSettings s;
        s.rootWidth = 8;
        s.intervals = 8;
        s.amplitude = 0;
        for (double height : {0., .25, 4., 7.75, 8.})
        {
            s.heightOffset = height;
            const auto geometry = mesh({}, 0, s);
            CHECK_NOTHROW(validateGeometry(geometry, 8, 16 << 20));
            for (const auto& vertex : geometry.vertices)
            {
                CHECK(vertex.position.y == doctest::Approx(height));
            }
            for (size_t i = 0; i < geometry.indices.size(); i += 3)
            {
                const auto& a = geometry.vertices[geometry.indices[i]].position;
                const auto& b = geometry.vertices[geometry.indices[i + 1]].position;
                const auto& c = geometry.vertices[geometry.indices[i + 2]].position;
                CHECK(ofg::math::cross(ofg::math::sub(b, a), ofg::math::sub(c, a)).y > 0);
            }
        }
    }

    TEST_CASE("Terrain mesher certifies only globally empty volumes")
    {
        GeneratorSettings s;
        const auto above = mesh({{0, 2, 0}}, 0, s);
        CHECK(above.certifiedEmpty);
        CHECK(above.indices.empty());
        s.amplitude = 0;
        s.heightOffset = 0;
        auto boundary = mesh({}, 0, s);
        CHECK_FALSE(boundary.certifiedEmpty);
    }

    TEST_CASE("Terrain sample and mesh output are independent of step size")
    {
        GeneratorSettings s;
        s.rootWidth = 8;
        s.intervals = 8;
        s.heightOffset = 4;
        s.amplitude = 3;
        auto a = mesh({}, 63, s);
        TerrainMesher mesher({}, 63, s, 16 << 20);
        while (!mesher.step(1))
        {
        }
        auto b = mesher.takeGeometry();
        REQUIRE(a.vertices.size() == b.vertices.size());
        CHECK(a.indices == b.indices);
        for (size_t i = 0; i < a.vertices.size(); ++i)
        {
            CHECK(a.vertices[i].position.x == b.vertices[i].position.x);
            CHECK(a.vertices[i].position.y == b.vertices[i].position.y);
            CHECK(a.vertices[i].position.z == b.vertices[i].position.z);
        }
        CHECK_THROWS(mesher.takeGeometry());
    }

    TEST_CASE("Terrain transition strips close fine coarse interfaces")
    {
        GeneratorSettings s;
        s.rootWidth = 8;
        s.intervals = 8;
        s.heightOffset = 2.1;
        s.amplitude = 2;
        // Two roots meet at X=8. The left root is split into eight children, right remains coarse.
        std::map<Edge, unsigned> edges;
        for (const auto& child : childAddresses({}))
        {
            auto geometry = mesh(child, child.x == 1 ? 2 : 0, s);
            addEdges(edges, geometry, {child.x * 4., child.y * 4., child.z * 4.});
        }
        addEdges(edges, mesh({{1, 0, 0}}, 0, s), {8, 0, 0});
        size_t internal = 0;
        for (const auto& [edge, incidence] : edges)
        {
            const auto& [a, b] = edge;
            // Only internal interface edges, excluding the outside boundary of the assembled domain.
            if (a[0] == 8000000 && b[0] == 8000000 && a[2] > 0 && b[2] > 0 && a[2] < 8000000 && b[2] < 8000000 &&
                a[1] > 0 && b[1] > 0 && a[1] < 8000000 && b[1] < 8000000)
            {
                CAPTURE(a[1]);
                CAPTURE(a[2]);
                CAPTURE(b[1]);
                CAPTURE(b[2]);
                CAPTURE(incidence);
                CHECK(incidence == 2);
                ++internal;
            }
        }
        CHECK(internal > 0);
    }

    TEST_CASE("S30 worker geometry rejects malformed results before GPU preparation")
    {
        TerrainGeometry geometry;
        geometry.vertices.resize(3);
        geometry.indices = {0, 1, 3};
        CHECK_THROWS(validateGeometry(geometry, 8, 4096));
        geometry.indices[2] = 2;
        geometry.vertices[0].position.x = 9;
        CHECK_THROWS(validateGeometry(geometry, 8, 4096));
        geometry.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
        CHECK_THROWS(validateGeometry(geometry, 8, 4096));
        geometry.vertices[0].position.x = 0;
        CHECK_THROWS(validateGeometry(geometry, 8, 1));
        geometry.certifiedEmpty = true;
        CHECK_THROWS(validateGeometry(geometry, 8, 4096));
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Terrain transitions close all six faces and their edge and corner junctions")
    {
        GeneratorSettings s;
        s.rootWidth = 8;
        s.intervals = 8;
        s.heightOffset = 4;
        s.amplitude = 9;
        for (uint64_t seed : {1, 7, 19})
        {
            s.seed = seed;
            std::map<Edge, unsigned> edges;
            for (const auto& child : childAddresses({}))
            {
                const uint8_t faces = uint8_t((1u << child.x) | (1u << (2 + child.y)) | (1u << (4 + child.z)));
                addEdges(edges, mesh(child, faces, s), {child.x * 4., child.y * 4., child.z * 4.});
            }
            for (int z = -1; z <= 1; ++z)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        if (x == 0 && y == 0 && z == 0)
                        {
                            continue;
                        }
                        addEdges(edges, mesh({{x, y, z}}, 0, s), {x * 8., y * 8., z * 8.});
                    }
                }
            }
            size_t interiorEdges = 0;
            for (const auto& [edge, incidence] : weldEdges(edges))
            {
                bool outside = false;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    outside |= edge.first[axis] <= -8000000 || edge.first[axis] >= 16000000 ||
                               edge.second[axis] <= -8000000 || edge.second[axis] >= 16000000;
                }
                if (outside)
                {
                    continue;
                }
                CAPTURE(seed);
                CAPTURE(edge.first[0]);
                CAPTURE(edge.first[1]);
                CAPTURE(edge.first[2]);
                CHECK(incidence == 2);
                ++interiorEdges;
            }
            CHECK(interiorEdges > 100);
        }
    }
}

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Local height bounds certify empty nodes inside the global height range")
    {
        GeneratorSettings settings;
        settings.rootWidth = 64;
        settings.heightOffset = 16;
        settings.amplitude = 28;
        settings.intervals = 4;
        size_t localCertificates = 0;
        for (uint64_t seed : {1ull, 19ull})
        {
            settings.seed = seed;
            for (uint32_t z = 0; z < 8; ++z)
            {
                for (uint32_t x = 0; x < 8; ++x)
                {
                    for (uint32_t y = 0; y < 6; ++y)
                    {
                        NodeAddress node{{-1000000000, 0, -1}, x, y, z, 3};
                        auto geometry = mesh(node, 0, settings);
                        if (!geometry.certifiedEmpty)
                        {
                            continue;
                        }
                        ++localCertificates;
                        // Independently sample much more densely than the mesher, including the full node perimeter.
                        bool below = true, above = true;
                        for (unsigned iz = 0; iz <= 16; ++iz)
                        {
                            for (unsigned ix = 0; ix <= 16; ++ix)
                            {
                                const auto h = terrainHeight(node.cell, x * 8 + ix * .5, z * 8 + iz * .5, settings);
                                below = below && h < y * 8;
                                above = above && h > (y + 1) * 8;
                            }
                        }
                        CHECK((below || above));
                        CHECK(geometry.indices.empty());
                    }
                }
            }
        }
        CHECK(localCertificates > 100);
    }
}
