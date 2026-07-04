// Scene wiring for terrain render resources.
#include "ofg/terrain/terrain_scene.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/terrain/terrain.hpp"

#include "../render/shaders/terrain_height_debug.wgsl.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr float _terrain_height_debug_min_divisor = 0.0001f;

// Creates a stable human-readable resource label suffix from a chunk id.
[[nodiscard]] std::string chunk_label_suffix(TerrainChunkId id) {
    return " L" + std::to_string(id.m_lod) + " X" + std::to_string(id.m_chunk_x) + " Y" + std::to_string(id.m_chunk_y) +
           " Z" + std::to_string(id.m_chunk_z);
}

// Converts generated height samples into one tightly packed R16Float texel buffer.
[[nodiscard]] std::vector<std::byte> heightfield_debug_pixels(const TerrainChunk& chunk) {
    if (!chunk.has_heightfield()) {
        throw EngineError("Terrain debug texture requires a generated chunk heightfield.");
    }

    const std::span<const TerrainSample> samples = chunk.heightfield_samples();
    std::vector<float> heights;
    heights.reserve(samples.size());
    for (const TerrainSample& sample : samples) {
        if (!std::isfinite(sample.m_height)) {
            throw EngineError("Terrain debug texture requires finite heightfield samples.");
        }
        heights.push_back(sample.m_height);
    }
    return pack_r16_float_pixels(heights);
}

// Creates one material for a chunk-specific R16Float heightfield texture.
[[nodiscard]] Material* create_height_debug_material(
    Shader& shader, Texture& texture, TerrainChunkId id, float height_debug_divisor) {
    PropertyBag properties;
    properties.set("height_debug_options",
        math::vec4(std::max(height_debug_divisor, _terrain_height_debug_min_divisor), 0.0f, 0.0f, 0.0f));
    properties.set("heightfield_texture", &texture);

    Material& material = Resources::create_material("OFG terrain height debug material" + chunk_label_suffix(id));
    material.init(shader, std::move(properties));
    return &material;
}

// Creates or returns the chunk's debug texture resource.
[[nodiscard]] Texture* realize_height_debug_texture(TerrainChunk& chunk) {
    if (Texture* texture = chunk.heightfield_debug_texture()) {
        return texture;
    }

    Texture& texture = Resources::create_texture("OFG terrain height debug texture" + chunk_label_suffix(chunk.id()));
    texture.init_from_r16_float_pixels(static_cast<std::uint32_t>(terrain_chunk_lod0_vertices_per_edge),
        static_cast<std::uint32_t>(terrain_chunk_lod0_vertices_per_edge),
        heightfield_debug_pixels(chunk));
    chunk.set_heightfield_debug_texture(&texture);
    return &texture;
}

// Creates one scene mesh renderer component with explicit validation.
[[nodiscard]] MeshRenderer& create_terrain_mesh_renderer(Entity& entity) {
    Component* component = entity.create_component(ComponentType::MeshRenderer);
    if (component == nullptr || component->type() != ComponentType::MeshRenderer || entity.mesh_renderer() == nullptr) {
        throw EngineError("Terrain scene failed to create a MeshRenderer component.");
    }
    return *entity.mesh_renderer();
}

