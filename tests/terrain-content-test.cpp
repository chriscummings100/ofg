// Python-to-C++ content fixtures verify immutable identity, source inheritance and real mesher inputs.
#include <doctest.h>
#include "terrain/terrain-content.h"
#include "terrain/terrain-generation-client.h"
#include "terrain/terrain-generator.h"
#include "terrain/terrain-shading.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cstring>

using namespace ofg::terrain;

TEST_CASE(
    "Generator recipes preserve exact world identifiers and independent draft values" *
    doctest::test_suite("terrain-fast")
)
{
    IslandRecipe recipe;
    recipe.seed = UINT64_MAX;
    recipe.regionX = INT64_MIN;
    recipe.regionZ = INT64_MAX;
    recipe.plateauHeight = 123.5;
    const auto encoded = encodeIslandRecipe(recipe);
    const auto decoded = decodeIslandRecipe(encoded);
    CHECK(decoded.seed == UINT64_MAX);
    CHECK(decoded.regionX == INT64_MIN);
    CHECK(decoded.regionZ == INT64_MAX);
    CHECK(decoded.plateauHeight == 123.5);
    CHECK(encodeIslandRecipe(decoded) == encoded);
    auto draft = decoded;
    draft.seed = 7;
    CHECK(decoded.seed == UINT64_MAX);
    CHECK_THROWS(decodeIslandRecipe("{}"));
}

