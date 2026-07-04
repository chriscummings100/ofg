// Large deterministic renderer demo scene for smoke tests and renderer work.
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
#include "ofg/terrain/terrain_scene.hpp"

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

constexpr float _pi = 3.14159265358979323846f;
constexpr float _demo_camera_vertical_fov_radians = 55.0f * _pi / 180.0f;
constexpr float _demo_camera_near_z = 0.1f;
constexpr float _demo_camera_far_z = 80.0f;
constexpr float _demo_scene_near_validation_distance = 28.0f;
constexpr float _demo_scene_mid_validation_distance = 50.0f;

struct CubePlacement {
    math::Vec3 m_position;
    math::Vec3 m_scale{1.0f, 1.0f, 1.0f};
    std::uint32_t m_material_index{0};
    float m_phase{0.0f};
    float m_turn_rate{1.0f};
    float m_bob_amplitude{0.0f};
    bool m_overlap_cluster{false};
    bool m_off_camera_candidate{false};
};

math::Vec3 demo_camera_eye() noexcept;

// Builds a byte vector from RGBA8 channels.
std::vector<std::byte> rgba_bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
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

// Returns whether a scaled cube crosses below the ground plane.
bool placement_is_partly_below_ground(const CubePlacement& placement) noexcept {
    return placement.m_position.y - placement.m_scale.y * 0.5f < -0.001f;
}

// Returns camera distance used for broad near/mid/far validation buckets.
float placement_camera_distance(const CubePlacement& placement) noexcept {
    return math::length(math::sub(placement.m_position, demo_camera_eye()));
}

// Builds the deterministic box field used by the default scene.
std::vector<CubePlacement> build_cube_placements() {
    std::vector<CubePlacement> placements;
    placements.reserve(184);

    for (std::uint32_t row = 0; row < 12U; ++row) {
        const float z = -6.0f - static_cast<float>(row) * 5.0f;
        for (std::uint32_t column = 0; column < 12U; ++column) {
            const float row_offset = row % 2U == 0U ? 0.0f : 1.35f;
            const float x = -30.0f + static_cast<float>(column) * 5.5f + row_offset;
            const float sx = 0.75f + static_cast<float>((column * 7U + row * 3U) % 5U) * 0.25f;
            const float sy = 0.55f + static_cast<float>((column * 5U + row * 11U) % 7U) * 0.30f;
            const float sz = 0.70f + static_cast<float>((column * 13U + row * 2U) % 6U) * 0.22f;
            const bool sunk = ((row + column) % 17U == 0U) || (row % 5U == 0U && column % 6U == 1U);
            const float sink = sunk ? sy * (0.18f + static_cast<float>((row + column) % 3U) * 0.06f) : 0.0f;
            const float bob = (row < 2U && column % 4U == 0U) ? 0.08f : 0.0f;

            placements.push_back(CubePlacement{math::vec3(x, sy * 0.5f - sink, z),
                math::vec3(sx, sy, sz),
                (row + column * 2U) % 4U,
                static_cast<float>(row) * 0.43f + static_cast<float>(column) * 0.31f,
                0.20f + static_cast<float>((row + column) % 5U) * 0.10f,
                bob,
                false,
                false});
        }
    }

    const std::array<math::Vec3, 4> cluster_centers{
        math::vec3(-12.0f, 0.0f, -16.0f),
        math::vec3(8.0f, 0.0f, -24.0f),
        math::vec3(22.0f, 0.0f, -39.0f),
        math::vec3(-22.0f, 0.0f, -52.0f),
    };
    const std::array<math::Vec3, 6> cluster_offsets{
        math::vec3(0.0f, 0.0f, 0.0f),
        math::vec3(0.45f, 0.0f, 0.20f),
        math::vec3(-0.35f, 0.0f, -0.30f),
        math::vec3(0.25f, 0.0f, -0.45f),
        math::vec3(-0.55f, 0.0f, 0.35f),
        math::vec3(0.10f, 0.0f, 0.55f),
    };
    for (std::size_t cluster_index = 0; cluster_index < cluster_centers.size(); ++cluster_index) {
        for (std::size_t offset_index = 0; offset_index < cluster_offsets.size(); ++offset_index) {
            const math::Vec3 scale = math::vec3(1.25f + static_cast<float>(offset_index % 3U) * 0.45f,
                0.95f + static_cast<float>((cluster_index + offset_index) % 4U) * 0.55f,
                1.15f + static_cast<float>((cluster_index * 2U + offset_index) % 3U) * 0.40f);
            const float sink = offset_index % 3U == 0U ? scale.y * 0.22f : 0.0f;
            placements.push_back(
                CubePlacement{math::vec3(cluster_centers[cluster_index].x + cluster_offsets[offset_index].x,
                                  scale.y * 0.5f - sink,
                                  cluster_centers[cluster_index].z + cluster_offsets[offset_index].z),
                    scale,
                    static_cast<std::uint32_t>((cluster_index + offset_index) % 4U),
                    static_cast<float>(cluster_index) * 1.3f + static_cast<float>(offset_index) * 0.37f,
                    0.12f + static_cast<float>(offset_index % 4U) * 0.06f,
                    0.0f,
                    true,
                    false});
        }
    }

    for (std::uint32_t index = 0; index < 16U; ++index) {
        const bool left_side = index % 2U == 0U;
        const float x =
            left_side ? -58.0f - static_cast<float>(index % 4U) * 3.5f : 58.0f + static_cast<float>(index % 4U) * 3.5f;
        const float z = -8.0f - static_cast<float>(index / 2U) * 7.0f;
        const math::Vec3 scale = math::vec3(1.2f + static_cast<float>(index % 3U) * 0.45f,
            0.8f + static_cast<float>(index % 5U) * 0.35f,
            1.0f + static_cast<float>((index + 2U) % 4U) * 0.30f);
        const float sink = index % 5U == 0U ? scale.y * 0.25f : 0.0f;
        placements.push_back(CubePlacement{math::vec3(x, scale.y * 0.5f - sink, z),
            scale,
            index % 4U,
            static_cast<float>(index) * 0.41f,
            0.16f + static_cast<float>(index % 4U) * 0.05f,
            0.0f,
            false,
            true});
    }

    return placements;
}

