// Doctest coverage for the generated renderer demo scene.
//
// These tests keep the plane-and-cubes scene deterministic while the renderer
// evolves. They intentionally use CPU-only resources so geometry, material
// layout, mip-chain creation, and draw-list emission stay cheap to validate.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/texture.hpp"

#include <optional>
#include <string>
#include <utility>

namespace {

struct ResourcesGuard {
    // Releases the static Resources singleton before the borrowed test GPU is destroyed.
    ~ResourcesGuard() {
        ofg::Resources::destroy();
    }
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

} // namespace

// Verifies the demo builder creates the expected high-level resources.
TEST_CASE("demo scene builds generated resources with mipmapped textures") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;

    ofg::build_demo_scene(scene);

    CHECK(ofg::Resources::shaders().size() == 1);
    CHECK(ofg::Resources::textures().size() == 2);
    CHECK(ofg::Resources::materials().size() == 5);
    CHECK(ofg::Resources::meshes().size() == 2);
    REQUIRE(scene.m_checker_texture != nullptr);
    REQUIRE(scene.m_white_texture != nullptr);
    CHECK(scene.m_checker_texture->mip_level_count() == 7);
    CHECK(scene.m_white_texture->mip_level_count() == 1);
    CHECK(scene.m_ground_mesh->submeshes()[0].m_default_material == scene.m_ground_material);
    CHECK(scene.m_cube_mesh->submeshes()[0].m_default_material == scene.m_cube_materials[0]);
}

// Verifies the demo setup creates a ground entity plus four animated cube entities.
TEST_CASE("demo scene setup and update create deterministic plane and cube entities") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ResourcesGuard guard;
    init_test_resources(gpu.borrowed_context());
    ofg::DemoScene scene;
    ofg::build_demo_scene(scene);

    ofg::Scene first_scene;
    REQUIRE_NOTHROW(ofg::setup_demo_scene(scene, first_scene));
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, 0.0, 16.0f / 9.0f, first_scene));
    REQUIRE(first_scene.entity_count() == 6);
    REQUIRE(first_scene.mesh_renderer_count() == 5);
    REQUIRE(scene.m_ground_renderer != nullptr);
    CHECK(first_scene.get_mesh_renderer(0) == scene.m_ground_renderer);
    CHECK(scene.m_ground_renderer->m_mesh == scene.m_ground_mesh);
    CHECK(first_scene.get_mesh_renderer(1)->m_mesh == scene.m_cube_mesh);
    CHECK(first_scene.get_mesh_renderer(1)->m_sort_origin_offset.x == doctest::Approx(0.0f));
    CHECK(first_scene.get_mesh_renderer(1)->m_sort_origin_offset.y == doctest::Approx(0.0f));
    CHECK(first_scene.get_mesh_renderer(1)->m_sort_origin_offset.z == doctest::Approx(0.0f));
    REQUIRE(first_scene.get_mesh_renderer(1)->m_material_overrides.size() == 1);
    CHECK(first_scene.get_mesh_renderer(1)->m_material_overrides[0].m_material == scene.m_cube_materials[0]);
    REQUIRE(first_scene.get_mesh_renderer(4)->m_material_overrides.size() == 1);
    CHECK(first_scene.get_mesh_renderer(4)->m_material_overrides[0].m_material == scene.m_cube_materials[3]);

    REQUIRE(scene.m_cube_entities[0] != nullptr);
    const float first_y = scene.m_cube_entities[0]->local_transform().m_position.y;
    REQUIRE_NOTHROW(ofg::update_demo_scene(scene, ofg::demo_native_smoke_time_ms(), 16.0f / 9.0f, first_scene));
    CHECK(scene.m_cube_entities[0]->local_transform().m_position.y != first_y);

    CHECK_THROWS_WITH_AS(
        ofg::update_demo_scene(scene, 0.0, 0.0f, first_scene), doctest::Contains("positive aspect"), ofg::EngineError);
}

// Verifies public update validation catches incomplete scene resource pointers.
TEST_CASE("demo scene update reports incomplete scene resources") {
    ofg::Scene render_scene;

    ofg::DemoScene empty_scene;
    CHECK_THROWS_WITH_AS(ofg::update_demo_scene(empty_scene, 0.0, 16.0f / 9.0f, render_scene),
        doctest::Contains("resources"),
        ofg::EngineError);

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

    CHECK_THROWS_WITH_AS(ofg::update_demo_scene(scene, 0.0, 16.0f / 9.0f, render_scene),
        doctest::Contains("bindings"),
        ofg::EngineError);
}
