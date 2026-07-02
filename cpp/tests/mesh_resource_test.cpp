// Doctest coverage for CPU-side OFG mesh resources.
#include "doctest.h"

#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Builds a valid material for mesh submesh tests.
std::unique_ptr<ofg::Material> make_mesh_material(ofg::Shader& shader) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    auto material = std::make_unique<ofg::Material>(ofg::GpuContext{}, "material");
    material->init(shader, properties);
    return material;
}

// Builds a valid shader for mesh submesh tests.
std::unique_ptr<ofg::Shader> make_mesh_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    auto shader = std::make_unique<ofg::Shader>(ofg::GpuContext{}, "shader");
    shader->init_from_wgsl("source", layout, {});
    return shader;
}

// Builds a triangle vertex for tests.
ofg::MeshVertex vertex(float x, float y, float z) {
    return ofg::MeshVertex{{x, y, z}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}};
}

} // namespace

// Verifies mesh creation validates vertices, indices, and submesh ranges.
TEST_CASE("mesh resource validates indexed submeshes") {
    std::unique_ptr<ofg::Shader> shader = make_mesh_shader();
    std::unique_ptr<ofg::Material> material = make_mesh_material(*shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, material.get()}};

    ofg::Mesh mesh{ofg::GpuContext{}, "mesh"};
    mesh.init(vertices, indices, submeshes);
    CHECK(mesh.label() == "mesh");
    CHECK(mesh.vertices().size() == 3);
    CHECK(mesh.indices().size() == 3);
    CHECK(mesh.submeshes()[0].m_default_material == material.get());
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
    std::unique_ptr<ofg::Shader> shader = make_mesh_shader();
    std::unique_ptr<ofg::Material> material = make_mesh_material(*shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, material.get()}};
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

    std::vector<ofg::SubMesh> bad_submeshes{ofg::SubMesh{"triangle", 0, 0, material.get()}};
    try {
        mesh.replace_indices(std::vector<std::uint32_t>{0, 1, 2}, bad_submeshes);
        FAIL("Expected invalid replacement submesh to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("Submesh index range") != std::string::npos);
    }
}

// Verifies dynamic vertex meshes update contents without recreating their vertex buffer.
TEST_CASE("dynamic mesh updates vertices in place") {
    std::string gpu_error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(gpu_error);
    REQUIRE_MESSAGE(gpu.has_value(), gpu_error);

    std::unique_ptr<ofg::Shader> shader = make_mesh_shader();
    std::unique_ptr<ofg::Material> material = make_mesh_material(*shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, material.get()}};

    ofg::Mesh mesh{gpu->borrowed_context(), "dynamic mesh"};
    mesh.init_dynamic_vertices(vertices, indices, submeshes);
    REQUIRE(mesh.is_dynamic_vertex_mesh());
    REQUIRE(mesh.vertex_buffer() != nullptr);
    const std::uint64_t create_count = mesh.vertex_buffer_create_count();
    const std::uint64_t upload_bytes = mesh.vertex_upload_bytes();

    vertices[0].m_position = {2.0F, 3.0F, 4.0F};
    mesh.update_vertices_in_place(vertices);
    CHECK(mesh.vertex_buffer_create_count() == create_count);
    CHECK(mesh.vertex_upload_bytes() == upload_bytes + sizeof(ofg::MeshVertex) * vertices.size());
    CHECK(mesh.vertices()[0].m_position[0] == doctest::Approx(2.0F));
    CHECK(mesh.vertices()[0].m_position[1] == doctest::Approx(3.0F));
    CHECK(mesh.vertices()[0].m_position[2] == doctest::Approx(4.0F));

    CHECK_THROWS_WITH_AS(mesh.update_vertices_in_place(std::span<const ofg::MeshVertex>(vertices.data(), 2)),
        doctest::Contains("capacity"),
        ofg::EngineError);
}

// Verifies mesh creation catches each required data category.
TEST_CASE("mesh resource rejects incomplete mesh data") {
    std::unique_ptr<ofg::Shader> shader = make_mesh_shader();
    std::unique_ptr<ofg::Material> material = make_mesh_material(*shader);
    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, material.get()}};

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

    std::vector<ofg::SubMesh> unnamed_submeshes{ofg::SubMesh{"", 0, 3, material.get()}};
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

// Verifies mesh resources are address-stable Object-derived values.
TEST_CASE("mesh resource is not movable") {
    CHECK_FALSE(std::is_move_constructible_v<ofg::Mesh>);
    CHECK_FALSE(std::is_move_assignable_v<ofg::Mesh>);
}