// Returns the deterministic cube placements used by the demo.
const std::vector<CubePlacement>& cube_placements() {
    static const std::vector<CubePlacement> _placements = build_cube_placements();
    return _placements;
}

// Returns the default camera eye used by the browser and native smoke views.
math::Vec3 demo_camera_eye() noexcept {
    return math::vec3(6.2f, 4.4f, 7.6f);
}

// Returns the default camera target used by the browser and native smoke views.
math::Vec3 demo_camera_target() noexcept {
    return math::vec3(0.0f, 1.9f, 0.0f);
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
    if (demo_scene.m_cube_mesh == nullptr || demo_scene.m_player_material == nullptr) {
        throw EngineError("Demo scene resources are not initialized.");
    }
    if (demo_scene.m_terrain_resources.m_height_debug_shader == nullptr ||
        demo_scene.m_terrain_resources.m_height_debug_default_material == nullptr ||
        demo_scene.m_terrain_resources.m_debug_plane_mesh == nullptr) {
        throw EngineError("Demo scene terrain debug resources are not initialized.");
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
    if (demo_scene.m_player_entity == nullptr || demo_scene.m_player_visual_entity == nullptr ||
        demo_scene.m_player_renderer == nullptr || demo_scene.m_player == nullptr) {
        throw EngineError("Demo scene player entity binding is not initialized.");
    }
    if (demo_scene.m_terrain_resources.m_debug_chunks.size() != scene.terrain().chunk_count() ||
        demo_scene.m_terrain_resources.m_debug_chunks.size() <
            static_cast<std::size_t>(terrain_initial_surface_radius_chunks * 2 + 1) *
                static_cast<std::size_t>(terrain_initial_surface_radius_chunks * 2 + 1)) {
        throw EngineError("Demo scene terrain debug bindings are not initialized.");
    }
    for (const TerrainDebugChunkBinding& binding : demo_scene.m_terrain_resources.m_debug_chunks) {
        if (binding.m_entity == nullptr || binding.m_renderer == nullptr || binding.m_material == nullptr) {
            throw EngineError("Demo scene terrain debug bindings are not initialized.");
        }
        if (scene.terrain().find_chunk(binding.m_chunk_id) == nullptr) {
            throw EngineError("Demo scene terrain debug bindings are stale for the current streamed chunks.");
        }
    }
    const std::size_t placement_count = cube_placements().size();
    if (demo_scene.m_cube_entities.size() != placement_count || demo_scene.m_cube_renderers.size() != placement_count) {
        throw EngineError("Demo scene cube entity binding count is not initialized.");
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
    build_terrain_debug_resources(scene.m_terrain_resources);

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
    std::vector<MeshVertex> cube_vertices;
    std::vector<std::uint32_t> cube_indices;
    cube_geometry(cube_vertices, cube_indices);
    std::vector<SubMesh> cube_submeshes{
        SubMesh{"cube", 0, static_cast<std::uint32_t>(cube_indices.size()), scene.m_cube_materials[0]}};
    scene.m_cube_mesh = &Resources::create_mesh("OFG demo cube mesh");
    scene.m_cube_mesh->init(std::move(cube_vertices), std::move(cube_indices), std::move(cube_submeshes));
}

// Creates a stable camera, floor/box entities, and mesh-renderer components.
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
    sun_light->set_color_intensity(math::vec3(1.0f, 0.90f, 0.72f), 3.2f);
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

    setup_terrain_debug_scene(
        demo_scene.m_terrain_resources, scene, *root, demo_scene.m_player_entity->local_transform().m_position);

    const std::vector<CubePlacement>& placements = cube_placements();
    demo_scene.m_cube_entities.assign(placements.size(), nullptr);
    demo_scene.m_cube_renderers.assign(placements.size(), nullptr);
    for (std::size_t index = 0; index < placements.size(); ++index) {
        Entity* entity = scene.create_entity(root);
        MeshRenderer& renderer = create_mesh_renderer(*entity);
        renderer.set_mesh(demo_scene.m_cube_mesh);
        renderer.set_material_overrides(
            {MaterialOverride{0, demo_scene.m_cube_materials[placements[index].m_material_index]}});
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

    demo_scene.m_player_entity->local_transform().m_position.y = demo_scene.m_player->height() * 0.5f;
    demo_scene.m_player_entity->local_transform().m_scale = math::vec3(1.0f, 1.0f, 1.0f);
    demo_scene.m_player_visual_entity->local_transform().m_position = math::vec3(0.0f, 0.0f, 0.0f);
    demo_scene.m_player_visual_entity->local_transform().m_rotation = math::Quat{};
    demo_scene.m_player_visual_entity->local_transform().m_scale = math::vec3(0.6f, 1.8f, 0.35f);

    // The animation updates only entity transforms; resource objects remain stable.
    const float seconds = static_cast<float>(time_ms * 0.001);
    const std::vector<CubePlacement>& placements = cube_placements();
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const float bob = placements[index].m_bob_amplitude * std::sin(seconds * 1.7f + placements[index].m_phase);
        LocalTransform& transform = demo_scene.m_cube_entities[index]->local_transform();
        transform.m_position = math::vec3(
            placements[index].m_position.x, placements[index].m_position.y + bob, placements[index].m_position.z);
        transform.m_rotation = cube_rotation(seconds * placements[index].m_turn_rate + placements[index].m_phase);
        transform.m_scale = placements[index].m_scale;
    }
}

// Returns the deterministic validation-scene distribution used by tests and smoke reports.
DemoSceneValidationStats demo_scene_validation_stats() {
    DemoSceneValidationStats stats;
    const std::vector<CubePlacement>& placements = cube_placements();
    stats.m_box_count = static_cast<std::uint32_t>(placements.size());
    for (const CubePlacement& placement : placements) {
        const float distance = placement_camera_distance(placement);
        if (distance <= _demo_scene_near_validation_distance) {
            stats.m_near_box_count += 1U;
        } else if (distance <= _demo_scene_mid_validation_distance) {
            stats.m_mid_box_count += 1U;
        } else {
            stats.m_far_box_count += 1U;
        }
        if (placement_is_partly_below_ground(placement)) {
            stats.m_partly_below_ground_count += 1U;
        }
        if (placement.m_overlap_cluster) {
            stats.m_overlap_cluster_box_count += 1U;
        }
        if (placement.m_off_camera_candidate) {
            stats.m_off_camera_candidate_count += 1U;
        }
    }
    return stats;
}

// Returns the stable timestamp used by browser-free native visual smoke.
double demo_native_smoke_time_ms() noexcept {
    return 1250.0;
}

} // namespace ofg
