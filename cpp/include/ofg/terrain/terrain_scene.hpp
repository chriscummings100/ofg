// Scene wiring for terrain render resources.
//
// Terrain generation stays owned by Scene::terrain() and TerrainChunk. This file
// contains the narrow bridge that realizes debug textures/materials/meshes
// through Resources and retargets a fixed set of debug draw slots to the current
// streamed chunks.
#pragma once

#include "ofg/math/vec.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/terrain/terrain_chunk.hpp"

#include <vector>

namespace ofg {

class Entity;
class Material;
class Mesh;
class MeshRenderer;
class Scene;
class Shader;
class Texture;

struct TerrainDebugChunkBinding {
    TerrainChunkId m_chunk_id{};
    Entity* m_entity{nullptr};
    MeshRenderer* m_renderer{nullptr};
    Material* m_material{nullptr};
};

struct TerrainSceneResources {
    Shader* m_height_debug_shader{nullptr};
    Texture* m_zero_height_texture{nullptr};
    Material* m_height_debug_default_material{nullptr};
    Mesh* m_debug_plane_mesh{nullptr};
    std::vector<TerrainDebugChunkBinding> m_debug_chunks;
};

// Returns the explicit shader parameter layout used by the height debug shader.
[[nodiscard]] ShaderParameterLayout terrain_height_debug_shader_layout();

// Creates shared terrain debug resources that do not depend on a particular scene.
void build_terrain_debug_resources(TerrainSceneResources& terrain_scene);

// Ticks terrain around the supplied origin and creates one debug plane per chunk.
void setup_terrain_debug_scene(
    TerrainSceneResources& terrain_scene, Scene& scene, Entity& parent, math::Vec3 generation_origin);

// Retargets terrain debug draw slots to the scene's current streamed chunks.
void sync_terrain_debug_scene(TerrainSceneResources& terrain_scene, Scene& scene, Entity& parent);

} // namespace ofg
