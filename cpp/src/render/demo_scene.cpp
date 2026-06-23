// Small animated renderer demo scene for smoke tests and early renderer work.
#include "ofg/render/demo_scene.hpp"

#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include "shaders/opaque_uber.wgsl.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr std::uint32_t _checker_size = 64;
constexpr float _pi = 3.14159265358979323846F;

struct CubePlacement {
    math::Vec3 m_position;
    float m_scale{1.0F};
    float m_phase{0.0F};
    float m_turn_rate{1.0F};
};

// Builds a byte vector from RGBA8 channels.
std::vector<std::byte> rgba_bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Creates a mipmapped gray checker texture for the ground plane.
std::vector<std::byte> checker_pixels() {
    std::vector<std::byte> pixels;
    pixels.reserve(static_cast<std::size_t>(_checker_size) * _checker_size * 4U);
    for (std::uint32_t y = 0; y < _checker_size; ++y) {
        for (std::uint32_t x = 0; x < _checker_size; ++x) {
            const bool bright = ((x / 8U) + (y / 8U)) % 2U == 0U;
            const std::uint8_t value = bright ? 150U : 74U;
            pixels.push_back(static_cast<std::byte>(value));
            pixels.push_back(static_cast<std::byte>(value));
            pixels.push_back(static_cast<std::byte>(value));
            pixels.push_back(static_cast<std::byte>(255U));
        }
    }
    return pixels;
}

// Creates a generated texture and stores it in the arena.
Texture* add_texture(ResourceArena& resources,
    GpuContext gpu,
    std::string label,
    std::uint32_t width,
    std::uint32_t height,
    TextureColorSpace color_space,
    std::vector<std::byte> pixels,
    std::string& error) {
    std::optional<Texture> texture = Texture::from_rgba8_pixels(gpu,
        std::move(label),
        width,
        height,
        color_space,
        std::move(pixels),
        MipMapPolicy::GenerateCpuFullChain,
        error);
    if (!texture.has_value()) {
        return nullptr;
    }
    return &resources.add_texture(std::move(*texture));
}

// Creates a material that binds a color factor and generated texture.
Material* add_material(ResourceArena& resources,
    GpuContext gpu,
    std::string label,
    Shader& shader,
    math::Vec4 color_factor,
    Texture& texture,
    std::string& error) {
    PropertyBag properties;
    properties.set("base_color_factor", color_factor);
    properties.set("base_color_texture", &texture);

    std::optional<Material> material = Material::create(gpu, std::move(label), shader, std::move(properties), error);
    if (!material.has_value()) {
        return nullptr;
    }
    return &resources.add_material(std::move(*material));
}

// Builds the large XZ ground plane vertex data.
std::vector<MeshVertex> ground_vertices() {
    return {
        MeshVertex{{-8.0F, 0.0F, -8.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}},
        MeshVertex{{8.0F, 0.0F, -8.0F}, {1.0F, 1.0F, 1.0F}, {8.0F, 0.0F}},
        MeshVertex{{8.0F, 0.0F, 8.0F}, {1.0F, 1.0F, 1.0F}, {8.0F, 8.0F}},
        MeshVertex{{-8.0F, 0.0F, 8.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 8.0F}},
    };
}

// Appends one cube face as four vertices plus two triangles.
void append_cube_face(
    std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices, std::array<math::Vec3, 4> positions) {
    const std::uint32_t start = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back(MeshVertex{{positions[0].x, positions[0].y, positions[0].z}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F}});
    vertices.push_back(MeshVertex{{positions[1].x, positions[1].y, positions[1].z}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F}});
    vertices.push_back(MeshVertex{{positions[2].x, positions[2].y, positions[2].z}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}});
    vertices.push_back(MeshVertex{{positions[3].x, positions[3].y, positions[3].z}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F}});
    indices.insert(indices.end(), {start, start + 1U, start + 2U, start, start + 2U, start + 3U});
}

