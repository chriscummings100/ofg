// Doctest coverage for OFG draw-list validation and material resolution.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

// Builds a shader schema with material color plus optional model draw data.
ofg::Shader make_draw_shader(bool require_object_tint) {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw, 0, false});
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "object_tint", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Draw, 0, require_object_tint});

    ofg::Shader shader{ofg::GpuContext{}, "draw shader"};
    shader.init_from_wgsl("source", layout, {});
    return shader;
}

// Builds a CPU-only material with a base color property.
ofg::Material make_draw_material(ofg::Shader& shader, std::string label, ofg::math::Vec4 color) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", color);

    ofg::Material material{ofg::GpuContext{}, std::move(label)};
    material.init(shader, std::move(properties));
    return material;
}

// Builds a quad mesh split into two submeshes.
ofg::Mesh make_two_submesh_mesh(ofg::Material& material) {
    std::vector<ofg::MeshVertex> vertices{
        ofg::MeshVertex{{-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        ofg::MeshVertex{{1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        ofg::MeshVertex{{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        ofg::MeshVertex{{-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    };
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    std::vector<ofg::SubMesh> submeshes{
        ofg::SubMesh{"first triangle", 0, 3, &material},
        ofg::SubMesh{"second triangle", 3, 3, &material},
    };

    ofg::Mesh mesh{ofg::GpuContext{}, "two submesh mesh"};
    mesh.init(std::move(vertices), std::move(indices), submeshes);
    return mesh;
}

// Builds draw properties with the required object tint.
ofg::PropertyBag make_draw_properties() {
    ofg::PropertyBag properties;
    properties.set("object_tint", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    return properties;
}

// Builds a draw command with explicit model transform and optional draw properties.
ofg::DrawCommand make_draw_command(ofg::Mesh& mesh, const ofg::PropertyBag* properties) {
    ofg::DrawCommand command;
    command.m_mesh = &mesh;
    command.m_model = ofg::math::mat4_identity();
    command.m_properties = properties;
    return command;
}

} // namespace

// Verifies commands stay in stable insertion order and can be cleared.
TEST_CASE("draw list preserves stable command order") {
    ofg::Shader shader = make_draw_shader(false);
    ofg::Material material = make_draw_material(shader, "white", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    ofg::Mesh mesh = make_two_submesh_mesh(material);

    ofg::DrawCommand first = make_draw_command(mesh, nullptr);
    ofg::DrawCommand second = make_draw_command(mesh, nullptr);
    first.m_sort_origin = ofg::math::vec3(1.0f, 0.0f, 0.0f);
    second.m_sort_origin = ofg::math::vec3(2.0f, 0.0f, 0.0f);

    ofg::DrawList draw_list;
    draw_list.add(std::move(first));
    draw_list.add(std::move(second));

    REQUIRE_NOTHROW(draw_list.validate());
    CHECK(draw_list.size() == 2);
    CHECK(draw_list.commands()[0].m_sort_origin.x == 1.0f);
    CHECK(draw_list.commands()[1].m_sort_origin.x == 2.0f);

    draw_list.clear();
    CHECK(draw_list.size() == 0);
}

// Verifies material override resolution falls back to submesh defaults and applies overrides.
TEST_CASE("draw list resolves submesh material overrides") {
    ofg::Shader shader = make_draw_shader(false);
    ofg::Material default_material = make_draw_material(shader, "default", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    ofg::Material override_material = make_draw_material(shader, "override", ofg::math::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    ofg::Mesh mesh = make_two_submesh_mesh(default_material);

    std::vector<ofg::MaterialOverride> overrides{ofg::MaterialOverride{1, &override_material}};
    ofg::DrawCommand command = make_draw_command(mesh, nullptr);
    command.m_material_overrides = std::span<const ofg::MaterialOverride>(overrides.data(), overrides.size());

    CHECK(&resolve_material(command, 0) == &default_material);
    CHECK(&resolve_material(command, 1) == &override_material);
}

// Verifies validation catches invalid meshes, overrides, and draw properties.
TEST_CASE("draw list validates command resources and draw property bags") {
    ofg::Shader shader = make_draw_shader(true);
    ofg::Material material = make_draw_material(shader, "white", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    ofg::Mesh mesh = make_two_submesh_mesh(material);

    ofg::DrawList missing_mesh;
    missing_mesh.add(ofg::DrawCommand{});
    CHECK_THROWS_WITH_AS(missing_mesh.validate(), doctest::Contains("mesh"), ofg::EngineError);

    ofg::PropertyBag draw_properties = make_draw_properties();
    ofg::DrawCommand command = make_draw_command(mesh, &draw_properties);
    std::vector<ofg::MaterialOverride> bad_override_values{ofg::MaterialOverride{2, &material}};
    command.m_material_overrides =
        std::span<const ofg::MaterialOverride>(bad_override_values.data(), bad_override_values.size());
    ofg::DrawList bad_override;
    bad_override.add(std::move(command));
    CHECK_THROWS_WITH_AS(bad_override.validate(), doctest::Contains("missing submesh"), ofg::EngineError);

    ofg::DrawCommand null_override = make_draw_command(mesh, &draw_properties);
    std::vector<ofg::MaterialOverride> null_override_values{ofg::MaterialOverride{0, nullptr}};
    null_override.m_material_overrides =
        std::span<const ofg::MaterialOverride>(null_override_values.data(), null_override_values.size());
    ofg::DrawList bad_null_override;
    bad_null_override.add(std::move(null_override));
    CHECK_THROWS_WITH_AS(bad_null_override.validate(), doctest::Contains("override"), ofg::EngineError);

    ofg::DrawList missing_draw_property;
    missing_draw_property.add(make_draw_command(mesh, nullptr));
    CHECK_THROWS_WITH_AS(missing_draw_property.validate(), doctest::Contains("Missing required"), ofg::EngineError);

    ofg::PropertyBag undeclared_properties = make_draw_properties();
    undeclared_properties.set("undeclared", 1.0f);
    ofg::DrawCommand undeclared_property = make_draw_command(mesh, &undeclared_properties);
    ofg::DrawList bad_property;
    bad_property.add(std::move(undeclared_property));
    CHECK_THROWS_WITH_AS(bad_property.validate(), doctest::Contains("not declared"), ofg::EngineError);

    ofg::DrawCommand direct_resolve = make_draw_command(mesh, &draw_properties);
    CHECK_THROWS_WITH_AS([&direct_resolve]() { (void)resolve_material(direct_resolve, 4); }(),
        doctest::Contains("missing submesh"),
        ofg::EngineError);
    std::vector<ofg::MaterialOverride> direct_override_values{ofg::MaterialOverride{0, nullptr}};
    direct_resolve.m_material_overrides =
        std::span<const ofg::MaterialOverride>(direct_override_values.data(), direct_override_values.size());
    CHECK_THROWS_WITH_AS([&direct_resolve]() { (void)resolve_material(direct_resolve, 0); }(),
        doctest::Contains("override"),
        ofg::EngineError);
    ofg::DrawCommand missing_direct_mesh;
    CHECK_THROWS_WITH_AS([&missing_direct_mesh]() { (void)resolve_material(missing_direct_mesh, 0); }(),
        doctest::Contains("mesh"),
        ofg::EngineError);
}
