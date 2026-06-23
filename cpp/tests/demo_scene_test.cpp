// Doctest coverage for the generated renderer demo scene.
//
// These tests keep the plane-and-cubes scene deterministic while the renderer
// evolves. They intentionally use CPU-only resources so geometry, material
// layout, mip-chain creation, and draw-list emission stay cheap to validate.
#include "doctest.h"

#include "ofg/render/demo_scene.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resource_arena.hpp"
#include "ofg/resources/texture.hpp"

#include <string>

// Verifies the demo builder creates the expected high-level resources.
TEST_CASE("demo scene builds generated resources with mipmapped textures") {
    ofg::ResourceArena resources;
    ofg::DemoScene scene;
    std::string error;

    REQUIRE_MESSAGE(ofg::build_demo_scene(ofg::GpuContext{}, resources, scene, error), error);

    CHECK(resources.shaders().size() == 1);
    CHECK(resources.textures().size() == 2);
    CHECK(resources.materials().size() == 5);
    CHECK(resources.meshes().size() == 2);
    REQUIRE(scene.m_checker_texture != nullptr);
    REQUIRE(scene.m_white_texture != nullptr);
    CHECK(scene.m_checker_texture->mip_level_count() == 7);
    CHECK(scene.m_white_texture->mip_level_count() == 1);
    CHECK(scene.m_ground_mesh->submeshes()[0].m_default_material == scene.m_ground_material);
    CHECK(scene.m_cube_mesh->submeshes()[0].m_default_material == scene.m_cube_materials[0]);
}

// Verifies the demo updater emits a ground draw plus four animated cube draws.
TEST_CASE("demo scene update emits deterministic plane and cube draw list") {
    ofg::ResourceArena resources;
    ofg::DemoScene scene;
    std::string error;
    REQUIRE_MESSAGE(ofg::build_demo_scene(ofg::GpuContext{}, resources, scene, error), error);

    ofg::DrawList first_draw_list;
    ofg::RenderView first_view;
    REQUIRE_MESSAGE(ofg::update_demo_scene(scene, 0.0, 16.0F / 9.0F, first_draw_list, first_view, error), error);
    REQUIRE(first_draw_list.size() == 5);
    CHECK(first_draw_list.commands()[0].m_mesh == scene.m_ground_mesh);
    CHECK(first_draw_list.commands()[1].m_mesh == scene.m_cube_mesh);
    REQUIRE(first_draw_list.commands()[1].m_material_overrides.size() == 1);
    CHECK(first_draw_list.commands()[1].m_material_overrides[0].m_material == scene.m_cube_materials[0]);
    REQUIRE(first_draw_list.commands()[4].m_material_overrides.size() == 1);
    CHECK(first_draw_list.commands()[4].m_material_overrides[0].m_material == scene.m_cube_materials[3]);

    const float first_y = first_draw_list.commands()[1].m_model[3].y;
    ofg::DrawList second_draw_list;
    ofg::RenderView second_view;
    REQUIRE_MESSAGE(ofg::update_demo_scene(
                        scene, ofg::demo_native_smoke_time_ms(), 16.0F / 9.0F, second_draw_list, second_view, error),
        error);
    CHECK(second_draw_list.commands()[1].m_model[3].y != first_y);

    CHECK(ofg::update_demo_scene(scene, 0.0, 0.0F, second_draw_list, second_view, error) == false);
    CHECK(error.find("positive aspect") != std::string::npos);
}

// Verifies public update validation catches incomplete scene resource pointers.
TEST_CASE("demo scene update reports incomplete scene resources") {
    ofg::DrawList draw_list;
    ofg::RenderView render_view;
    std::string error;

    ofg::DemoScene empty_scene;
    CHECK(ofg::update_demo_scene(empty_scene, 0.0, 16.0F / 9.0F, draw_list, render_view, error) == false);
    CHECK(error.find("resources") != std::string::npos);

    ofg::ResourceArena resources;
    ofg::DemoScene scene;
    REQUIRE_MESSAGE(ofg::build_demo_scene(ofg::GpuContext{}, resources, scene, error), error);
    scene.m_cube_materials[2] = nullptr;
    CHECK(ofg::update_demo_scene(scene, 0.0, 16.0F / 9.0F, draw_list, render_view, error) == false);
    CHECK(error.find("cube materials") != std::string::npos);
}
