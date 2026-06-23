// Doctest coverage for OFG draw-list validation and material resolution.
#include "doctest.h"

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <optional>
#include <string>
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

    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(ofg::GpuContext{}, "draw shader", "source", layout, {}, error);
    REQUIRE_MESSAGE(shader.has_value(), error);
    return std::move(*shader);
}

// Builds a CPU-only material with a base color property.
ofg::Material make_draw_material(ofg::Shader& shader, std::string label, ofg::math::Vec4 color) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", color);

    std::string error;
    std::optional<ofg::Material> material =
        ofg::Material::create(ofg::GpuContext{}, std::move(label), shader, std::move(properties), error);
    REQUIRE_MESSAGE(material.has_value(), error);
    return std::move(*material);
}

// Builds a quad mesh split into two submeshes.
ofg::Mesh make_two_submesh_mesh(ofg::Material& material) {
    std::vector<ofg::MeshVertex> vertices{
        ofg::MeshVertex{{-1.0F, -1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
        ofg::MeshVertex{{1.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
        ofg::MeshVertex{{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
        ofg::MeshVertex{{-1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}},
    };
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
    std::vector<ofg::SubMesh> submeshes{
        ofg::SubMesh{"first triangle", 0, 3, &material},
        ofg::SubMesh{"second triangle", 3, 3, &material},
    };

    std::string error;
    std::optional<ofg::Mesh> mesh = ofg::Mesh::create(
        ofg::GpuContext{}, "two submesh mesh", std::move(vertices), std::move(indices), submeshes, error);
    REQUIRE_MESSAGE(mesh.has_value(), error);
    return std::move(*mesh);
}

// Builds a draw command with explicit model transform and optional draw tint.
ofg::DrawCommand make_draw_command(ofg::Mesh& mesh, bool include_tint) {
    ofg::DrawCommand command;
    command.m_mesh = &mesh;
    command.m_model = ofg::math::mat4_identity();
    if (include_tint) {
        command.m_properties.set("object_tint", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    }
    return command;
}

} // namespace

// Verifies commands stay in stable insertion order and can be cleared.
TEST_CASE("draw list preserves stable command order") {
    ofg::Shader shader = make_draw_shader(false);
    ofg::Material material = make_draw_material(shader, "white", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    ofg::Mesh mesh = make_two_submesh_mesh(material);

    ofg::DrawCommand first = make_draw_command(mesh, false);
    ofg::DrawCommand second = make_draw_command(mesh, false);
    first.m_sort_origin = ofg::math::vec3(1.0F, 0.0F, 0.0F);
    second.m_sort_origin = ofg::math::vec3(2.0F, 0.0F, 0.0F);

    ofg::DrawList draw_list;
    draw_list.add(std::move(first));
    draw_list.add(std::move(second));

    std::string error;
    REQUIRE_MESSAGE(draw_list.validate(error), error);
    CHECK(draw_list.size() == 2);
    CHECK(draw_list.commands()[0].m_sort_origin.x == 1.0F);
    CHECK(draw_list.commands()[1].m_sort_origin.x == 2.0F);

    draw_list.clear();
    CHECK(draw_list.size() == 0);
}

// Verifies material override resolution falls back to submesh defaults and applies overrides.
TEST_CASE("draw list resolves submesh material overrides") {
    ofg::Shader shader = make_draw_shader(false);
    ofg::Material default_material = make_draw_material(shader, "default", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    ofg::Material override_material = make_draw_material(shader, "override", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F));
    ofg::Mesh mesh = make_two_submesh_mesh(default_material);

    ofg::DrawCommand command = make_draw_command(mesh, false);
    command.m_material_overrides.push_back(ofg::MaterialOverride{1, &override_material});

    std::string error;
    CHECK(resolve_material(command, 0, error) == &default_material);
    CHECK(resolve_material(command, 1, error) == &override_material);
}

// Verifies validation catches invalid meshes, overrides, and draw properties.
TEST_CASE("draw list validates command resources and draw property bags") {
    ofg::Shader shader = make_draw_shader(true);
    ofg::Material material = make_draw_material(shader, "white", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    ofg::Mesh mesh = make_two_submesh_mesh(material);

    std::string error;
    ofg::DrawList missing_mesh;
    missing_mesh.add(ofg::DrawCommand{});
    CHECK(missing_mesh.validate(error) == false);
    CHECK(error.find("mesh") != std::string::npos);

    ofg::DrawCommand command = make_draw_command(mesh, true);
    command.m_material_overrides.push_back(ofg::MaterialOverride{2, &material});
    ofg::DrawList bad_override;
    bad_override.add(std::move(command));
    CHECK(bad_override.validate(error) == false);
    CHECK(error.find("missing submesh") != std::string::npos);

    ofg::DrawCommand null_override = make_draw_command(mesh, true);
    null_override.m_material_overrides.push_back(ofg::MaterialOverride{0, nullptr});
    ofg::DrawList bad_null_override;
    bad_null_override.add(std::move(null_override));
    CHECK(bad_null_override.validate(error) == false);
    CHECK(error.find("override") != std::string::npos);

    ofg::DrawList missing_draw_property;
    missing_draw_property.add(make_draw_command(mesh, false));
    CHECK(missing_draw_property.validate(error) == false);
    CHECK(error.find("Missing required") != std::string::npos);

    ofg::DrawCommand undeclared_property = make_draw_command(mesh, true);
    undeclared_property.m_properties.set("undeclared", 1.0F);
    ofg::DrawList bad_property;
    bad_property.add(std::move(undeclared_property));
    CHECK(bad_property.validate(error) == false);
    CHECK(error.find("not declared") != std::string::npos);

    ofg::DrawCommand direct_resolve = make_draw_command(mesh, true);
    CHECK(resolve_material(direct_resolve, 4, error) == nullptr);
    CHECK(error.find("missing submesh") != std::string::npos);
    direct_resolve.m_material_overrides.push_back(ofg::MaterialOverride{0, nullptr});
    CHECK(resolve_material(direct_resolve, 0, error) == nullptr);
    CHECK(error.find("override") != std::string::npos);
    ofg::DrawCommand missing_direct_mesh;
    CHECK(resolve_material(missing_direct_mesh, 0, error) == nullptr);
    CHECK(error.find("mesh") != std::string::npos);
}
