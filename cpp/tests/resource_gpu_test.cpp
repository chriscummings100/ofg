// Doctest coverage for WebGPU-backed OFG resource state.
//
// These tests use Dawn's null backend to validate resource creation and
// mutation paths without depending on a physical graphics adapter.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char _valid_wgsl_a[] = R"wgsl(
@vertex
fn vs_main() -> @builtin(position) vec4<f32> {
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}
)wgsl";

constexpr char _valid_wgsl_b[] = R"wgsl(
@vertex
fn vs_main() -> @builtin(position) vec4<f32> {
    return vec4<f32>(0.5, 0.0, 0.0, 1.0);
}
)wgsl";

// Builds a byte vector from RGBA8 channel values.
std::vector<std::byte> rgba_bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    for (std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

// Builds a GPU-ready texture used by material tests.
ofg::Texture make_gpu_texture(ofg::GpuContext gpu) {
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(gpu,
        "gpu texture",
        1,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({255, 255, 255, 255}),
        ofg::MipMapPolicy::None,
        error);
    REQUIRE_MESSAGE(texture.has_value(), error);
    return std::move(*texture);
}

// Builds a CPU-only material pointer for mesh submesh validation.
ofg::Material make_cpu_material(ofg::Shader& shader) {
    ofg::PropertyBag properties;
    std::string error;
    std::optional<ofg::Material> material =
        ofg::Material::create(ofg::GpuContext{}, "mesh material", shader, properties, error);
    REQUIRE_MESSAGE(material.has_value(), error);
    return std::move(*material);
}

// Builds a triangle vertex for mesh GPU tests.
ofg::MeshVertex vertex(float x, float y, float z) {
    return ofg::MeshVertex{{x, y, z}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}};
}

} // namespace

// Verifies shader resources create and replace WebGPU shader modules.
TEST_CASE("gpu shader resource creates and replaces modules") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(gpu.borrowed_context(), "gpu shader", _valid_wgsl_a, {}, {}, error);
    REQUIRE_MESSAGE(shader.has_value(), error);
    REQUIRE(shader->module() != nullptr);
    const WGPUShaderModule stable_module = shader->module();
    CHECK(shader->module() == stable_module);

    REQUIRE_MESSAGE(shader->replace_source(_valid_wgsl_b, error), error);
    CHECK(shader->module() != nullptr);
    CHECK(shader->revision() == 2);
}

// Verifies texture resources create GPU texture/view/sampler state and reupload mips on mutation.
TEST_CASE("gpu texture resource uploads full mip chains") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(gpu.borrowed_context(),
        "gpu checker",
        2,
        2,
        ofg::TextureColorSpace::Srgb,
        rgba_bytes({0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255, 255, 0, 0, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain,
        error);
    REQUIRE_MESSAGE(texture.has_value(), error);
    CHECK(texture->texture() != nullptr);
    CHECK(texture->view() != nullptr);
    CHECK(texture->sampler() != nullptr);
    CHECK(texture->mip_level_count() == 2);
    const WGPUTextureView stable_view = texture->view();

    REQUIRE_MESSAGE(
        texture->update_pixels(rgba_bytes({10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 0, 0, 255}), error), error);
    CHECK(texture->revision() == 2);
    CHECK(texture->view() == stable_view);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[0]) == 25);
}

// Verifies materials create uniform buffers and bind groups from shader schemas.
TEST_CASE("gpu material resource creates uniform and texture bind groups") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::Texture texture = make_gpu_texture(gpu.borrowed_context());

    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "base_color_texture", ofg::ShaderParameterType::Texture, ofg::ShaderParameterScope::Material});

    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(gpu.borrowed_context(), "material shader", _valid_wgsl_a, layout, {}, error);
    REQUIRE_MESSAGE(shader.has_value(), error);

    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.5F, 0.25F, 1.0F));
    properties.set("base_color_texture", &texture);
    std::optional<ofg::Material> material =
        ofg::Material::create(gpu.borrowed_context(), "gpu material", *shader, properties, error);
    REQUIRE_MESSAGE(material.has_value(), error);
    CHECK(material->bind_group_layout() != nullptr);
    CHECK(material->uniform_buffer() != nullptr);
    CHECK(material->bind_group() != nullptr);
    const WGPUBindGroup stable_bind_group = material->bind_group();
    CHECK(material->bind_group() == stable_bind_group);

    REQUIRE_MESSAGE(material->set_property("base_color_factor", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F), error), error);
    CHECK(material->revision() == 2);
    CHECK(material->bind_group() != nullptr);

    std::optional<ofg::Texture> cpu_texture = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "cpu texture",
        1,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({255, 255, 255, 255}),
        ofg::MipMapPolicy::None,
        error);
    REQUIRE(cpu_texture.has_value());
    properties.set("base_color_texture", &*cpu_texture);
    CHECK(
        ofg::Material::create(gpu.borrowed_context(), "bad material", *shader, properties, error).has_value() == false);
    CHECK(error.find("GPU-ready texture") != std::string::npos);
}

