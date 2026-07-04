// Small animated renderer demo scene for smoke tests and early renderer work.
#include "ofg/render/demo_scene.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/render/opaque_pbr_shader.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"

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
constexpr float _pi = 3.14159265358979323846f;
constexpr float _demo_camera_vertical_fov_radians = 55.0f * _pi / 180.0f;
constexpr float _demo_camera_near_z = 0.1f;
constexpr float _demo_camera_far_z = 80.0f;

struct CubePlacement {
    math::Vec3 m_position;
    float m_scale{1.0f};
    float m_phase{0.0f};
    float m_turn_rate{1.0f};
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

// Creates and initializes a generated texture through Resources.
Texture* add_texture(std::string label,
    std::uint32_t width,
    std::uint32_t height,
    TextureColorSpace color_space,
    std::vector<std::byte> pixels) {
    Texture& texture = Resources::create_texture(std::move(label));
    texture.init_from_rgba8_pixels(width, height, color_space, std::move(pixels), MipMapPolicy::GenerateCpuFullChain);
    return &texture;
}

// Creates and initializes a PBR material backed by generated fallback textures.
Material* add_material(std::string label,
    Shader& shader,
    math::Vec4 color_factor,
    Texture& base_color_texture,
    Texture& metallic_roughness_texture,
    Texture& normal_texture) {
    PropertyBag properties;
    properties.set("base_color_factor", color_factor);
    properties.set("pbr_factors", math::vec4(0.0f, 0.92f, 1.0f, 0.0f));
    properties.set("base_color_texture", &base_color_texture);
    properties.set("metallic_roughness_texture", &metallic_roughness_texture);
    properties.set("normal_texture", &normal_texture);

    Material& material = Resources::create_material(std::move(label));
    material.init(shader, std::move(properties));
    return &material;
}

// Builds the large XZ ground plane vertex data.
std::vector<MeshVertex> ground_vertices() {
    return {
        MeshVertex{{-8.0f, 0.0f, -8.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        MeshVertex{{8.0f, 0.0f, -8.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {8.0f, 0.0f}},
        MeshVertex{{8.0f, 0.0f, 8.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {8.0f, 8.0f}},
        MeshVertex{{-8.0f, 0.0f, 8.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 8.0f}},
    };
}

// Appends one cube face as four vertices plus two triangles.
void append_cube_face(
    std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices, std::array<math::Vec3, 4> positions) {
    std::string error;
    const std::optional<math::Vec3> tangent = math::normalize(math::sub(positions[1], positions[0]), error);
    if (!tangent.has_value()) {
        throw EngineError("Demo cube face tangent creation failed.");
    }
    const std::optional<math::Vec3> bitangent = math::normalize(math::sub(positions[3], positions[0]), error);
    if (!bitangent.has_value()) {
        throw EngineError("Demo cube face bitangent creation failed.");
    }
    const std::optional<math::Vec3> normal = math::normalize(math::cross(*tangent, *bitangent), error);
    if (!normal.has_value()) {
        throw EngineError("Demo cube face normal creation failed.");
    }

    const std::uint32_t start = static_cast<std::uint32_t>(vertices.size());
    const std::array<float, 3> normal_array{normal->x, normal->y, normal->z};
    const std::array<float, 4> tangent_array{tangent->x, tangent->y, tangent->z, 1.0f};
    vertices.push_back(
        MeshVertex{{positions[0].x, positions[0].y, positions[0].z}, normal_array, tangent_array, {0.0f, 0.0f}});
    vertices.push_back(
        MeshVertex{{positions[1].x, positions[1].y, positions[1].z}, normal_array, tangent_array, {1.0f, 0.0f}});
    vertices.push_back(
        MeshVertex{{positions[2].x, positions[2].y, positions[2].z}, normal_array, tangent_array, {1.0f, 1.0f}});
    vertices.push_back(
        MeshVertex{{positions[3].x, positions[3].y, positions[3].z}, normal_array, tangent_array, {0.0f, 1.0f}});
    indices.insert(indices.end(), {start, start + 1U, start + 2U, start, start + 2U, start + 3U});
}

// Builds a unit cube with repeated vertices per face for stable UVs.
void cube_geometry(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices) {
    vertices.clear();
    indices.clear();
    vertices.reserve(24);
    indices.reserve(36);

    const math::Vec3 nnn = math::vec3(-0.5f, -0.5f, -0.5f);
    const math::Vec3 nnp = math::vec3(-0.5f, -0.5f, 0.5f);
    const math::Vec3 npn = math::vec3(-0.5f, 0.5f, -0.5f);
    const math::Vec3 npp = math::vec3(-0.5f, 0.5f, 0.5f);
    const math::Vec3 pnn = math::vec3(0.5f, -0.5f, -0.5f);
    const math::Vec3 pnp = math::vec3(0.5f, -0.5f, 0.5f);
    const math::Vec3 ppn = math::vec3(0.5f, 0.5f, -0.5f);
    const math::Vec3 ppp = math::vec3(0.5f, 0.5f, 0.5f);

    append_cube_face(vertices, indices, {nnp, pnp, ppp, npp});
    append_cube_face(vertices, indices, {pnn, nnn, npn, ppn});
    append_cube_face(vertices, indices, {pnp, pnn, ppn, ppp});
    append_cube_face(vertices, indices, {nnn, nnp, npp, npn});
    append_cube_face(vertices, indices, {npp, ppp, ppn, npn});
    append_cube_face(vertices, indices, {nnn, pnn, pnp, nnp});
}

// Returns the deterministic cube placements used by the demo.
std::array<CubePlacement, 4> cube_placements() noexcept {
    return {{
        CubePlacement{math::vec3(-2.35f, 0.0f, -0.8f), 1.15f, 0.0f, 0.75f},
        CubePlacement{math::vec3(0.25f, 0.0f, -2.45f), 0.9f, 1.7f, -0.95f},
        CubePlacement{math::vec3(2.25f, 0.0f, 0.35f), 1.0f, 3.0f, 0.6f},
        CubePlacement{math::vec3(-0.75f, 0.0f, 2.15f), 0.72f, 4.2f, -1.15f},
    }};
}

// Returns the default camera eye used by the browser and native smoke views.
math::Vec3 demo_camera_eye() noexcept {
    return math::vec3(6.2f, 4.4f, 7.6f);
}

// Returns the default camera target used by the browser and native smoke views.
math::Vec3 demo_camera_target() noexcept {
    return math::vec3(0.0f, 0.55f, 0.0f);
}

// Returns the default camera up direction used by the browser and native smoke views.
math::Vec3 demo_camera_up() noexcept {
    return math::vec3(0.0f, 1.0f, 0.0f);
}

// Returns the requested Y-axis rotation or throws a clear scene-update error.
math::Quat cube_rotation(float radians) {
    std::string error;
    std::optional<math::Quat> rotation = math::quat_from_axis_angle(math::vec3(0.0f, 1.0f, 0.0f), radians, error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Demo scene cube rotation creation failed." : error);
    }
    return *rotation;
}

// Creates a camera component on an entity or reports an impossible component mismatch.
Camera& create_camera(Entity& entity) {
    Component* component = entity.create_component(ComponentType::Camera);
    if (component == nullptr || component->type() != ComponentType::Camera || entity.camera() == nullptr) {
        throw EngineError("Demo scene failed to create a Camera component.");
    }
    return *entity.camera();
}

// Creates a mesh renderer on an entity or reports an impossible component mismatch.
MeshRenderer& create_mesh_renderer(Entity& entity) {
    Component* component = entity.create_component(ComponentType::MeshRenderer);
    if (component == nullptr || component->type() != ComponentType::MeshRenderer || entity.mesh_renderer() == nullptr) {
        throw EngineError("Demo scene failed to create a MeshRenderer component.");
    }
    return *entity.mesh_renderer();
}

// Configures the default +Z-forward camera entity for no-input browser and smoke-test views.
void configure_demo_camera(Entity& entity) {
    std::string error;
    std::optional<math::Quat> rotation =
        math::quat_look_at_lh(demo_camera_eye(), demo_camera_target(), demo_camera_up(), error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Demo scene camera rotation creation failed." : error);
    }

    entity.local_transform() = LocalTransform{};
    entity.local_transform().m_position = demo_camera_eye();
    entity.local_transform().m_rotation = *rotation;
    Camera& camera = create_camera(entity);
    camera.set_perspective(_demo_camera_vertical_fov_radians, _demo_camera_near_z, _demo_camera_far_z);
}

// Validates resources that must exist before scene entity setup or update.
void validate_demo_resources(const DemoScene& demo_scene) {
    if (demo_scene.m_ground_mesh == nullptr || demo_scene.m_cube_mesh == nullptr ||
        demo_scene.m_ground_material == nullptr || demo_scene.m_player_material == nullptr) {
        throw EngineError("Demo scene resources are not initialized.");
    }
    for (Material* material : demo_scene.m_cube_materials) {
        if (material == nullptr) {
            throw EngineError("Demo scene cube materials are not initialized.");
        }
    }
}

// Validates cached scene pointers that must exist before per-frame update.
void validate_demo_bindings(const DemoScene& demo_scene, const Scene& scene) {
    if (demo_scene.m_scene != &scene || demo_scene.m_scene_generation != scene.generation()) {
        throw EngineError("Demo scene entity bindings are not initialized for this scene.");
    }
    if (demo_scene.m_ground_entity == nullptr || demo_scene.m_ground_renderer == nullptr) {
        throw EngineError("Demo scene ground entity binding is not initialized.");
    }
    if (demo_scene.m_player_entity == nullptr || demo_scene.m_player_visual_entity == nullptr ||
        demo_scene.m_player_renderer == nullptr || demo_scene.m_player == nullptr) {
        throw EngineError("Demo scene player entity binding is not initialized.");
    }
    for (std::size_t index = 0; index < demo_scene.m_cube_entities.size(); ++index) {
        if (demo_scene.m_cube_entities[index] == nullptr || demo_scene.m_cube_renderers[index] == nullptr) {
            throw EngineError("Demo scene cube entity bindings are not initialized.");
        }
    }
}

} // namespace

// Returns the always-textured opaque shader parameter layout used by the demo.
ShaderParameterLayout opaque_demo_shader_layout() {
    return opaque_pbr_shader_layout();
}

// Creates generated textures, materials, meshes, and shader resources.
void build_demo_scene(DemoScene& scene) {
    // Shader and textures are created first because every material references them.
    scene.m_shader = &Resources::create_shader("OFG opaque demo shader");
    scene.m_shader->init_from_wgsl(
        render::shaders::opaque_uber_wgsl, opaque_demo_shader_layout(), {PipelineDefinition{"opaque demo"}});

    scene.m_checker_texture = add_texture(
        "OFG generated checker texture", _checker_size, _checker_size, TextureColorSpace::Srgb, checker_pixels());
    scene.m_white_texture =
        add_texture("OFG generated white texture", 1, 1, TextureColorSpace::Srgb, rgba_bytes({255, 255, 255, 255}));
    scene.m_neutral_metallic_roughness_texture = add_texture("OFG generated neutral metallic-roughness texture",
        1,
        1,
        TextureColorSpace::Linear,
        rgba_bytes({255, 255, 0, 255}));
    scene.m_flat_normal_texture = add_texture(
        "OFG generated flat normal texture", 1, 1, TextureColorSpace::Linear, rgba_bytes({128, 128, 255, 255}));

    // Materials all share one shader layout: PBR factors plus the required texture slots.
    scene.m_ground_material = add_material("OFG demo ground material",
        *scene.m_shader,
        math::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        *scene.m_checker_texture,
        *scene.m_neutral_metallic_roughness_texture,
        *scene.m_flat_normal_texture);
    scene.m_player_material = add_material("OFG demo player material",
        *scene.m_shader,
        math::vec4(0.15f, 0.86f, 0.92f, 1.0f),
        *scene.m_white_texture,
        *scene.m_neutral_metallic_roughness_texture,
        *scene.m_flat_normal_texture);

    const std::array<math::Vec4, 4> cube_colors{
        math::vec4(0.95f, 0.18f, 0.13f, 1.0f),
        math::vec4(0.12f, 0.78f, 0.32f, 1.0f),
        math::vec4(0.18f, 0.42f, 1.0f, 1.0f),
        math::vec4(0.96f, 0.78f, 0.16f, 1.0f),
    };
    for (std::size_t index = 0; index < scene.m_cube_materials.size(); ++index) {
        scene.m_cube_materials[index] = add_material("OFG demo cube material " + std::to_string(index),
            *scene.m_shader,
            cube_colors[index],
            *scene.m_white_texture,
            *scene.m_neutral_metallic_roughness_texture,
            *scene.m_flat_normal_texture);
    }

    // Meshes are added last so their submeshes can point at arena-owned materials.
    std::vector<SubMesh> ground_submeshes{SubMesh{"ground", 0, 6, scene.m_ground_material}};
    scene.m_ground_mesh = &Resources::create_mesh("OFG demo ground mesh");
    scene.m_ground_mesh->init(ground_vertices(), {0, 1, 2, 0, 2, 3}, std::move(ground_submeshes));

    std::vector<MeshVertex> cube_vertices;
    std::vector<std::uint32_t> cube_indices;
    cube_geometry(cube_vertices, cube_indices);
    std::vector<SubMesh> cube_submeshes{
        SubMesh{"cube", 0, static_cast<std::uint32_t>(cube_indices.size()), scene.m_cube_materials[0]}};
    scene.m_cube_mesh = &Resources::create_mesh("OFG demo cube mesh");
    scene.m_cube_mesh->init(std::move(cube_vertices), std::move(cube_indices), std::move(cube_submeshes));
}

// Creates a stable camera, floor/cube entities, and mesh-renderer components.
void setup_demo_scene(DemoScene& demo_scene, Scene& scene) {
    validate_demo_resources(demo_scene);

    scene.clear();
    demo_scene.m_scene = &scene;
    demo_scene.m_scene_generation = scene.generation();

    Entity* root = scene.get_root();
    Entity* sun_entity = scene.create_entity(root);
    Component* sun_component = sun_entity->create_component(ComponentType::Light);
    if (sun_component == nullptr || sun_component->type() != ComponentType::Light || sun_entity->light() == nullptr) {
        throw EngineError("Demo scene failed to create a directional sun Light component.");
    }
    Light* sun_light = sun_entity->light();
    sun_light->set_color_intensity(math::vec3(1.0f, 0.96f, 0.88f), 3.2f);
    scene.environment().set_main_directional_light(sun_light);
    scene.environment().set_ambient_light(AmbientLight{math::vec3(0.46f, 0.52f, 0.62f), 0.22f});

    std::string light_error;
    const std::optional<math::Vec3> initial_sun_direction =
        math::normalize(math::vec3(-0.35f, -1.0f, -0.25f), light_error);
    if (!initial_sun_direction.has_value()) {
        throw EngineError(light_error.empty() ? "Demo sun direction could not be normalized." : light_error);
    }
    const std::optional<math::Quat> initial_sun_rotation =
        math::quat_look_at_lh(sun_entity->local_transform().m_position,
            math::add(sun_entity->local_transform().m_position, *initial_sun_direction),
            math::vec3(0.0f, 1.0f, 0.0f),
            light_error);
    if (!initial_sun_rotation.has_value()) {
        throw EngineError(light_error.empty() ? "Demo sun rotation could not be built." : light_error);
    }
    sun_entity->local_transform().m_rotation = *initial_sun_rotation;

    Entity* camera_entity = scene.create_entity(root);
    configure_demo_camera(*camera_entity);

    demo_scene.m_ground_entity = scene.create_entity(root);
    demo_scene.m_ground_renderer = &create_mesh_renderer(*demo_scene.m_ground_entity);
    demo_scene.m_ground_renderer->set_mesh(demo_scene.m_ground_mesh);

    demo_scene.m_player_entity = scene.create_entity(root);
    Component* player_component = demo_scene.m_player_entity->create_component(ComponentType::Player);
    if (player_component == nullptr || player_component->type() != ComponentType::Player ||
        demo_scene.m_player_entity->player() == nullptr) {
        throw EngineError("Demo scene failed to create a Player component.");
    }
    demo_scene.m_player = demo_scene.m_player_entity->player();
    demo_scene.m_player_visual_entity = scene.create_entity(demo_scene.m_player_entity);
    demo_scene.m_player_renderer = &create_mesh_renderer(*demo_scene.m_player_visual_entity);
    demo_scene.m_player_renderer->set_mesh(demo_scene.m_cube_mesh);
    demo_scene.m_player_renderer->set_material_overrides({MaterialOverride{0, demo_scene.m_player_material}});
    demo_scene.m_player->bind_fallback_renderer(*demo_scene.m_player_renderer);
    demo_scene.m_player_entity->local_transform().m_position = math::vec3(0.0f, 0.9f, 0.0f);
    demo_scene.m_player_entity->local_transform().m_scale = math::vec3(1.0f, 1.0f, 1.0f);
    demo_scene.m_player_visual_entity->local_transform().m_scale = math::vec3(0.6f, 1.8f, 0.35f);

    const std::array<CubePlacement, 4> placements = cube_placements();
    for (std::size_t index = 0; index < placements.size(); ++index) {
        Entity* entity = scene.create_entity(root);
        MeshRenderer& renderer = create_mesh_renderer(*entity);
        renderer.set_mesh(demo_scene.m_cube_mesh);
        renderer.set_material_overrides({MaterialOverride{0, demo_scene.m_cube_materials[index]}});
        demo_scene.m_cube_entities[index] = entity;
        demo_scene.m_cube_renderers[index] = &renderer;
    }
}

// Mutates entity transforms for one deterministic animation time.
void update_demo_scene(const DemoScene& demo_scene, double time_ms, Scene& scene) {
    validate_demo_resources(demo_scene);
    validate_demo_bindings(demo_scene, scene);
    if (!std::isfinite(time_ms)) {
        throw EngineError("Demo scene update requires finite time.");
    }

    demo_scene.m_ground_entity->local_transform() = LocalTransform{};
    demo_scene.m_player_entity->local_transform().m_position.y = demo_scene.m_player->height() * 0.5f;
    demo_scene.m_player_entity->local_transform().m_scale = math::vec3(1.0f, 1.0f, 1.0f);
    demo_scene.m_player_visual_entity->local_transform().m_position = math::vec3(0.0f, 0.0f, 0.0f);
    demo_scene.m_player_visual_entity->local_transform().m_rotation = math::Quat{};
    demo_scene.m_player_visual_entity->local_transform().m_scale = math::vec3(0.6f, 1.8f, 0.35f);

    // The animation updates only entity transforms; resource objects remain stable.
    const float seconds = static_cast<float>(time_ms * 0.001);
    const std::array<CubePlacement, 4> placements = cube_placements();
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const float bob = 0.16f * std::sin(seconds * 1.7f + placements[index].m_phase);
        LocalTransform& transform = demo_scene.m_cube_entities[index]->local_transform();
        transform.m_position = math::vec3(placements[index].m_position.x, 0.5f + bob, placements[index].m_position.z);
        transform.m_rotation = cube_rotation(seconds * placements[index].m_turn_rate + placements[index].m_phase);
        transform.m_scale = math::vec3(placements[index].m_scale, placements[index].m_scale, placements[index].m_scale);
    }
}

// Returns the stable timestamp used by browser-free native visual smoke.
double demo_native_smoke_time_ms() noexcept {
    return 1250.0;
}

} // namespace ofg
