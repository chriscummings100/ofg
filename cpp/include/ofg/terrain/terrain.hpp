// Scene-owned procedural terrain model.
//
// Terrain owns deterministic terrain configuration, world-coordinate sampling,
// and the streamed map of addressable TerrainChunk objects. It is intentionally
// CPU/data oriented so tests can prove chunk identity and height continuity
// before the renderer attaches richer textures and meshes in later milestones.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/terrain/terrain_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <map>

namespace ofg {

class Material;

struct TerrainConfig {
    std::uint64_t m_seed{1};
    float m_height_scale{8.0f};
};

struct TerrainTickContext {
    math::Vec3 m_generation_origin{0.0f, 0.0f, 0.0f};
};

enum class TerrainRenderMode {
    ClayMesh,
    HeightDebugPlane,
};

class Terrain {
public:
    // Returns the current deterministic terrain generation inputs.
    [[nodiscard]] const TerrainConfig& config() const noexcept;
    // Replaces generation inputs and clears generated chunks when they change.
    void set_config(TerrainConfig config);
    // Stores the shared clay terrain material used by normal terrain rendering.
    void set_material(Material* material) noexcept;
    // Returns the shared clay terrain material, or nullptr.
    [[nodiscard]] Material* material() noexcept;
    // Returns the shared clay terrain material, or nullptr.
    [[nodiscard]] const Material* material() const noexcept;
    // Stores the shared height-debug plane material used by debug rendering.
    void set_debug_plane_material(Material* material) noexcept;
    // Returns the shared height-debug plane material, or nullptr.
    [[nodiscard]] Material* debug_plane_material() noexcept;
    // Returns the shared height-debug plane material, or nullptr.
    [[nodiscard]] const Material* debug_plane_material() const noexcept;
    // Selects which chunk-owned render data terrain extraction should expose.
    void set_render_mode(TerrainRenderMode mode) noexcept;
    // Returns the selected terrain render mode.
    [[nodiscard]] TerrainRenderMode render_mode() const noexcept;
    // Reconciles the fixed 5 by 5 LOD0 surface region around the origin chunk.
    void tick(const TerrainTickContext& context);
    // Samples the deterministic heightfield at one world X/Z coordinate.
    [[nodiscard]] TerrainSample sample(float world_x, float world_z) const;
    // Returns the LOD0 surface chunk containing one world X/Z coordinate.
    [[nodiscard]] TerrainChunkId chunk_id_containing(float world_x, float world_z) const;
    // Finds one existing chunk, or nullptr.
    [[nodiscard]] TerrainChunk* find_chunk(TerrainChunkId id) noexcept;
    // Finds one existing chunk, or nullptr.
    [[nodiscard]] const TerrainChunk* find_chunk(TerrainChunkId id) const noexcept;
    // Finds or creates one validated chunk and returns a stable pointer to it.
    [[nodiscard]] TerrainChunk* get_or_create_chunk(TerrainChunkId id);
    // Reports the number of currently streamed chunks.
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    // Returns the current chunk map for render/debug iteration.
    [[nodiscard]] const std::map<TerrainChunkId, TerrainChunk>& chunks() const noexcept;
    // Clears all streamed chunks without changing the generator config.
    void clear_chunks() noexcept;

private:
    TerrainConfig m_config;
    std::map<TerrainChunkId, TerrainChunk> m_chunks;
    Ptr<Material> m_material;
    Ptr<Material> m_debug_plane_material;
    TerrainRenderMode m_render_mode{TerrainRenderMode::ClayMesh};
};

// Validates user-tunable terrain generation inputs.
void validate_terrain_config(const TerrainConfig& config);

} // namespace ofg
