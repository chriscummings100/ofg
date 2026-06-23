// Small animated renderer demo scene for smoke tests and early renderer work.
//
// DemoScene owns no resources itself. It stores non-owning pointers into a
// ResourceArena that outlives the scene, and update_demo_scene emits a fresh
// draw list plus camera view for the current frame.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/resource_arena.hpp"
#include "ofg/resources/shader.hpp"

#include <array>
#include <string>

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
[[nodiscard]] bool build_demo_scene(GpuContext gpu, ResourceArena& resources, DemoScene& scene, std::string& error);

// Rebuilds draw commands and camera state for one deterministic animation time.
[[nodiscard]] bool update_demo_scene(const DemoScene& scene,
    double time_ms,
    float aspect,
    DrawList& draw_list,
    RenderView& render_view,
    std::string& error);

// Returns the stable timestamp used by browser-free native visual smoke.
[[nodiscard]] double demo_native_smoke_time_ms() noexcept;

} // namespace ofg