// Builds the shared 32 by 32 meter XZ debug quad used by every LOD0 surface chunk.
[[nodiscard]] std::vector<MeshVertex> terrain_debug_plane_vertices() {
    const float edge = static_cast<float>(terrain_chunk_lod0_cells_per_edge);
    return {
        MeshVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        MeshVertex{{edge, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        MeshVertex{{edge, 0.0f, edge}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        MeshVertex{{0.0f, 0.0f, edge}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
}

// Creates a zero-height fallback texture for the shared debug-plane default material.
[[nodiscard]] Texture* create_zero_height_texture() {
    const std::vector<float> zero{0.0f};
    Texture& texture = Resources::create_texture("OFG terrain zero height texture");
    texture.init_from_r16_float_pixels(1, 1, pack_r16_float_pixels(zero));
    return &texture;
}

} // namespace

// Returns the explicit shader parameter layout used by the height debug shader.
ShaderParameterLayout terrain_height_debug_shader_layout() {
    return ShaderParameterLayout{{
        ShaderParameter{"view_projection", ShaderParameterType::Mat4, ShaderParameterScope::Frame, 0, true},
        ShaderParameter{"main_light_direction", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 64, false},
        ShaderParameter{"main_light_color", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 80, false},
        ShaderParameter{"ambient_light_color", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 96, false},
        ShaderParameter{"camera_position", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 112, false},
        ShaderParameter{"model", ShaderParameterType::Mat4, ShaderParameterScope::Draw, 0, false},
        ShaderParameter{"normal_model", ShaderParameterType::Mat4, ShaderParameterScope::Draw, 64, false},
        ShaderParameter{"height_debug_options", ShaderParameterType::Vec4, ShaderParameterScope::Material, 0, true},
        ShaderParameter{"heightfield_texture", ShaderParameterType::Texture, ShaderParameterScope::Material, 0, true},
    }};
}

// Creates shared terrain debug resources that do not depend on a particular scene.
void build_terrain_debug_resources(TerrainSceneResources& terrain_scene) {
    terrain_scene.m_height_debug_shader = &Resources::create_shader("OFG terrain height debug shader");
    terrain_scene.m_height_debug_shader->init_from_wgsl(render::shaders::terrain_height_debug_wgsl,
        terrain_height_debug_shader_layout(),
        {PipelineDefinition{"terrain height debug"}});

    terrain_scene.m_zero_height_texture = create_zero_height_texture();
    terrain_scene.m_height_debug_default_material = create_height_debug_material(
        *terrain_scene.m_height_debug_shader, *terrain_scene.m_zero_height_texture, TerrainChunkId{}, 1.0f);

    std::vector<SubMesh> submeshes{
        SubMesh{"terrain height debug", 0, 6, terrain_scene.m_height_debug_default_material}};
    terrain_scene.m_debug_plane_mesh = &Resources::create_mesh("OFG terrain debug plane mesh");
    terrain_scene.m_debug_plane_mesh->init(terrain_debug_plane_vertices(), {0, 1, 2, 0, 2, 3}, std::move(submeshes));
}

// Ticks terrain around the supplied origin and creates one debug plane per chunk.
void setup_terrain_debug_scene(
    TerrainSceneResources& terrain_scene, Scene& scene, Entity& parent, math::Vec3 generation_origin) {
    if (terrain_scene.m_height_debug_shader == nullptr || terrain_scene.m_height_debug_default_material == nullptr ||
        terrain_scene.m_debug_plane_mesh == nullptr) {
        throw EngineError("Terrain debug resources are not initialized.");
    }

    scene.terrain().tick(TerrainTickContext{generation_origin});
    terrain_scene.m_debug_chunks.clear();
    sync_terrain_debug_scene(terrain_scene, scene, parent);
}

// Retargets terrain debug draw slots to the scene's current streamed chunks.
void sync_terrain_debug_scene(TerrainSceneResources& terrain_scene, Scene& scene, Entity& parent) {
    if (terrain_scene.m_height_debug_shader == nullptr || terrain_scene.m_height_debug_default_material == nullptr ||
        terrain_scene.m_debug_plane_mesh == nullptr) {
        throw EngineError("Terrain debug resources are not initialized.");
    }

    std::vector<TerrainChunkId> chunk_ids;
    chunk_ids.reserve(scene.terrain().chunk_count());
    for (const auto& entry : scene.terrain().chunks()) {
        chunk_ids.push_back(entry.first);
    }

    std::vector<TerrainDebugChunkBinding> previous_bindings = std::move(terrain_scene.m_debug_chunks);
    std::vector<bool> previous_used(previous_bindings.size(), false);
    terrain_scene.m_debug_chunks.clear();
    terrain_scene.m_debug_chunks.reserve(chunk_ids.size());

    const float height_debug_divisor = scene.terrain().config().m_height_scale * 2.0f;
    for (TerrainChunkId id : chunk_ids) {
        std::size_t previous_index = previous_bindings.size();
        bool reused_matching_chunk = false;
        for (std::size_t index = 0; index < previous_bindings.size(); ++index) {
            if (!previous_used[index] && previous_bindings[index].m_chunk_id == id) {
                previous_index = index;
                reused_matching_chunk = true;
                break;
            }
        }
        if (previous_index == previous_bindings.size()) {
            for (std::size_t index = 0; index < previous_bindings.size(); ++index) {
                if (!previous_used[index]) {
                    previous_index = index;
                    break;
                }
            }
        }

        TerrainChunk* chunk = scene.terrain().find_chunk(id);
        if (chunk == nullptr) {
            throw EngineError("Terrain debug scene lost a chunk during setup.");
        }
        if (!chunk->has_heightfield()) {
            chunk->generate_heightfield(scene.terrain());
        }

        TerrainDebugChunkBinding binding;
        if (previous_index != previous_bindings.size()) {
            previous_used[previous_index] = true;
            binding = previous_bindings[previous_index];
        } else {
            Entity* entity = scene.create_entity(&parent);
            MeshRenderer& renderer = create_terrain_mesh_renderer(*entity);
            binding.m_entity = entity;
            binding.m_renderer = &renderer;
        }

        const bool chunk_already_had_texture = chunk->heightfield_debug_texture() != nullptr;
        Texture* texture = realize_height_debug_texture(*chunk);
        Material* material = binding.m_material;
        if (!reused_matching_chunk || material == nullptr || !chunk_already_had_texture) {
            material =
                create_height_debug_material(*terrain_scene.m_height_debug_shader, *texture, id, height_debug_divisor);
        }
        chunk->set_debug_plane_mesh(terrain_scene.m_debug_plane_mesh);

        if (binding.m_entity == nullptr || binding.m_renderer == nullptr) {
            throw EngineError("Terrain debug scene has an incomplete reusable binding.");
        }
        binding.m_chunk_id = id;
        binding.m_material = material;
        binding.m_entity->local_transform().m_position = math::vec3(chunk->world_min_x(), 0.0f, chunk->world_min_z());
        binding.m_renderer->set_visible(true);
        binding.m_renderer->set_mesh(terrain_scene.m_debug_plane_mesh);
        binding.m_renderer->set_material_overrides({MaterialOverride{0, material}});
        binding.m_renderer->set_sort_origin_offset(
            math::vec3(static_cast<float>(terrain_chunk_lod0_cells_per_edge) * 0.5f,
                0.0f,
                static_cast<float>(terrain_chunk_lod0_cells_per_edge) * 0.5f));

        terrain_scene.m_debug_chunks.push_back(binding);
    }

    for (std::size_t index = 0; index < previous_bindings.size(); ++index) {
        if (!previous_used[index] && previous_bindings[index].m_renderer != nullptr) {
            previous_bindings[index].m_renderer->set_visible(false);
        }
    }
}

} // namespace ofg
