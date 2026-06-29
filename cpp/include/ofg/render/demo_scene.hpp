// Small animated renderer demo scene for smoke tests and early renderer work.
//
// DemoScene owns no resources itself. It stores non-owning pointers into
// Resources-owned assets that outlive the scene, and update_demo_scene writes
// render objects plus camera state into the Game-owned Scene for the current frame.
#pragma once

#include "ofg/resources/shader.hpp"
#include "ofg/scene/scene.hpp"

#include <array>

namespace ofg {

class Material;
class Mesh;
class Shader;
class Texture;

struct DemoScene {
    Shader* m_shader{nullptr};
    Texture* m_checker_texture{nullptr};
    Texture* m_white_texture{nullptr};
    Material* m_ground_material{nullptr};
    std::array<Material*, 4> m_cube_materials{};
    Mesh* m_ground_mesh{nullptr};
    Mesh* m_cube_mesh{nullptr};
};

// Returns the always-textured opaque shader parameter layout used by the demo.
[[nodiscard]] ShaderParameterLayout opaque_demo_shader_layout();

// Creates generated textures, materials, meshes, and shader resources.
void build_demo_scene(DemoScene& scene);

// Rebuilds render objects and camera state for one deterministic animation time.
void update_demo_scene(const DemoScene& demo_scene, double time_ms, float aspect, Scene& scene);

// Returns the stable timestamp used by browser-free native visual smoke.
[[nodiscard]] double demo_native_smoke_time_ms() noexcept;

} // namespace ofg
