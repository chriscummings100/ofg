// Doctest coverage for the generated renderer demo scene.
//
// These tests keep the large default validation scene deterministic while the renderer
// evolves. They intentionally use CPU-only resources so geometry, material
// layout, mip-chain creation, and draw-list emission stay cheap to validate.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/debug/debug_menu.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/player.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float _pi = 3.14159265358979323846f;
constexpr const char* _terrain_debug_plane_path = "terrain/show_debug_plane";

struct ResourcesGuard {
    // Releases the static Resources singleton before the borrowed test GPU is destroyed.
    ~ResourcesGuard() {
        ofg::Resources::destroy();
    }
};

struct TerrainDebugPlaneGuard {
    // Restores the shared debug-menu terrain mode after a test mutates it.
    ~TerrainDebugPlaneGuard() {
        (void)ofg::DebugMenu::instance().set_bool(_terrain_debug_plane_path, m_original_value);
    }

    bool m_original_value{ofg::DebugMenu::instance().get_bool(_terrain_debug_plane_path).value_or(false)};
};

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

// Initializes the static Resources facade for a demo-scene test.
void init_test_resources(ofg::GpuContext gpu) {
    ofg::Resources::destroy();
    ofg::Resources::create(std::move(gpu));
    REQUIRE(ofg::Resources::prepare());
}

// Checks two Mat4 values component-wise with a tight floating-point tolerance.
void check_mat4_close(ofg::math::Mat4 actual, ofg::math::Mat4 expected) {
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            CHECK(actual[column][row] == doctest::Approx(expected[column][row]).epsilon(0.0001));
        }
    }
}

} // namespace

// Verifies the demo builder creates the expected high-level resources.
TEST_CASE("demo scene builds generated resources with mipmapped textures") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;

    ofg::build_demo_scene(scene);

    CHECK(ofg::Resources::shaders().size() == 2);
    CHECK(ofg::Resources::textures().size() == 4);
    CHECK(ofg::Resources::materials().size() == 7);
    CHECK(ofg::Resources::meshes().size() == 1);
    REQUIRE(scene.m_white_texture != nullptr);
    REQUIRE(scene.m_neutral_metallic_roughness_texture != nullptr);
    REQUIRE(scene.m_flat_normal_texture != nullptr);
    REQUIRE(scene.m_player_material != nullptr);
    REQUIRE(scene.m_terrain_material != nullptr);
    REQUIRE(scene.m_terrain_debug_material != nullptr);
    CHECK(scene.m_white_texture->mip_level_count() == 1);
    CHECK(scene.m_neutral_metallic_roughness_texture->pixel_format() == ofg::TexturePixelFormat::Rgba8);
    CHECK(scene.m_flat_normal_texture->pixel_format() == ofg::TexturePixelFormat::Rgba8);
    CHECK(scene.m_cube_mesh->submeshes()[0].m_default_material == scene.m_cube_materials[0]);
}

