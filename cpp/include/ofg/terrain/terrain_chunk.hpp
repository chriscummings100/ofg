// Addressable procedural terrain chunk data.
//
// TerrainChunk owns the generated CPU heightfield for one fixed-size LOD0
// terrain chunk. Renderer milestones attach debug textures and mesh resources
// to this same chunk object so terrain data and render resources keep the same
// stable chunk identity.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/texture.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ofg {

class Terrain;

inline constexpr std::int32_t terrain_chunk_lod0_cells_per_edge = 32;
inline constexpr std::int32_t terrain_chunk_lod0_vertices_per_edge = terrain_chunk_lod0_cells_per_edge + 1;
inline constexpr std::int32_t terrain_initial_surface_radius_chunks = 2;
inline constexpr std::int32_t terrain_chunk_coordinate_abs_limit = 1'000'000;

struct TerrainChunkId {
    std::int32_t m_lod{0};
    std::int32_t m_chunk_x{0};
    std::int32_t m_chunk_y{0};
    std::int32_t m_chunk_z{0};

    [[nodiscard]] bool operator==(const TerrainChunkId&) const noexcept = default;
    [[nodiscard]] auto operator<=>(const TerrainChunkId&) const noexcept = default;
};

struct TerrainSample {
    float m_height{0.0f};
};

class TerrainChunk {
public:
    // Creates a terrain chunk for one validated chunk id.
    explicit TerrainChunk(TerrainChunkId id);
    TerrainChunk(const TerrainChunk&) = delete;
    TerrainChunk& operator=(const TerrainChunk&) = delete;
    TerrainChunk(TerrainChunk&&) noexcept = default;
    TerrainChunk& operator=(TerrainChunk&&) noexcept = default;
    ~TerrainChunk() = default;

    // Returns this chunk's stable terrain address.
    [[nodiscard]] TerrainChunkId id() const noexcept;
    // Returns whether heightfield_samples() contains generated LOD0 surface data.
    [[nodiscard]] bool has_heightfield() const noexcept;
    // Returns the generated row-major X/Z heightfield samples.
    [[nodiscard]] std::span<const TerrainSample> heightfield_samples() const noexcept;
    // Returns one generated heightfield sample by local X/Z sample coordinate.
    [[nodiscard]] TerrainSample heightfield_sample_at(std::int32_t sample_x, std::int32_t sample_z) const;
    // Returns the world X coordinate of this chunk's minimum X edge.
    [[nodiscard]] float world_min_x() const noexcept;
    // Returns the world Z coordinate of this chunk's minimum Z edge.
    [[nodiscard]] float world_min_z() const noexcept;
    // Returns the debug texture resource attached to this chunk, or nullptr.
    [[nodiscard]] Texture* heightfield_debug_texture() noexcept;
    // Returns the debug-plane mesh resource attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* debug_plane_mesh() noexcept;
    // Returns the generated heightfield mesh resource attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* heightfield_mesh() noexcept;
    // Attaches the Resources-owned debug texture produced for this chunk.
    void set_heightfield_debug_texture(Texture* texture) noexcept;
    // Attaches the Resources-owned debug-plane mesh used for this chunk.
    void set_debug_plane_mesh(Mesh* mesh) noexcept;
    // Attaches the Resources-owned generated heightfield mesh for this chunk.
    void set_heightfield_mesh(Mesh* mesh) noexcept;

    // Regenerates the fixed 33 by 33 LOD0 heightfield from Terrain::sample().
    void generate_heightfield(const Terrain& terrain);

private:
    TerrainChunkId m_id;
    std::vector<TerrainSample> m_heightfield_samples;
    Ptr<Texture> m_heightfield_debug_texture;
    Ptr<Mesh> m_debug_plane_mesh;
    Ptr<Mesh> m_heightfield_mesh;
};

// Validates the first supported terrain chunk address space.
void validate_terrain_chunk_id(TerrainChunkId id);

} // namespace ofg