// Builds a unit cube with repeated vertices per face for stable UVs.
void cube_geometry(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices) {
    vertices.clear();
    indices.clear();
    vertices.reserve(24);
    indices.reserve(36);

    const math::Vec3 nnn = math::vec3(-0.5F, -0.5F, -0.5F);
    const math::Vec3 nnp = math::vec3(-0.5F, -0.5F, 0.5F);
    const math::Vec3 npn = math::vec3(-0.5F, 0.5F, -0.5F);
    const math::Vec3 npp = math::vec3(-0.5F, 0.5F, 0.5F);
    const math::Vec3 pnn = math::vec3(0.5F, -0.5F, -0.5F);
    const math::Vec3 pnp = math::vec3(0.5F, -0.5F, 0.5F);
    const math::Vec3 ppn = math::vec3(0.5F, 0.5F, -0.5F);
    const math::Vec3 ppp = math::vec3(0.5F, 0.5F, 0.5F);

    append_cube_face(vertices, indices, {nnp, pnp, ppp, npp});
    append_cube_face(vertices, indices, {pnn, nnn, npn, ppn});
    append_cube_face(vertices, indices, {pnp, pnn, ppn, ppp});
    append_cube_face(vertices, indices, {nnn, nnp, npp, npn});
    append_cube_face(vertices, indices, {npp, ppp, ppn, npn});
    append_cube_face(vertices, indices, {nnn, pnn, pnp, nnp});
}

// Builds a model matrix with column-vector transform order.
math::Mat4 cube_model(const CubePlacement& placement, float seconds) noexcept {
    const float bob = 0.16F * std::sin(seconds * 1.7F + placement.m_phase);
    const math::Mat4 translation =
        math::mat4_translation(math::vec3(placement.m_position.x, 0.5F + bob, placement.m_position.z));
    const math::Mat4 rotation = math::mat4_rotation_y(seconds * placement.m_turn_rate + placement.m_phase);
    const math::Mat4 scale = math::mat4_scale(math::vec3(placement.m_scale, placement.m_scale, placement.m_scale));
    return math::mul(math::mul(translation, rotation), scale);
}

// Adds one draw command to the target draw list.
void add_draw(DrawList& draw_list, Mesh& mesh, math::Mat4 model, math::Vec3 sort_origin) {
    DrawCommand command;
    command.m_mesh = &mesh;
    command.m_model = model;
    command.m_sort_origin = sort_origin;
    draw_list.add(std::move(command));
}

} // namespace

// Returns the always-textured opaque shader parameter layout used by the demo.
ShaderParameterLayout opaque_demo_shader_layout() {
    return ShaderParameterLayout{{
        ShaderParameter{"view_projection", ShaderParameterType::Mat4, ShaderParameterScope::Frame, 0, true},
        ShaderParameter{"model", ShaderParameterType::Mat4, ShaderParameterScope::Draw, 0, false},
        ShaderParameter{"base_color_factor", ShaderParameterType::Vec4, ShaderParameterScope::Material, 0, true},
        ShaderParameter{"base_color_texture", ShaderParameterType::Texture, ShaderParameterScope::Material, 0, true},
    }};
}

// Creates generated textures, materials, meshes, and shader resources.
bool build_demo_scene(GpuContext gpu, ResourceArena& resources, DemoScene& scene, std::string& error) {
    // Shader and textures are created first because every material references them.
    std::optional<Shader> shader = Shader::create(gpu,
        "OFG opaque demo shader",
        render::shaders::opaque_uber_wgsl,
        opaque_demo_shader_layout(),
        {PipelineDefinition{"opaque demo"}},
        error);
    if (!shader.has_value()) {
        return false;
    }
    scene.m_shader = &resources.add_shader(std::move(*shader));

    scene.m_checker_texture = add_texture(resources,
        gpu,
        "OFG generated checker texture",
        _checker_size,
        _checker_size,
        TextureColorSpace::Linear,
        checker_pixels(),
        error);
    if (scene.m_checker_texture == nullptr) {
        return false;
    }
    scene.m_white_texture = add_texture(resources,
        gpu,
        "OFG generated white texture",
        1,
        1,
        TextureColorSpace::Linear,
        rgba_bytes({255, 255, 255, 255}),
        error);
    if (scene.m_white_texture == nullptr) {
        return false;
    }

    // Materials all share one shader layout: a uniform color factor plus texture.
    scene.m_ground_material = add_material(resources,
        gpu,
        "OFG demo ground material",
        *scene.m_shader,
        math::vec4(1.0F, 1.0F, 1.0F, 1.0F),
        *scene.m_checker_texture,
        error);
    if (scene.m_ground_material == nullptr) {
        return false;
    }

    const std::array<math::Vec4, 4> cube_colors{
        math::vec4(0.95F, 0.18F, 0.13F, 1.0F),
        math::vec4(0.12F, 0.78F, 0.32F, 1.0F),
        math::vec4(0.18F, 0.42F, 1.0F, 1.0F),
        math::vec4(0.96F, 0.78F, 0.16F, 1.0F),
    };
    for (std::size_t index = 0; index < scene.m_cube_materials.size(); ++index) {
        scene.m_cube_materials[index] = add_material(resources,
            gpu,
            "OFG demo cube material " + std::to_string(index),
            *scene.m_shader,
            cube_colors[index],
            *scene.m_white_texture,
            error);
        if (scene.m_cube_materials[index] == nullptr) {
            return false;
        }
    }

    // Meshes are added last so their submeshes can point at arena-owned materials.
    std::vector<SubMesh> ground_submeshes{SubMesh{"ground", 0, 6, scene.m_ground_material}};
    std::optional<Mesh> ground_mesh = Mesh::create(
        gpu, "OFG demo ground mesh", ground_vertices(), {0, 1, 2, 0, 2, 3}, std::move(ground_submeshes), error);
    if (!ground_mesh.has_value()) {
        return false;
    }
    scene.m_ground_mesh = &resources.add_mesh(std::move(*ground_mesh));

    std::vector<MeshVertex> cube_vertices;
    std::vector<std::uint32_t> cube_indices;
    cube_geometry(cube_vertices, cube_indices);
    std::vector<SubMesh> cube_submeshes{
        SubMesh{"cube", 0, static_cast<std::uint32_t>(cube_indices.size()), scene.m_cube_materials[0]}};
    std::optional<Mesh> cube_mesh = Mesh::create(
        gpu, "OFG demo cube mesh", std::move(cube_vertices), std::move(cube_indices), std::move(cube_submeshes), error);
    if (!cube_mesh.has_value()) {
        return false;
    }
    scene.m_cube_mesh = &resources.add_mesh(std::move(*cube_mesh));

    error.clear();
    return true;
}