// Verifies GPU material preparation handles no-uniform and optional-texture schemas.
TEST_CASE("gpu material resource supports empty bind groups and optional textures") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "optional_texture", ofg::ShaderParameterType::Texture, ofg::ShaderParameterScope::Material, 0, false});

    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(gpu.borrowed_context(), "optional texture shader", _valid_wgsl_a, layout, {}, error);
    REQUIRE_MESSAGE(shader.has_value(), error);

    ofg::PropertyBag properties;
    std::optional<ofg::Material> material =
        ofg::Material::create(gpu.borrowed_context(), "empty gpu material", *shader, properties, error);
    REQUIRE_MESSAGE(material.has_value(), error);
    CHECK(material->bind_group_layout() != nullptr);
    CHECK(material->uniform_buffer() == nullptr);
    CHECK(material->bind_group() != nullptr);
}

// Verifies material GPU preparation reports schema and context failures before mutating state.
TEST_CASE("gpu material resource rejects invalid gpu preparation inputs") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    ofg::ShaderParameterLayout overlapping_layout;
    overlapping_layout.m_parameters.push_back(
        ofg::ShaderParameter{"first_color", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material, 4});
    overlapping_layout.m_parameters.push_back(
        ofg::ShaderParameter{"second_color", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material, 8});

    std::string error;
    std::optional<ofg::Shader> overlapping_shader =
        ofg::Shader::create(ofg::GpuContext{}, "overlap shader", "source", overlapping_layout, {}, error);
    REQUIRE(overlapping_shader.has_value());

    ofg::PropertyBag overlapping_properties;
    overlapping_properties.set("first_color", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));
    overlapping_properties.set("second_color", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F));
    CHECK(ofg::Material::create(
              gpu.borrowed_context(), "overlapping gpu material", *overlapping_shader, overlapping_properties, error)
              .has_value() == false);
    CHECK(error.find("overlaps") != std::string::npos);

    std::optional<ofg::Shader> empty_shader =
        ofg::Shader::create(ofg::GpuContext{}, "empty shader", "source", {}, {}, error);
    REQUIRE(empty_shader.has_value());
    ofg::GpuContext incomplete_gpu = gpu.borrowed_context();
    incomplete_gpu.m_queue = nullptr;
    CHECK(ofg::Material::create(incomplete_gpu, "partial gpu material", *empty_shader, {}, error).has_value() == false);
    CHECK(error.find("device and queue") != std::string::npos);
}

// Verifies meshes create and replace GPU vertex/index buffers after validation.
TEST_CASE("gpu mesh resource creates and replaces buffers") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    std::string error;
    std::optional<ofg::Shader> shader = ofg::Shader::create(ofg::GpuContext{}, "mesh shader", "source", {}, {}, error);
    REQUIRE(shader.has_value());
    ofg::Material material = make_cpu_material(*shader);

    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};

    std::optional<ofg::Mesh> mesh =
        ofg::Mesh::create(gpu.borrowed_context(), "gpu mesh", vertices, indices, submeshes, error);
    REQUIRE_MESSAGE(mesh.has_value(), error);
    CHECK(mesh->vertex_buffer() != nullptr);
    CHECK(mesh->index_buffer() != nullptr);
    const WGPUBuffer stable_vertex_buffer = mesh->vertex_buffer();
    CHECK(mesh->vertex_buffer() == stable_vertex_buffer);

    vertices[1] = vertex(2.0F, 0.0F, 0.0F);
    REQUIRE_MESSAGE(mesh->replace_vertices(vertices, error), error);
    CHECK(mesh->revision() == 2);
    CHECK(mesh->vertex_buffer() != nullptr);

    REQUIRE_MESSAGE(mesh->replace_indices(std::vector<std::uint32_t>{2, 1, 0}, submeshes, error), error);
    CHECK(mesh->revision() == 3);
    CHECK(mesh->index_buffer() != nullptr);

    CHECK(mesh->replace_vertices({}, error) == false);
    CHECK(error.find("vertices") != std::string::npos);
    CHECK(mesh->replace_indices({}, submeshes, error) == false);
    CHECK(error.find("indices") != std::string::npos);
    CHECK(mesh->replace_vertices(std::vector<ofg::MeshVertex>{vertex(0.0F, 0.0F, 0.0F)}, error) == false);
    CHECK(mesh->revision() == 3);
}

// Verifies mesh GPU preparation rejects incomplete contexts before creating buffers.
TEST_CASE("gpu mesh resource rejects incomplete gpu contexts") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    std::string error;
    std::optional<ofg::Shader> shader = ofg::Shader::create(ofg::GpuContext{}, "mesh shader", "source", {}, {}, error);
    REQUIRE(shader.has_value());
    ofg::Material material = make_cpu_material(*shader);

    std::vector<ofg::MeshVertex> vertices{vertex(0.0F, 0.0F, 0.0F), vertex(1.0F, 0.0F, 0.0F), vertex(0.0F, 1.0F, 0.0F)};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    ofg::GpuContext incomplete_gpu = gpu.borrowed_context();
    incomplete_gpu.m_queue = nullptr;

    CHECK(ofg::Mesh::create(incomplete_gpu, "partial gpu mesh", vertices, indices, submeshes, error).has_value() ==
          false);
    CHECK(error.find("device and queue") != std::string::npos);
}
