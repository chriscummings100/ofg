// Doctest coverage for the scene-owned procedural Terrain model.
//
// These tests pin the renderer-first terrain foundation: deterministic sampling,
// floor-division chunk lookup, origin-centred streaming, addressable chunks,
// and Scene ownership.
#include "doctest.h"

#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"
#include "ofg/terrain/terrain.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

// Returns whether a generated terrain surface set contains one chunk id.
bool terrain_has_chunk(const ofg::Terrain& terrain, std::int32_t chunk_x, std::int32_t chunk_z) {
    return terrain.find_chunk(ofg::TerrainChunkId{0, chunk_x, 0, chunk_z}) != nullptr;
}

// Creates a concise LOD0 surface chunk id for expectations.
ofg::TerrainChunkId chunk_id(std::int32_t chunk_x, std::int32_t chunk_z) {
    return ofg::TerrainChunkId{0, chunk_x, 0, chunk_z};
}

} // namespace

// Verifies terrain config validation and sample determinism.
TEST_CASE("terrain samples deterministic finite sine-octave heights") {
    ofg::Terrain terrain;

    const ofg::TerrainSample first = terrain.sample(12.5f, -7.25f);
    const ofg::TerrainSample second = terrain.sample(12.5f, -7.25f);
    CHECK(std::isfinite(first.m_height));
    CHECK(first.m_height == doctest::Approx(second.m_height));

    ofg::Terrain other_seed;
    other_seed.set_config(ofg::TerrainConfig{99U, 8.0f});
    CHECK(other_seed.sample(12.5f, -7.25f).m_height != doctest::Approx(first.m_height));

    CHECK_THROWS_WITH_AS(([&]() { terrain.set_config(ofg::TerrainConfig{1U, 0.0f}); }()),
        doctest::Contains("height_scale"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { terrain.set_config(ofg::TerrainConfig{1U, std::numeric_limits<float>::infinity()}); }()),
        doctest::Contains("height_scale"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)terrain.sample(std::numeric_limits<float>::quiet_NaN(), 0.0f); }()),
        doctest::Contains("finite"),
        ofg::EngineError);
}

// Verifies terrain chunk lookup uses mathematical floor division at boundaries.
TEST_CASE("terrain maps world coordinates to LOD0 chunks with floor division") {
    ofg::Terrain terrain;

    CHECK(terrain.chunk_id_containing(0.0f, 0.0f) == chunk_id(0, 0));
    CHECK(terrain.chunk_id_containing(31.999f, 31.999f) == chunk_id(0, 0));
    CHECK(terrain.chunk_id_containing(32.0f, 0.0f) == chunk_id(1, 0));
    CHECK(terrain.chunk_id_containing(-0.001f, 0.0f) == chunk_id(-1, 0));
    CHECK(terrain.chunk_id_containing(-32.0f, -32.0f) == chunk_id(-1, -1));
    CHECK(terrain.chunk_id_containing(-32.001f, -32.001f) == chunk_id(-2, -2));
    CHECK_THROWS_WITH_AS(([&]() { (void)terrain.chunk_id_containing(std::numeric_limits<float>::infinity(), 0.0f); }()),
        doctest::Contains("finite"),
        ofg::EngineError);
}

