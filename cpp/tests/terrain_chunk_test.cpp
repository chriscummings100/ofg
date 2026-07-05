// Doctest coverage for generated terrain chunk data.
//
// These tests keep the first terrain chunk contract concrete: LOD0 chunks have
// fixed 33 by 33 surface samples, address validation is explicit, and adjacent
// chunks agree at shared world-coordinate edges.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/terrain/terrain.hpp"
#include "ofg/terrain/terrain_chunk.hpp"

#include <cstddef>

// Verifies chunk ids reject unsupported terrain address space for Milestone 1.
TEST_CASE("terrain chunk id validates the supported LOD0 surface address") {
    CHECK_NOTHROW(ofg::validate_terrain_chunk_id(ofg::TerrainChunkId{0, 0, 0, 0}));
    CHECK_THROWS_WITH_AS(([&]() { ofg::validate_terrain_chunk_id(ofg::TerrainChunkId{1, 0, 0, 0}); }()),
        doctest::Contains("LOD"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { ofg::validate_terrain_chunk_id(ofg::TerrainChunkId{0, 0, 1, 0}); }()),
        doctest::Contains("chunk_y"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::validate_terrain_chunk_id(ofg::TerrainChunkId{0, ofg::terrain_chunk_coordinate_abs_limit + 1, 0, 0});
    }()),
        doctest::Contains("limit"),
        ofg::EngineError);
}

// Verifies chunk construction preserves identity and future resource slots start empty.
TEST_CASE("terrain chunk exposes identity and empty resource slots") {
    const ofg::TerrainChunkId id{0, -2, 0, 3};
    ofg::TerrainChunk chunk{id};

    CHECK(chunk.id() == id);
    CHECK_FALSE(chunk.has_heightfield());
    CHECK(chunk.heightfield_samples().empty());
    CHECK(chunk.world_min_x() == doctest::Approx(-64.0f));
    CHECK(chunk.world_min_z() == doctest::Approx(96.0f));
    CHECK(chunk.render_mesh() == nullptr);
    CHECK(chunk.debug_plane_mesh() == nullptr);
    CHECK(chunk.debug_plane_texture() == nullptr);
    CHECK_THROWS_WITH_AS(([&]() { (void)chunk.heightfield_sample_at(0, 0); }()),
        doctest::Contains("not been generated"),
        ofg::EngineError);
}

// Verifies generated heightfields use the fixed LOD0 dual-grid dimensions.
TEST_CASE("terrain chunk generates a 33 by 33 heightfield from terrain sampling") {
    ofg::Terrain terrain;
    ofg::TerrainChunk chunk{ofg::TerrainChunkId{0, 0, 0, 0}};

    chunk.generate_heightfield(terrain);

    CHECK(chunk.has_heightfield());
    CHECK(chunk.heightfield_samples().size() == static_cast<std::size_t>(ofg::terrain_chunk_lod0_vertices_per_edge *
                                                                         ofg::terrain_chunk_lod0_vertices_per_edge));
    CHECK(chunk.heightfield_sample_at(0, 0).m_height == doctest::Approx(terrain.sample(0.0f, 0.0f).m_height));
    CHECK(chunk.heightfield_sample_at(32, 32).m_height == doctest::Approx(terrain.sample(32.0f, 32.0f).m_height));
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)chunk.heightfield_sample_at(33, 0); }()), doctest::Contains("33 by 33"), ofg::EngineError);
}

// Verifies adjacent chunks share identical edge heights because sampling is world-coordinate based.
TEST_CASE("terrain chunk heightfields agree on shared edges") {
    ofg::Terrain terrain;
    ofg::TerrainChunk left{ofg::TerrainChunkId{0, 0, 0, 0}};
    ofg::TerrainChunk right{ofg::TerrainChunkId{0, 1, 0, 0}};

    left.generate_heightfield(terrain);
    right.generate_heightfield(terrain);

    for (std::int32_t z = 0; z < ofg::terrain_chunk_lod0_vertices_per_edge; ++z) {
        CHECK(
            left.heightfield_sample_at(32, z).m_height == doctest::Approx(right.heightfield_sample_at(0, z).m_height));
    }
}