// Verifies the demo setup creates a camera, player, terrain chunks, and deterministic boxes.
TEST_CASE("demo scene setup and update create terrain player and box entities") {
    TerrainDebugPlaneGuard terrain_debug_plane_guard;
    REQUIRE(ofg::DebugMenu::instance().set_bool(_terrain_debug_plane_path, false));
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;
    ofg::build_demo_scene(scene);
    const ofg::DemoSceneValidationStats stats = ofg::demo_scene_validation_stats();
    REQUIRE(stats.m_box_count >= 150);
    CHECK(stats.m_near_box_count >= 20);
    CHECK(stats.m_mid_box_count >= 50);
    CHECK(stats.m_far_box_count >= 50);
    CHECK(stats.m_near_box_count + stats.m_mid_box_count + stats.m_far_box_count == stats.m_box_count);
    CHECK(stats.m_partly_below_ground_count >= 12);
    CHECK(stats.m_overlap_cluster_box_count >= 20);
    CHECK(stats.m_off_camera_candidate_count >= 12);

    ofg::Scene first_scene;
    REQUIRE_NOTHROW(ofg::setup_demo_scene(scene, first_scene));
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, 0.0, first_scene));
    const std::size_t terrain_chunk_count =
        static_cast<std::size_t>(ofg::terrain_initial_surface_radius_chunks * 2 + 1) *
        static_cast<std::size_t>(ofg::terrain_initial_surface_radius_chunks * 2 + 1);
    REQUIRE(scene.m_cube_entities.size() == stats.m_box_count);
    REQUIRE(scene.m_cube_renderers.size() == stats.m_box_count);
    REQUIRE(first_scene.terrain().chunk_count() == terrain_chunk_count);
    CHECK(first_scene.terrain().material() == scene.m_terrain_material);
    CHECK(first_scene.terrain().debug_plane_material() == scene.m_terrain_debug_material);
    CHECK(first_scene.terrain().render_mode() == ofg::TerrainRenderMode::ClayMesh);
    CHECK(ofg::Resources::textures().size() == 4U);
    CHECK(ofg::Resources::materials().size() == 7U);
    CHECK(ofg::Resources::meshes().size() == 1U + terrain_chunk_count);
    REQUIRE(first_scene.entity_count() == 5U + stats.m_box_count);
    REQUIRE(first_scene.camera_count() == 1);
    REQUIRE(first_scene.player_count() == 1);
    REQUIRE(first_scene.light_count() == 1);
    REQUIRE(first_scene.mesh_renderer_count() == 1U + stats.m_box_count);
    REQUIRE(first_scene.main_camera() != nullptr);
    CHECK(first_scene.main_camera() == first_scene.get_camera(0));
    REQUIRE(first_scene.environment().main_directional_light() != nullptr);
    CHECK(first_scene.environment().main_directional_light() == first_scene.get_light(0));
    CHECK(first_scene.get_light(0)->enabled());
    CHECK(first_scene.environment().ambient_light().m_intensity > 0.0f);
    const std::size_t terrain_entity_count = first_scene.entity_count();
    REQUIRE(scene.m_player != nullptr);
    REQUIRE(scene.m_player_entity != nullptr);
    REQUIRE(scene.m_player_visual_entity != nullptr);
    REQUIRE(scene.m_player_renderer != nullptr);
    CHECK(first_scene.get_player(0) == scene.m_player);
    CHECK(first_scene.get_mesh_renderer(0) == scene.m_player_renderer);
    CHECK(scene.m_player_visual_entity->parent() == scene.m_player_entity);
    CHECK(scene.m_player_renderer->mesh() == scene.m_cube_mesh);
    CHECK(scene.m_player_renderer->visible());
    REQUIRE(scene.m_player_renderer->material_overrides().size() == 1);
    CHECK(scene.m_player_renderer->material_overrides()[0].m_material == scene.m_player_material);
    CHECK(scene.m_player_entity->local_transform().m_position.y == doctest::Approx(0.9f));
    CHECK(scene.m_player_entity->local_transform().m_scale.x == doctest::Approx(1.0f));
    CHECK(scene.m_player_entity->local_transform().m_scale.y == doctest::Approx(1.0f));
    CHECK(scene.m_player_entity->local_transform().m_scale.z == doctest::Approx(1.0f));
    CHECK(scene.m_player_visual_entity->local_transform().m_scale.x == doctest::Approx(0.6f));
    CHECK(scene.m_player_visual_entity->local_transform().m_scale.y == doctest::Approx(1.8f));
    CHECK(scene.m_player_visual_entity->local_transform().m_scale.z == doctest::Approx(0.35f));
    const ofg::TerrainChunk* first_chunk = first_scene.terrain().find_chunk(ofg::TerrainChunkId{0, 0, 0, 0});
    REQUIRE(first_chunk != nullptr);
    CHECK(first_chunk->has_heightfield());
    REQUIRE(first_chunk->render_mesh() != nullptr);
    CHECK(first_chunk->debug_plane_mesh() == nullptr);
    CHECK(first_chunk->debug_plane_texture() == nullptr);
    CHECK(first_chunk->debug_plane_material_overrides().empty());
    CHECK(first_chunk->render_mesh()->vertices().size() ==
          static_cast<std::size_t>(
              ofg::terrain_chunk_lod0_vertices_per_edge * ofg::terrain_chunk_lod0_vertices_per_edge));
    CHECK(
        first_chunk->render_mesh()->indices().size() ==
        static_cast<std::size_t>(ofg::terrain_chunk_lod0_cells_per_edge * ofg::terrain_chunk_lod0_cells_per_edge * 6));
    REQUIRE(first_chunk->render_mesh()->submeshes().size() == 1);
    CHECK(first_chunk->render_mesh()->submeshes()[0].m_default_material == scene.m_terrain_material);
    CHECK(first_chunk->render_mesh()->vertices()[0].m_position[0] == doctest::Approx(0.0f));
    CHECK(first_chunk->render_mesh()->vertices()[0].m_position[1] ==
          doctest::Approx(first_chunk->heightfield_sample_at(0, 0).m_height));
    CHECK(first_chunk->render_mesh()->vertices()[0].m_position[2] == doctest::Approx(0.0f));
    CHECK(first_chunk->render_mesh()->vertices()[ofg::terrain_chunk_lod0_vertices_per_edge - 1].m_position[0] ==
          doctest::Approx(static_cast<float>(ofg::terrain_chunk_lod0_cells_per_edge)));
    std::vector<ofg::RenderObject> render_objects;
    ofg::RenderObjectExtractionStats extraction_stats;
    REQUIRE_NOTHROW(ofg::extract_render_objects(first_scene, render_objects, extraction_stats));
    REQUIRE(render_objects.size() == first_scene.mesh_renderer_count() + terrain_chunk_count);
    CHECK(extraction_stats.m_extracted_object_count == render_objects.size());
    const ofg::TerrainChunk& first_streamed_chunk = first_scene.terrain().chunks().begin()->second;
    const ofg::RenderObject& first_terrain_object = render_objects[first_scene.mesh_renderer_count()];
    CHECK(first_terrain_object.m_mesh == first_streamed_chunk.render_mesh());
    CHECK(first_terrain_object.m_material_overrides.empty());
    REQUIRE(ofg::DebugMenu::instance().set_bool(_terrain_debug_plane_path, true));
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, 125.0, first_scene));
    CHECK(first_scene.terrain().render_mode() == ofg::TerrainRenderMode::HeightDebugPlane);
    first_scene.terrain().tick(ofg::TerrainTickContext{scene.m_player_entity->local_transform().m_position});
    const ofg::TerrainChunk& first_debug_chunk = first_scene.terrain().chunks().begin()->second;
    REQUIRE(first_debug_chunk.debug_plane_mesh() != nullptr);
    REQUIRE(first_debug_chunk.debug_plane_texture() != nullptr);
    CHECK(first_debug_chunk.debug_plane_texture()->pixel_format() == ofg::TexturePixelFormat::R16Float);
    REQUIRE(first_debug_chunk.debug_plane_material_overrides().size() == 1);
    render_objects.clear();
    extraction_stats = ofg::RenderObjectExtractionStats{};
    REQUIRE_NOTHROW(ofg::extract_render_objects(first_scene, render_objects, extraction_stats));
    REQUIRE(render_objects.size() == first_scene.mesh_renderer_count() + terrain_chunk_count);
    const ofg::RenderObject& first_debug_terrain_object = render_objects[first_scene.mesh_renderer_count()];
    CHECK(first_debug_terrain_object.m_mesh == first_debug_chunk.debug_plane_mesh());
    REQUIRE(first_debug_terrain_object.m_material_overrides.size() == 1);
    CHECK(first_debug_terrain_object.m_material_overrides[0].m_submesh_index == 0);
    CHECK(first_debug_terrain_object.m_material_overrides[0].m_material.get() ==
          first_debug_chunk.debug_plane_material_overrides()[0].m_material.get());
    REQUIRE(ofg::DebugMenu::instance().set_bool(_terrain_debug_plane_path, false));
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, 150.0, first_scene));
    CHECK(first_scene.terrain().render_mode() == ofg::TerrainRenderMode::ClayMesh);
    REQUIRE(scene.m_cube_renderers[0] != nullptr);
    CHECK(scene.m_cube_renderers[0]->mesh() == scene.m_cube_mesh);
    CHECK(scene.m_cube_renderers[0]->sort_origin_offset().x == doctest::Approx(0.0f));
    CHECK(scene.m_cube_renderers[0]->sort_origin_offset().y == doctest::Approx(0.0f));
    CHECK(scene.m_cube_renderers[0]->sort_origin_offset().z == doctest::Approx(0.0f));
    REQUIRE(scene.m_cube_renderers[0]->material_overrides().size() == 1);
    CHECK(scene.m_cube_renderers[0]->material_overrides()[0].m_material == scene.m_cube_materials[0]);
    REQUIRE(scene.m_cube_renderers.back() != nullptr);
    CHECK(scene.m_cube_renderers.back()->mesh() == scene.m_cube_mesh);
    REQUIRE(scene.m_cube_renderers.back()->material_overrides().size() == 1);
    CHECK(scene.m_cube_renderers.back()->material_overrides()[0].m_material == scene.m_cube_materials[3]);

    first_scene.terrain().tick(ofg::TerrainTickContext{ofg::math::vec3(33.0f, 0.0f, 0.0f)});
    REQUIRE(first_scene.terrain().chunk_count() == terrain_chunk_count);
    CHECK(first_scene.terrain().find_chunk(ofg::TerrainChunkId{0, -2, 0, -2}) == nullptr);
    CHECK(first_scene.terrain().find_chunk(ofg::TerrainChunkId{0, 3, 0, -2}) != nullptr);
    CHECK(first_scene.entity_count() == terrain_entity_count);
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, 250.0, first_scene));

    const ofg::Camera* camera = first_scene.main_camera();
    REQUIRE(camera != nullptr);
    const ofg::CameraProperties camera_properties = camera->camera_properties(16.0f / 9.0f);
    CHECK(camera_properties.camera == camera);
    std::string error;
    const std::optional<ofg::math::Mat4> view = ofg::math::look_at_lh(
        ofg::math::vec3(6.2f, 4.4f, 7.6f), ofg::math::vec3(0.0f, 1.9f, 0.0f), ofg::math::vec3(0.0f, 1.0f, 0.0f), error);
    REQUIRE(view.has_value());
    const std::optional<ofg::math::Mat4> projection =
        ofg::math::perspective_lh(55.0f * _pi / 180.0f, 16.0f / 9.0f, 0.1f, 80.0f, error);
    REQUIRE(projection.has_value());
    check_mat4_close(camera_properties.clip_from_world, ofg::math::mul(*projection, *view));

    REQUIRE(scene.m_cube_entities[0] != nullptr);
    const float first_y = scene.m_cube_entities[0]->local_transform().m_position.y;
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, ofg::demo_native_smoke_time_ms(), first_scene));
    CHECK(scene.m_cube_entities[0]->local_transform().m_position.y != first_y);

    CHECK_THROWS_WITH_AS(ofg::update_demo_scene(scene, std::numeric_limits<double>::infinity(), first_scene),
        doctest::Contains("finite time"),
        ofg::EngineError);
}

// Verifies public update validation catches incomplete scene resource pointers.
TEST_CASE("demo scene update reports incomplete scene resources") {
    ofg::Scene render_scene;

    ofg::DemoScene empty_scene;
    CHECK_THROWS_WITH_AS(
        ofg::update_demo_scene(empty_scene, 0.0, render_scene), doctest::Contains("resources"), ofg::EngineError);

    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;
    ofg::build_demo_scene(scene);
    scene.m_cube_materials[2] = nullptr;
    CHECK_THROWS_WITH_AS(
        ofg::setup_demo_scene(scene, render_scene), doctest::Contains("cube materials"), ofg::EngineError);
}

// Verifies update rejects scenes whose cached entity pointers were invalidated.
TEST_CASE("demo scene update reports stale entity bindings") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;
    ofg::build_demo_scene(scene);

    ofg::Scene render_scene;
    ofg::setup_demo_scene(scene, render_scene);
    render_scene.clear();

    CHECK_THROWS_WITH_AS(
        ofg::update_demo_scene(scene, 0.0, render_scene), doctest::Contains("bindings"), ofg::EngineError);
}
