// Scene-owned procedural terrain model implementation.
#include "ofg/terrain/terrain.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace ofg {
namespace {

constexpr float _terrain_pi = 3.14159265358979323846f;
constexpr float _terrain_tau = _terrain_pi * 2.0f;

// Mixes the seed into well-distributed phase bits for deterministic sine waves.
[[nodiscard]] std::uint64_t splitmix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] float phase_from_seed(std::uint64_t seed, std::uint64_t stream) noexcept {
    const std::uint64_t bits = splitmix64(seed + stream * 0x632be59bd9b4e019ULL);
    const float unit = static_cast<float>(bits & 0x00ffffffULL) / static_cast<float>(0x01000000ULL);
    return unit * _terrain_tau;
}

[[nodiscard]] std::int32_t floor_divide_world_to_lod0_chunk(float coordinate) {
    if (!std::isfinite(coordinate)) {
        throw EngineError("Terrain world coordinates must be finite.");
    }
    const float chunk = std::floor(coordinate / static_cast<float>(terrain_chunk_lod0_cells_per_edge));
    if (chunk < static_cast<float>(-terrain_chunk_coordinate_abs_limit) ||
        chunk > static_cast<float>(terrain_chunk_coordinate_abs_limit)) {
        throw EngineError("Terrain world coordinate maps outside the supported chunk coordinate limit.");
    }
    return static_cast<std::int32_t>(chunk);
}

[[nodiscard]] bool same_config(const TerrainConfig& a, const TerrainConfig& b) noexcept {
    return a.m_seed == b.m_seed && a.m_height_scale == b.m_height_scale;
}

} // namespace

// Returns the current deterministic terrain generation inputs.
const TerrainConfig& Terrain::config() const noexcept {
    return m_config;
}

// Replaces generation inputs and clears generated chunks when they change.
void Terrain::set_config(TerrainConfig config) {
    validate_terrain_config(config);
    if (!same_config(m_config, config)) {
        m_config = config;
        clear_chunks();
    }
}

// Stores the shared clay terrain material used by normal terrain rendering.
void Terrain::set_material(Material* material) noexcept {
    m_material = material;
}

// Returns the shared clay terrain material, or nullptr.
Material* Terrain::material() noexcept {
    return m_material.get();
}

// Returns the shared clay terrain material, or nullptr.
Material* Terrain::material() const noexcept {
    return m_material.get();
}

// Stores the shared height-debug plane material used by debug rendering.
void Terrain::set_debug_plane_material(Material* material) {
    m_debug_plane_material = material;
    if (m_debug_plane_material != nullptr && m_render_mode == TerrainRenderMode::HeightDebugPlane) {
        for (auto& entry : m_chunks) {
            entry.second.generate(*this);
        }
    }
}

// Returns the shared height-debug plane material, or nullptr.
Material* Terrain::debug_plane_material() noexcept {
    return m_debug_plane_material.get();
}

// Returns the shared height-debug plane material, or nullptr.
Material* Terrain::debug_plane_material() const noexcept {
    return m_debug_plane_material.get();
}

// Selects which chunk-owned render data terrain extraction should expose.
void Terrain::set_render_mode(TerrainRenderMode mode) noexcept {
    m_render_mode = mode;
}

// Returns the selected terrain render mode.
TerrainRenderMode Terrain::render_mode() const noexcept {
    return m_render_mode;
}

// Reconciles the fixed 5 by 5 LOD0 surface region around the origin chunk.
void Terrain::tick(const TerrainTickContext& context) {
    validate_terrain_config(m_config);
    if (!std::isfinite(context.m_generation_origin.x) || !std::isfinite(context.m_generation_origin.y) ||
        !std::isfinite(context.m_generation_origin.z)) {
        throw EngineError("Terrain::tick requires a finite generation origin.");
    }

    const TerrainChunkId origin_id = chunk_id_containing(context.m_generation_origin.x, context.m_generation_origin.z);
    std::set<TerrainChunkId> desired_ids;
    for (std::int32_t dz = -terrain_initial_surface_radius_chunks; dz <= terrain_initial_surface_radius_chunks; ++dz) {
        for (std::int32_t dx = -terrain_initial_surface_radius_chunks; dx <= terrain_initial_surface_radius_chunks;
            ++dx) {
            const TerrainChunkId id{0, origin_id.m_chunk_x + dx, 0, origin_id.m_chunk_z + dz};
            validate_terrain_chunk_id(id);
            desired_ids.insert(id);
        }
    }

    for (auto chunk = m_chunks.begin(); chunk != m_chunks.end();) {
        if (desired_ids.find(chunk->first) == desired_ids.end()) {
            chunk = m_chunks.erase(chunk);
        } else {
            ++chunk;
        }
    }

    for (TerrainChunkId id : desired_ids) {
        TerrainChunk* chunk = get_or_create_chunk(id);
        if (chunk != nullptr) {
            chunk->generate(*this);
        }
    }
}