// Verifies ticking reconciles the streamed 5 by 5 LOD0 surface region.
TEST_CASE("terrain tick streams origin-centred surface chunks idempotently") {
    ofg::Terrain terrain;
    const ofg::TerrainSample before_tick_sample = terrain.sample(5.0f, 5.0f);

    terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(0.0f, 0.0f, 0.0f)});

    CHECK(terrain.chunk_count() == 25);
    for (std::int32_t z = -2; z <= 2; ++z) {
        for (std::int32_t x = -2; x <= 2; ++x) {
            CAPTURE(x);
            CAPTURE(z);
            CHECK(terrain_has_chunk(terrain, x, z));
            const ofg::TerrainChunk* chunk = terrain.find_chunk(ofg::TerrainChunkId{0, x, 0, z});
            REQUIRE(chunk != nullptr);
            CHECK(chunk->has_heightfield());
        }
    }
    CHECK(terrain.sample(5.0f, 5.0f).m_height == doctest::Approx(before_tick_sample.m_height));

    const ofg::TerrainChunk* origin_chunk = terrain.find_chunk(ofg::TerrainChunkId{0, 0, 0, 0});
    REQUIRE(origin_chunk != nullptr);
    const ofg::TerrainChunk* origin_chunk_after_same_tick = nullptr;
    terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(0.0f, 100.0f, 0.0f)});
    origin_chunk_after_same_tick = terrain.find_chunk(ofg::TerrainChunkId{0, 0, 0, 0});
    CHECK(origin_chunk_after_same_tick == origin_chunk);
    CHECK(terrain.chunk_count() == 25);

    terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(32.0f, 0.0f, 0.0f)});
    CHECK(terrain.chunk_count() == 25);
    CHECK(terrain_has_chunk(terrain, 3, -2));
    CHECK_FALSE(terrain_has_chunk(terrain, -2, 2));

    terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(160.0f, 0.0f, 0.0f)});
    CHECK(terrain.chunk_count() == 25);
    CHECK_FALSE(terrain_has_chunk(terrain, 0, 0));
    CHECK(terrain_has_chunk(terrain, 7, 0));

    CHECK_THROWS_WITH_AS(([&]() {
        terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(0.0f, std::numeric_limits<float>::infinity(), 0.0f)});
    }()),
        doctest::Contains("finite"),
        ofg::EngineError);
}

// Verifies chunk creation returns pointers and does not mix reference styles.
TEST_CASE("terrain get_or_create_chunk returns stable pointers") {
    ofg::Terrain terrain;
    const ofg::TerrainChunkId id{0, 4, 0, -3};

    ofg::TerrainChunk* first = terrain.get_or_create_chunk(id);
    ofg::TerrainChunk* second = terrain.get_or_create_chunk(id);

    REQUIRE(first != nullptr);
    CHECK(second == first);
    CHECK(first->id() == id);
    CHECK(terrain.chunk_count() == 1);
    CHECK_THROWS_WITH_AS(([&]() { (void)terrain.get_or_create_chunk(ofg::TerrainChunkId{2, 0, 0, 0}); }()),
        doctest::Contains("LOD"),
        ofg::EngineError);
}

// Verifies config changes clear streamed chunks so stale heightfields cannot survive.
TEST_CASE("terrain config changes clear generated chunks") {
    ofg::Terrain terrain;
    terrain.tick(ofg::TerrainTickContext{ofg::math::vec3(0.0f, 0.0f, 0.0f)});
    REQUIRE(terrain.chunk_count() == 25);

    terrain.set_config(ofg::TerrainConfig{77U, 8.0f});

    CHECK(terrain.chunk_count() == 0);
}

// Verifies Scene owns Terrain and updates it around the primary player position.
TEST_CASE("scene owns terrain and ticks it during scene update") {
    ofg::Scene scene;

    CHECK(scene.terrain().chunk_count() == 0);
    const ofg::Scene& const_scene = scene;
    CHECK(const_scene.terrain().chunk_count() == 0);

    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    ofg::Player* player = player_entity->player();
    REQUIRE(player != nullptr);
    player_entity->local_transform().m_position = ofg::math::vec3(64.0f, 0.0f, -64.0f);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, nullptr, &scene};
    scene.update(context);

    CHECK(scene.terrain().chunk_count() == 25);
    CHECK(terrain_has_chunk(scene.terrain(), 2, -2));
    CHECK(terrain_has_chunk(scene.terrain(), 4, 0));

    scene.clear();
    CHECK(scene.terrain().chunk_count() == 0);
}
