// Doctest coverage for CPU-side OFG mesh resources.
#include "doctest.h"

#include "ofg/math/vec.hpp"
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

// Builds a valid material for mesh submesh tests.
ofg::Material make_mesh_material(ofg::Shader& shader) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    std::string error;
    std::optional<ofg::Material> material =
        ofg::Material::create(ofg::GpuContext{}, "material", shader, properties, error);
    REQUIRE(material.has_value());
    return std::move(*material);
}

// Builds a valid shader for mesh submesh tests.
ofg::Shader make_mesh_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    std::string error;
    std::optional<ofg::Shader> shader = ofg::Shader::create(ofg::GpuContext{}, "shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());
    return std::move(*shader);
}

// Builds a triangle vertex for tests.
ofg::MeshVertex vertex(float x, float y, float z) {
    return ofg::MeshVertex{{x, y, z}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}};
}

} // namespace

// Verifies mesh creation validates vertices, indices, and submesh ranges.
TEST_CASE("mesh resource validates indexed submeshes") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};

    std::string error;
    std::optional<ofg::Mesh> mesh = ofg::Mesh::create(ofg::GpuContext{}, "mesh", vertices, indices, submeshes, error);
    REQUIRE(mesh.has_value());
    CHECK(mesh->label() == "mesh");
    CHECK(mesh->vertices().size() == 3);
    CHECK(mesh->indices().size() == 3);
    CHECK(mesh->submeshes()[0].m_default_material == &material);
    CHECK(mesh->vertex_buffer() == nullptr);
    CHECK(mesh->index_buffer() == nullptr);

    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "bad", vertices, std::vector<std::uint32_t>{0, 4, 2}, submeshes, error)
              .has_value() == false);
    CHECK(error.find("missing vertex") != std::string::npos);
}

// Verifies mesh mutation keeps existing data valid.
TEST_CASE("mesh resource validates replacement data") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    std::string error;
    std::optional<ofg::Mesh> mesh =
        ofg::Mesh::create(ofg::GpuContext{}, "mesh", vertices, std::vector<std::uint32_t>{0, 1, 2}, submeshes, error);
    REQUIRE(mesh.has_value());

    CHECK(mesh->replace_vertices(std::vector<ofg::MeshVertex>{vertex(0.0F, 0.0F, 0.0F)}, error) == false);
    CHECK(error.find("missing vertex") != std::string::npos);

    REQUIRE(mesh->replace_vertices(vertices, error));
    CHECK(mesh->revision() == 2);
    REQUIRE(mesh->replace_indices(std::vector<std::uint32_t>{0, 1, 2}, submeshes, error));
    CHECK(mesh->revision() == 3);

    std::vector<ofg::SubMesh> bad_submeshes{ofg::SubMesh{"triangle", 0, 0, &material}};
    CHECK(mesh->replace_indices(std::vector<std::uint32_t>{0, 1, 2}, bad_submeshes, error) == false);
    CHECK(error.find("Submesh index range") != std::string::npos);
}

// Verifies mesh creation catches each required data category.
TEST_CASE("mesh resource rejects incomplete mesh data") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};

    std::string error;
    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "", vertices, indices, submeshes, error).has_value() == false);
    CHECK(error.find("label") != std::string::npos);
    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "bad", {}, indices, submeshes, error).has_value() == false);
    CHECK(error.find("vertices") != std::string::npos);
    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "bad", vertices, {}, submeshes, error).has_value() == false);
    CHECK(error.find("indices") != std::string::npos);
    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "bad", vertices, indices, {}, error).has_value() == false);
    CHECK(error.find("submesh") != std::string::npos);

    std::vector<ofg::SubMesh> unnamed_submeshes{ofg::SubMesh{"", 0, 3, &material}};
    CHECK(
        ofg::Mesh::create(ofg::GpuContext{}, "bad", vertices, indices, unnamed_submeshes, error).has_value() == false);
    CHECK(error.find("label") != std::string::npos);

    std::vector<ofg::SubMesh> materialless_submeshes{ofg::SubMesh{"triangle", 0, 3, nullptr}};
    CHECK(ofg::Mesh::create(ofg::GpuContext{}, "bad", vertices, indices, materialless_submeshes, error).has_value() ==
          false);
    CHECK(error.find("material") != std::string::npos);
}

// Verifies mesh move assignment transfers CPU data and empty GPU handles.
TEST_CASE("mesh resource supports move assignment") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    std::string error;
    std::optional<ofg::Mesh> destination = ofg::Mesh::create(
        ofg::GpuContext{}, "destination", vertices, std::vector<std::uint32_t>{0, 1, 2}, submeshes, error);
    std::optional<ofg::Mesh> source =
        ofg::Mesh::create(ofg::GpuContext{}, "source", vertices, std::vector<std::uint32_t>{2, 1, 0}, submeshes, error);
    REQUIRE(destination.has_value());
    REQUIRE(source.has_value());

    *destination = std::move(*source);
    CHECK(destination->label() == "source");
    CHECK(destination->indices()[0] == 2);
    CHECK(destination->vertex_buffer() == nullptr);
    CHECK(destination->index_buffer() == nullptr);
}