// Samples the deterministic heightfield at one world X/Z coordinate.
TerrainSample Terrain::sample(float world_x, float world_z) const {
    if (!std::isfinite(world_x) || !std::isfinite(world_z)) {
        throw EngineError("Terrain::sample requires finite world coordinates.");
    }

    float height = 0.0f;
    float amplitude = 1.0f;
    float frequency = 0.035f;
    for (std::uint64_t octave = 0; octave < 4; ++octave) {
        const float phase_x = phase_from_seed(m_config.m_seed, octave * 3U + 0U);
        const float phase_z = phase_from_seed(m_config.m_seed, octave * 3U + 1U);
        const float phase_diagonal = phase_from_seed(m_config.m_seed, octave * 3U + 2U);
        const float x_wave = std::sin(world_x * frequency + phase_x);
        const float z_wave = std::cos(world_z * frequency * 1.31f + phase_z);
        const float diagonal_wave = std::sin((world_x + world_z) * frequency * 0.47f + phase_diagonal);
        height += amplitude * ((x_wave + z_wave) * 0.35f + diagonal_wave * 0.30f);
        amplitude *= 0.5f;
        frequency *= 2.07f;
    }

    return TerrainSample{height * m_config.m_height_scale};
}

// Returns the LOD0 surface chunk containing one world X/Z coordinate.
TerrainChunkId Terrain::chunk_id_containing(float world_x, float world_z) const {
    return TerrainChunkId{0, floor_divide_world_to_lod0_chunk(world_x), 0, floor_divide_world_to_lod0_chunk(world_z)};
}

// Finds one existing chunk, or nullptr.
TerrainChunk* Terrain::find_chunk(TerrainChunkId id) noexcept {
    const auto found = m_chunks.find(id);
    return found == m_chunks.end() ? nullptr : &found->second;
}

// Finds one existing chunk, or nullptr.
const TerrainChunk* Terrain::find_chunk(TerrainChunkId id) const noexcept {
    const auto found = m_chunks.find(id);
    return found == m_chunks.end() ? nullptr : &found->second;
}

// Finds or creates one validated chunk and returns a stable pointer to it.
TerrainChunk* Terrain::get_or_create_chunk(TerrainChunkId id) {
    validate_terrain_chunk_id(id);
    auto found = m_chunks.find(id);
    if (found != m_chunks.end()) {
        return &found->second;
    }

    auto inserted = m_chunks.emplace(id, TerrainChunk{id});
    return &inserted.first->second;
}

// Reports the number of currently streamed chunks.
std::size_t Terrain::chunk_count() const noexcept {
    return m_chunks.size();
}

// Returns the current chunk map for render/debug iteration.
const std::map<TerrainChunkId, TerrainChunk>& Terrain::chunks() const noexcept {
    return m_chunks;
}

// Appends render objects for currently generated, renderable terrain chunks.
void Terrain::extract_render_objects(std::vector<RenderObject>& output) const {
    for (const auto& entry : m_chunks) {
        const TerrainChunk& chunk = entry.second;

        Mesh* mesh = nullptr;
        std::span<const MaterialOverride> material_overrides;
        switch (m_render_mode) {
        case TerrainRenderMode::ClayMesh:
            mesh = chunk.render_mesh();
            break;
        case TerrainRenderMode::HeightDebugPlane:
            mesh = chunk.debug_plane_mesh();
            material_overrides = chunk.debug_plane_material_overrides();
            break;
        }

        if (mesh == nullptr) {
            continue;
        }

        const math::Mat4 world_from_chunk =
            math::mat4_translation(math::vec3(chunk.world_min_x(), 0.0f, chunk.world_min_z()));
        const Bounds3 local_bounds = mesh->local_bounds();
        RenderObject object;
        object.m_mesh = mesh;
        object.m_model = world_from_chunk;
        object.m_material_overrides = material_overrides;
        object.m_sort_origin = math::transform_point(world_from_chunk,
            math::vec3(static_cast<float>(terrain_chunk_lod0_cells_per_edge) * 0.5f,
                0.0f,
                static_cast<float>(terrain_chunk_lod0_cells_per_edge) * 0.5f));
        object.m_local_bounds = local_bounds;
        object.m_world_bounds = transform_bounds(local_bounds, world_from_chunk);
        output.push_back(object);
    }
}

// Clears all streamed chunks without changing the generator config.
void Terrain::clear_chunks() noexcept {
    m_chunks.clear();
}

// Validates user-tunable terrain generation inputs.
void validate_terrain_config(const TerrainConfig& config) {
    if (!std::isfinite(config.m_height_scale) || config.m_height_scale <= 0.0f) {
        throw EngineError("TerrainConfig height_scale must be a positive finite value.");
    }
}

} // namespace ofg