// Rebuilds draw commands and camera state for one deterministic animation time.
bool update_demo_scene(const DemoScene& scene,
    double time_ms,
    float aspect,
    DrawList& draw_list,
    RenderView& render_view,
    std::string& error) {
    if (scene.m_ground_mesh == nullptr || scene.m_cube_mesh == nullptr || scene.m_ground_material == nullptr) {
        error = "Demo scene resources are not initialized.";
        return false;
    }
    for (Material* material : scene.m_cube_materials) {
        if (material == nullptr) {
            error = "Demo scene cube materials are not initialized.";
            return false;
        }
    }
    if (!std::isfinite(time_ms) || !std::isfinite(aspect) || aspect <= 0.0F) {
        error = "Demo scene update requires finite time and positive aspect.";
        return false;
    }

    // Camera state is recomputed from aspect so browser and native paths match after resize.
    std::optional<math::Mat4> view = math::look_at_rh(
        math::vec3(6.2F, 4.4F, 7.6F), math::vec3(0.0F, 0.55F, 0.0F), math::vec3(0.0F, 1.0F, 0.0F), error);
    if (!view.has_value()) {
        return false;
    }
    std::optional<math::Mat4> projection = math::perspective_rh(55.0F * _pi / 180.0F, aspect, 0.1F, 80.0F, error);
    if (!projection.has_value()) {
        return false;
    }

    draw_list.clear();
    render_view = render_view_from_matrix(math::mul(*projection, *view));
    add_draw(draw_list, *scene.m_ground_mesh, math::mat4_identity(), math::vec3(0.0F, 0.0F, 0.0F));

    // The animation updates only draw commands; resource objects remain stable.
    const float seconds = static_cast<float>(time_ms * 0.001);
    const std::array<CubePlacement, 4> placements{{
        CubePlacement{math::vec3(-2.35F, 0.0F, -0.8F), 1.15F, 0.0F, 0.75F},
        CubePlacement{math::vec3(0.25F, 0.0F, -2.45F), 0.9F, 1.7F, -0.95F},
        CubePlacement{math::vec3(2.25F, 0.0F, 0.35F), 1.0F, 3.0F, 0.6F},
        CubePlacement{math::vec3(-0.75F, 0.0F, 2.15F), 0.72F, 4.2F, -1.15F},
    }};
    for (std::size_t index = 0; index < placements.size(); ++index) {
        DrawCommand command;
        command.m_mesh = scene.m_cube_mesh;
        command.m_model = cube_model(placements[index], seconds);
        command.m_sort_origin = placements[index].m_position;
        command.m_material_overrides.push_back(
            MaterialOverride{0, scene.m_cube_materials[index % scene.m_cube_materials.size()]});
        draw_list.add(std::move(command));
    }

    if (!draw_list.validate(error)) {
        return false;
    }
    error.clear();
    return true;
}

// Returns the stable timestamp used by browser-free native visual smoke.
double demo_native_smoke_time_ms() noexcept {
    return 1250.0;
}

} // namespace ofg
