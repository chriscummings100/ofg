// Doctest coverage for stable OFG resource ownership.
#include "doctest.h"

#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resource_arena.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

// Builds a one-pixel texture for arena ownership tests.
ofg::Texture make_arena_texture() {
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "white",
        1,
        1,
        ofg::TextureColorSpace::Linear,
        std::vector<std::byte>{static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255),
            static_cast<std::byte>(255)},
        ofg::MipMapPolicy::None,
        error);
    REQUIRE(texture.has_value());
    return std::move(*texture);
}

// Builds a shader with one material texture property for arena ownership tests.
ofg::Shader make_arena_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "base_color_texture", ofg::ShaderParameterType::Texture, ofg::ShaderParameterScope::Material});
    std::string error;
    std::optional<ofg::Shader> shader = ofg::Shader::create(ofg::GpuContext{}, "shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());
    return std::move(*shader);
}

} // namespace

// Verifies ResourceArena gives resources stable addresses and bulk teardown.
TEST_CASE("resource arena owns stable resource addresses") {
    ofg::ResourceArena arena;
    ofg::Texture& texture = arena.add_texture(make_arena_texture());
    ofg::Shader& shader = arena.add_shader(make_arena_shader());

    ofg::PropertyBag properties;
    properties.set("base_color_texture", &texture);
    std::string error;
    std::optional<ofg::Material> created_material =
        ofg::Material::create(ofg::GpuContext{}, "material", shader, properties, error);
    REQUIRE(created_material.has_value());
    ofg::Material& material = arena.add_material(std::move(*created_material));
    ofg::Texture* texture_address = &texture;
    ofg::Material* material_address = &material;

    std::vector<ofg::MeshVertex> vertices{{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}}};
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &material}};
    std::optional<ofg::Mesh> created_mesh =
        ofg::Mesh::create(ofg::GpuContext{}, "mesh", vertices, indices, submeshes, error);
    REQUIRE(created_mesh.has_value());
    ofg::Mesh& mesh = arena.add_mesh(std::move(*created_mesh));
    ofg::Mesh* mesh_address = &mesh;

    for (int i = 0; i < 16; ++i) {
        arena.add_texture(make_arena_texture());
    }

    CHECK(&texture == texture_address);
    CHECK(&material == material_address);
    CHECK(&mesh == mesh_address);
    CHECK(arena.textures().size() == 17);
    CHECK(arena.materials().size() == 1);
    CHECK(arena.meshes().size() == 1);

    arena.clear();
    CHECK(arena.textures().empty());
    CHECK(arena.shaders().empty());
    CHECK(arena.materials().empty());
    CHECK(arena.meshes().empty());
}
