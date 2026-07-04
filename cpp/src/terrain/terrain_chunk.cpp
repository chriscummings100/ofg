// Addressable procedural terrain chunk data implementation.
#include "ofg/terrain/terrain_chunk.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/terrain/terrain.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ofg {
namespace {

[[nodiscard]] std::size_t heightfield_index(std::int32_t sample_x, std::int32_t sample_z) {
    if (sample_x < 0 || sample_x >= terrain_chunk_lod0_vertices_per_edge || sample_z < 0 ||
        sample_z >= terrain_chunk_lod0_vertices_per_edge) {
        throw EngineError("TerrainChunk heightfield sample coordinates must be inside the 33 by 33 LOD0 grid.");
    }
    return static_cast<std::size_t>(sample_z) * static_cast<std::size_t>(terrain_chunk_lod0_vertices_per_edge) +
           static_cast<std::size_t>(sample_x);
}

[[nodiscard]] bool coordinate_inside_limit(std::int32_t coordinate) noexcept {
    return coordinate >= -terrain_chunk_coordinate_abs_limit && coordinate <= terrain_chunk_coordinate_abs_limit;
}

} // namespace

// Creates a terrain chunk for one validated chunk id.
TerrainChunk::TerrainChunk(TerrainChunkId id) : m_id(id) {
    validate_terrain_chunk_id(m_id);
}

// Returns this chunk's stable terrain address.
TerrainChunkId TerrainChunk::id() const noexcept {
    return m_id;
}

// Returns whether heightfield_samples() contains generated LOD0 surface data.
bool TerrainChunk::has_heightfield() const noexcept {
    return m_heightfield_samples.size() ==
           static_cast<std::size_t>(terrain_chunk_lod0_vertices_per_edge * terrain_chunk_lod0_vertices_per_edge);
}

// Returns the generated row-major X/Z heightfield samples.
std::span<const TerrainSample> TerrainChunk::heightfield_samples() const noexcept {
    return m_heightfield_samples;
}

// Returns one generated heightfield sample by local X/Z sample coordinate.
TerrainSample TerrainChunk::heightfield_sample_at(std::int32_t sample_x, std::int32_t sample_z) const {
    if (!has_heightfield()) {
        throw EngineError("TerrainChunk heightfield has not been generated.");
    }
    return m_heightfield_samples[heightfield_index(sample_x, sample_z)];
}

// Returns the world X coordinate of this chunk's minimum X edge.
float TerrainChunk::world_min_x() const noexcept {
    return static_cast<float>(m_id.m_chunk_x * terrain_chunk_lod0_cells_per_edge);
}

// Returns the world Z coordinate of this chunk's minimum Z edge.
float TerrainChunk::world_min_z() const noexcept {
    return static_cast<float>(m_id.m_chunk_z * terrain_chunk_lod0_cells_per_edge);
}

// Returns the debug texture resource attached to this chunk, or nullptr.
Texture* TerrainChunk::heightfield_debug_texture() noexcept {
    return m_heightfield_debug_texture.get();
}

// Returns the debug-plane mesh resource attached to this chunk, or nullptr.
Mesh* TerrainChunk::debug_plane_mesh() noexcept {
    return m_debug_plane_mesh.get();
}

// Returns the generated heightfield mesh resource attached to this chunk, or nullptr.
Mesh* TerrainChunk::heightfield_mesh() noexcept {
    return m_heightfield_mesh.get();
}

// Attaches the Resources-owned debug texture produced for this chunk.
void TerrainChunk::set_heightfield_debug_texture(Texture* texture) noexcept {
    m_heightfield_debug_texture = texture;
}

// Attaches the Resources-owned debug-plane mesh used for this chunk.
void TerrainChunk::set_debug_plane_mesh(Mesh* mesh) noexcept {
    m_debug_plane_mesh = mesh;
}

// Attaches the Resources-owned generated heightfield mesh for this chunk.
void TerrainChunk::set_heightfield_mesh(Mesh* mesh) noexcept {
    m_heightfield_mesh = mesh;
}

// Regenerates the fixed 33 by 33 LOD0 heightfield from Terrain::sample().
void TerrainChunk::generate_heightfield(const Terrain& terrain) {
    validate_terrain_chunk_id(m_id);
    const std::size_t sample_count =
        static_cast<std::size_t>(terrain_chunk_lod0_vertices_per_edge * terrain_chunk_lod0_vertices_per_edge);
    m_heightfield_samples.assign(sample_count, TerrainSample{});

    const float base_x = world_min_x();
    const float base_z = world_min_z();
    for (std::int32_t sample_z = 0; sample_z < terrain_chunk_lod0_vertices_per_edge; ++sample_z) {
        for (std::int32_t sample_x = 0; sample_x < terrain_chunk_lod0_vertices_per_edge; ++sample_x) {
            const float world_x = base_x + static_cast<float>(sample_x);
            const float world_z = base_z + static_cast<float>(sample_z);
            m_heightfield_samples[heightfield_index(sample_x, sample_z)] = terrain.sample(world_x, world_z);
        }
    }
}

// Validates the first supported terrain chunk address space.
void validate_terrain_chunk_id(TerrainChunkId id) {
    if (id.m_lod != 0) {
        throw EngineError("TerrainChunkId uses unsupported LOD " + std::to_string(id.m_lod) + "; only LOD0 exists.");
    }
    if (id.m_chunk_y != 0) {
        throw EngineError("TerrainChunkId uses unsupported chunk_y " + std::to_string(id.m_chunk_y) +
                          "; only LOD0 surface chunk_y 0 exists.");
    }
    if (!coordinate_inside_limit(id.m_chunk_x) || !coordinate_inside_limit(id.m_chunk_z)) {
        throw EngineError("TerrainChunkId coordinates exceed the supported terrain chunk coordinate limit.");
    }
}

} // namespace ofg
