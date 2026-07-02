// Small animated renderer demo scene for smoke tests and early renderer work.
//
// DemoScene owns no resources itself. It stores non-owning pointers into
// Resources-owned assets plus cached entity/component bindings for the active
// Game-owned Scene. update_demo_scene mutates animated local transforms.
#pragma once

#include "ofg/resources/shader.hpp"
#include "ofg/scene/scene.hpp"

#include <array>
#include <cstdint>

namespace ofg {

class Material;
class Mesh;
class Player;
class Shader;
class Texture;

struct DemoScene {
    Shader* m_shader{nullptr};
    Texture* m_checker_texture{nullptr};
    Texture* m_white_texture{nullptr};
    Texture* m_neutral_metallic_roughness_texture{nullptr};
    Texture* m_flat_normal_texture{nullptr};
    Material* m_ground_material{nullptr};
    Material* m_player_material{nullptr};
    std::array<Material*, 4> m_cube_materials{};
    Mesh* m_ground_mesh{nullptr};
    Mesh* m_cube_mesh{nullptr};
    Scene* m_scene{nullptr};
    std::uint32_t m_scene_generation{0};
    Entity* m_ground_entity{nullptr};
    MeshRenderer* m_ground_renderer{nullptr};
    Entity* m_player_entity{nullptr};
    Entity* m_player_visual_entity{nullptr};
    MeshRenderer* m_player_renderer{nullptr};
    Player* m_player{nullptr};
    std::array<Entity*, 4> m_cube_entities{};
    std::array<MeshRenderer*, 4> m_cube_renderers{};
};

// Returns the always-textured opaque shader parameter layout used by the demo.
[[nodiscard]] ShaderParameterLayout opaque_demo_shader_layout();

// Creates generated textures, materials, meshes, and shader resources.
void build_demo_scene(DemoScene& scene);

// Creates a stable camera, floor/cube entities, and mesh-renderer components.
void setup_demo_scene(DemoScene& demo_scene, Scene& scene);

// Mutates entity transforms for one deterministic animation time.
void update_demo_scene(const DemoScene& demo_scene, double time_ms, Scene& scene);

// Returns the stable timestamp used by browser-free native visual smoke.
[[nodiscard]] double demo_native_smoke_time_ms() noexcept;

} // namespace ofg
