// Doctest coverage for CPU-side OFG mesh resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

// Builds a valid material for mesh submesh tests.
ofg::Material make_mesh_material(ofg::Shader& shader) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    ofg::Material material{ofg::GpuContext{}, "material"};
    material.init(shader, properties);
    return material;
}

// Builds a valid shader for mesh submesh tests.
ofg::Shader make_mesh_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    ofg::Shader shader{ofg::GpuContext{}, "shader"};
    shader.init_from_wgsl("source", layout, {});
    return shader;
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

    ofg::Mesh mesh{ofg::GpuContext{}, "mesh"};
    mesh.init(vertices, indices, submeshes);
    CHECK(mesh.label() == "mesh");
    CHECK(mesh.vertices().size() == 3);
    CHECK(mesh.indices().size() == 3);
    CHECK(mesh.submeshes()[0].m_default_material == &material);
    CHECK(mesh.vertex_buffer() == nullptr);
    CHECK(mesh.index_buffer() == nullptr);

    try {
        ofg::Mesh bad_mesh{ofg::GpuContext{}, "bad"};
        bad_mesh.init(vertices, std::vector<std::uint32_t>{0, 4, 2}, submeshes);
        FAIL("Expected mesh index validation to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("missing vertex") != std::string::npos);
    }
}

// Verifies mesh mutation keeps existing data valid.
TEST_CASE("mesh resource validates replacement data") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    ofg::Mesh mesh{ofg::GpuContext{}, "mesh"};
    mesh.init(vertices, std::vector<std::uint32_t>{0, 1, 2}, submeshes);

    try {
        mesh.replace_vertices(std::vector<ofg::MeshVertex>{vertex(0.0F, 0.0F, 0.0F)});
        FAIL("Expected invalid replacement vertices to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("missing vertex") != std::string::npos);
    }

    mesh.replace_vertices(vertices);
    CHECK(mesh.revision() == 2);
    mesh.replace_indices(std::vector<std::uint32_t>{0, 1, 2}, submeshes);
    CHECK(mesh.revision() == 3);

    std::vector<ofg::SubMesh> bad_submeshes{ofg::SubMesh{"triangle", 0, 0, &material}};
    try {
        mesh.replace_indices(std::vector<std::uint32_t>{0, 1, 2}, bad_submeshes);
        FAIL("Expected invalid replacement submesh to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("Submesh index range") != std::string::npos);
    }
}

// Verifies mesh creation catches each required data category.
TEST_CASE("mesh resource rejects incomplete mesh data") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};

    try {
        ofg::Mesh mesh{ofg::GpuContext{}, ""};
        mesh.init(vertices, indices, submeshes);
        FAIL("Expected empty mesh label to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }
    try {
        ofg::Mesh mesh{ofg::GpuContext{}, "bad"};
        mesh.init({}, indices, submeshes);
        FAIL("Expected empty mesh vertices to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("vertices") != std::string::npos);
    }
    try {
        ofg::Mesh mesh{ofg::GpuContext{}, "bad"};
        mesh.init(vertices, {}, submeshes);
        FAIL("Expected empty mesh indices to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("indices") != std::string::npos);
    }
    try {
        ofg::Mesh mesh{ofg::GpuContext{}, "bad"};
        mesh.init(vertices, indices, {});
        FAIL("Expected empty mesh submeshes to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("submesh") != std::string::npos);
    }

    std::vector<ofg::SubMesh> unnamed_submeshes{ofg::SubMesh{"", 0, 3, &material}};
    try {
        ofg::Mesh mesh{ofg::GpuContext{}, "bad"};
        mesh.init(vertices, indices, unnamed_submeshes);
        FAIL("Expected unnamed submesh to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }

    std::vector<ofg::SubMesh> materialless_submeshes{ofg::SubMesh{"triangle", 0, 3, nullptr}};
    try {
        ofg::Mesh mesh{ofg::GpuContext{}, "bad"};
        mesh.init(vertices, indices, materialless_submeshes);
        FAIL("Expected materialless submesh to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("material") != std::string::npos);
    }
}

// Verifies mesh move assignment transfers CPU data and empty GPU handles.
TEST_CASE("mesh resource supports move assignment") {
    ofg::Shader shader = make_mesh_shader();
    ofg::Material material = make_mesh_material(shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    ofg::Mesh destination{ofg::GpuContext{}, "destination"};
    destination.init(vertices, std::vector<std::uint32_t>{0, 1, 2}, submeshes);
    ofg::Mesh source{ofg::GpuContext{}, "source"};
    source.init(vertices, std::vector<std::uint32_t>{2, 1, 0}, submeshes);

    destination = std::move(source);
    CHECK(destination.label() == "source");
    CHECK(destination.indices()[0] == 2);
    CHECK(destination.vertex_buffer() == nullptr);
    CHECK(destination.index_buffer() == nullptr);
}