namespace {
// Reads a checked-in Python-encoded fixture, failing visibly if its test data is absent.
std::vector<std::byte> fixture(std::string name)
{
    std::ifstream input(
        std::filesystem::path(OFG_SOURCE_DIR) / "tests/fixtures/terrain" / name,
        std::ios::binary | std::ios::ate
    );
    REQUIRE(input.good());
    std::vector<std::byte> bytes(size_t(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    REQUIRE(input.good());
    return bytes;
}

// Loads the manifest associated with both cross-language binary fixtures.
TerrainManifest manifest()
{
    const auto bytes = fixture("manifest.json");
    return decodeTerrainManifest({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
} // namespace

TEST_SUITE("terrain-fast")
{
    TEST_CASE("Terrain shading preserves subvertex heights and maps inherited texel centres")
    {
        auto source = *decodeTerrainTile(fixture("v1-shore.bin"), manifest(), {{}, 0, 0, 0, 4});
        // Independent IEEE half decoding provides an oracle for the GPU representation.
        const auto decode = [](uint16_t bits)
        {
            const int e = (bits >> 10) & 31;
            return (bits & 32768 ? -1.0 : 1.0) * std::ldexp(double(bits & 1023) + (e ? 1024 : 0), e ? e - 25 : -24);
        };
        source.surface[100] = 9876.543f;
        source.surface[101] = -1234.5678f;
        source.validity[101] = 0;
        const auto pixels = encodeTerrainTexture(source);
        CHECK(pixels.size() * sizeof(uint16_t) + source.validity.size() == terrainTextureBytes);
        for (size_t i = 0; i < source.surface.size(); ++i)
        {
            const double restored = (decode(pixels[i * 4]) + decode(pixels[i * 4 + 1])) * 1024;
            CHECK(std::abs(restored - source.surface[i]) < .001);
        }
        CHECK(decode(pixels[101 * 4 + 2]) == -1);
        CHECK(decode(pixels[101 * 4 + 3]) == -1);
        const auto own = terrainTextureMapping(source, source.address);
        CHECK(own[0] * 259 == doctest::Approx(256));
        CHECK(own[2] * 259 == doctest::Approx(1.5));
        NodeAddress descendant{{}, 3, 0, 2, 6};
        const auto uv = terrainTextureMapping(source, descendant);
        CHECK(uv[0] * 259 == doctest::Approx(64));
        CHECK(uv[2] * 259 == doctest::Approx(193.5));
        CHECK(uv[3] * 259 == doctest::Approx(129.5));
        CHECK_THROWS(terrainTextureMapping(source, {{}, 4, 0, 0, 6}));
    }
    TEST_CASE("Terrain service Python fixtures preserve identity channels and exact distant addresses")
    {
        const auto info = manifest();
        CHECK(info.sourceSpacing == 32);
        CHECK(info.terminalDepth == 4);
        auto bytes = fixture("v1-shore.bin");
        auto tile = decodeTerrainTile(bytes, info, {{}, 0, 0, 0, 4});
        CHECK(tile->terminal());
        CHECK(tile->surface.size() == 259 * 259);
        CHECK(tile->surface == tile->bedrock);
        CHECK(tile->height(0, 0) == 40);
        CHECK(tile->height(8000, 8000) == -80);
        CHECK(tile->allocatedBytes() >= 1006215);
        const NodeAddress far{{-(int64_t(1) << 58), 0, (int64_t(1) << 58) + 1}};
        const auto ocean = decodeTerrainTile(fixture("v1-distant-ocean.bin"), info, far);
        CHECK(ocean->address == far);
        CHECK(ocean->height(0, 0) == -80);
        CHECK_FALSE(ocean->terminal());
        CHECK(ocean->water.front() == 80);
    }

    TEST_CASE("Terrain decoding rejects corruption truncation and out of order identity")
    {
        auto info = manifest();
        const NodeAddress address{{}, 0, 0, 0, 4};
        const auto original = fixture("v1-shore.bin");
        for (size_t offset : {0,  8,  12, 16,  20,  24,  28,  32,  40,  48,  52,  56,    72,
                              80, 88, 96, 104, 112, 120, 128, 136, 140, 191, 192, 500000})
        {
            auto changed = original;
            changed[offset] ^= std::byte{64};
            CHECK_THROWS(decodeTerrainTile(changed, info, address));
        }
        CHECK_THROWS(decodeTerrainTile(std::span(original).first(original.size() - 1), info, address));
        CHECK_THROWS(decodeTerrainTile(original, info, {{}, 1, 0, 0, 4}));
        info.revisionBytes[0] ^= 1;
        CHECK_THROWS(decodeTerrainTile(original, info, address));
    }

    TEST_CASE("Terminal terrain source maps descendants without another data request or resampling")
    {
        const auto info = manifest();
        const NodeAddress source{{}, 0, 0, 0, 4};
        const auto tile = decodeTerrainTile(fixture("v1-shore.bin"), info, source);
        const NodeAddress child{{}, 7, 0, 9, 13};
        CHECK(terrainTileAddress(child, info.terminalDepth) == source);
        CHECK(terrainTileAddress({{}, 0, 0, 0, 2}, info.terminalDepth).depth == 2);
        CHECK(tile->sourcePosition(child, 8, 4) == std::array<double, 2>{120, 148});
        CHECK(terrainTilePath(info, child) == terrainTilePath(info, source));
        CHECK_THROWS(tile->sourcePosition({{1, 0, 0}}, 0, 0));
        CHECK_THROWS(tile->height(-33, 0));
        CHECK_THROWS(terrainTileAddress(child, 10));
    }

    TEST_CASE("Terrain mesher uses source heights and certificates rather than noise")
    {
        auto source =
            std::make_shared<TerrainTile>(*decodeTerrainTile(fixture("v1-shore.bin"), manifest(), {{}, 0, 0, 0, 4}));
        GeneratorSettings settings;
        settings.rootWidth = 131072;
        settings.source = source;
        const NodeAddress node{{}, 0, 2, 0, 13}; // 16 m cell spanning elevation 32..48, plateau is 40.
        TerrainMesher mesher(node, settings, 4 << 20);
        while (!mesher.step())
        {
        }
        const auto geometry = mesher.takeGeometry();
        REQUIRE_FALSE(geometry.indices.empty());
        for (const auto& vertex : geometry.vertices)
            CHECK(vertex.position.y == doctest::Approx(8));
        // Coarse samples can be completely flat while source bounds promise finer detail.
        std::fill(source->surface.begin(), source->surface.end(), 40.0f);
        source->minimumHeight = -80;
        source->maximumHeight = 150;
        TerrainMesher hidden({{}, 0, 7, 0, 13}, settings, 4 << 20);
        while (!hidden.step())
        {
        }
        auto empty = hidden.takeGeometry();
        CHECK(empty.indices.empty());
        CHECK_FALSE(empty.certifiedEmpty);
        TerrainMesher air({{}, 0, 10, 0, 13}, settings, 4 << 20);
        while (!air.step())
        {
        }
        CHECK(air.takeGeometry().certifiedEmpty);
    }
}
