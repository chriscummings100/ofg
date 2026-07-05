// Addressable procedural terrain chunk data.
//
// TerrainChunk owns the generated CPU heightfield and chunk-local render data
// for one fixed-size LOD0 terrain chunk. Optional debug resources live on the
// chunk too, so terrain rendering can stay a simple per-chunk data extraction.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/render/draw_list.hpp"
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
    // Returns the generated terrain render mesh attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* render_mesh() noexcept;
    // Returns the generated terrain render mesh attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* render_mesh() const noexcept;
    // Returns the debug-plane mesh resource attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* debug_plane_mesh() noexcept;
    // Returns the debug-plane mesh resource attached to this chunk, or nullptr.
    [[nodiscard]] Mesh* debug_plane_mesh() const noexcept;
    // Returns the debug-plane texture resource attached to this chunk, or nullptr.
    [[nodiscard]] Texture* debug_plane_texture() noexcept;
    // Returns the debug-plane texture resource attached to this chunk, or nullptr.
    [[nodiscard]] Texture* debug_plane_texture() const noexcept;
    // Returns the material override that binds this chunk's debug texture.
    [[nodiscard]] std::span<const MaterialOverride> debug_plane_material_overrides() const noexcept;
    // Attaches the Resources-owned terrain render mesh produced for this chunk.
    void set_render_mesh(Mesh* mesh) noexcept;
    // Attaches the Resources-owned debug-plane mesh used for this chunk.
    void set_debug_plane_mesh(Mesh* mesh) noexcept;
    // Attaches the Resources-owned debug-plane texture produced for this chunk.
    void set_debug_plane_texture(Texture* texture) noexcept;

    // Regenerates the fixed 33 by 33 LOD0 heightfield from Terrain::sample().
    void generate_heightfield(const Terrain& terrain);
    // Creates chunk-owned debug plane mesh, texture, and texture material override.
    void generate_debug_plane(const Terrain& terrain);

private:
    TerrainChunkId m_id;
    std::vector<TerrainSample> m_heightfield_samples;
    Ptr<Mesh> m_render_mesh;
    Ptr<Mesh> m_debug_plane_mesh;
    Ptr<Texture> m_debug_plane_texture;
    std::vector<MaterialOverride> m_debug_plane_material_overrides;
};

// Validates the first supported terrain chunk address space.
void validate_terrain_chunk_id(TerrainChunkId id);

} // namespace ofg
