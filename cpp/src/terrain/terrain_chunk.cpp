// Addressable procedural terrain chunk data implementation.
#include "ofg/terrain/terrain_chunk.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/terrain/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// Builds one local-space 32 by 32 meter XZ debug quad.
[[nodiscard]] std::vector<MeshVertex> terrain_debug_plane_vertices() {
    const float edge = static_cast<float>(terrain_chunk_lod0_cells_per_edge);
    return {
        MeshVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        MeshVertex{{edge, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        MeshVertex{{edge, 0.0f, edge}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        MeshVertex{{0.0f, 0.0f, edge}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
}

[[nodiscard]] MeshVertex terrain_render_vertex(
    const Terrain& terrain, const TerrainChunk& chunk, std::int32_t sample_x, std::int32_t sample_z) {
    const float local_x = static_cast<float>(sample_x);
    const float local_z = static_cast<float>(sample_z);
    const float world_x = chunk.world_min_x() + local_x;
    const float world_z = chunk.world_min_z() + local_z;
    const float height = chunk.heightfield_sample_at(sample_x, sample_z).m_height;

    const float left = terrain.sample(world_x - 1.0f, world_z).m_height;
    const float right = terrain.sample(world_x + 1.0f, world_z).m_height;
    const float back = terrain.sample(world_x, world_z - 1.0f).m_height;
    const float forward = terrain.sample(world_x, world_z + 1.0f).m_height;
    const math::Vec3 tangent_x = math::vec3(2.0f, right - left, 0.0f);
    const math::Vec3 tangent_z = math::vec3(0.0f, forward - back, 2.0f);

    std::string error;
    const std::optional<math::Vec3> normal = math::normalize(math::cross(tangent_z, tangent_x), error);
    if (!normal.has_value()) {
        throw EngineError(error.empty() ? "Terrain render mesh normal creation failed." : error);
    }
    const std::optional<math::Vec3> tangent = math::normalize(tangent_x, error);
    if (!tangent.has_value()) {
        throw EngineError(error.empty() ? "Terrain render mesh tangent creation failed." : error);
    }

    const float edge = static_cast<float>(terrain_chunk_lod0_cells_per_edge);
    return MeshVertex{{local_x, height, local_z},
        {normal->x, normal->y, normal->z},
        {tangent->x, tangent->y, tangent->z, 1.0f},
        {local_x / edge, local_z / edge}};
}

[[nodiscard]] std::vector<MeshVertex> terrain_render_vertices(const Terrain& terrain, const TerrainChunk& chunk) {
    std::vector<MeshVertex> vertices;
    vertices.reserve(
        static_cast<std::size_t>(terrain_chunk_lod0_vertices_per_edge * terrain_chunk_lod0_vertices_per_edge));
    for (std::int32_t sample_z = 0; sample_z < terrain_chunk_lod0_vertices_per_edge; ++sample_z) {
        for (std::int32_t sample_x = 0; sample_x < terrain_chunk_lod0_vertices_per_edge; ++sample_x) {
            vertices.push_back(terrain_render_vertex(terrain, chunk, sample_x, sample_z));
        }
    }
    return vertices;
}

[[nodiscard]] std::vector<std::uint32_t> terrain_render_indices() {
    std::vector<std::uint32_t> indices;
    indices.reserve(
        static_cast<std::size_t>(terrain_chunk_lod0_cells_per_edge * terrain_chunk_lod0_cells_per_edge) * 6U);
    for (std::int32_t cell_z = 0; cell_z < terrain_chunk_lod0_cells_per_edge; ++cell_z) {
        for (std::int32_t cell_x = 0; cell_x < terrain_chunk_lod0_cells_per_edge; ++cell_x) {
            const std::uint32_t top_left = static_cast<std::uint32_t>(heightfield_index(cell_x, cell_z));
            const std::uint32_t top_right = static_cast<std::uint32_t>(heightfield_index(cell_x + 1, cell_z));
            const std::uint32_t bottom_right = static_cast<std::uint32_t>(heightfield_index(cell_x + 1, cell_z + 1));
            const std::uint32_t bottom_left = static_cast<std::uint32_t>(heightfield_index(cell_x, cell_z + 1));
            indices.insert(indices.end(), {top_left, top_right, bottom_right, top_left, bottom_right, bottom_left});
        }
    }
    return indices;
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

// Returns the generated terrain render mesh attached to this chunk, or nullptr.
Mesh* TerrainChunk::render_mesh() noexcept {
    return m_render_mesh.get();
}

// Returns the generated terrain render mesh attached to this chunk, or nullptr.
Mesh* TerrainChunk::render_mesh() const noexcept {
    return m_render_mesh.get();
}

// Returns the debug-plane mesh resource attached to this chunk, or nullptr.
Mesh* TerrainChunk::debug_plane_mesh() noexcept {
    return m_debug_plane_mesh.get();
}

// Returns the debug-plane mesh resource attached to this chunk, or nullptr.
Mesh* TerrainChunk::debug_plane_mesh() const noexcept {
    return m_debug_plane_mesh.get();
}

// Returns the debug-plane texture resource attached to this chunk, or nullptr.
Texture* TerrainChunk::debug_plane_texture() noexcept {
    return m_debug_plane_texture.get();
}

// Returns the debug-plane texture resource attached to this chunk, or nullptr.
Texture* TerrainChunk::debug_plane_texture() const noexcept {
    return m_debug_plane_texture.get();
}

// Returns the material override that binds this chunk's debug texture.
std::span<const MaterialOverride> TerrainChunk::debug_plane_material_overrides() const noexcept {
    return m_debug_plane_material_overrides;
}

// Generates any missing chunk-owned data required by the current Terrain state.
void TerrainChunk::generate(const Terrain& terrain) {
    if (!has_heightfield()) {
        generate_heightfield(terrain);
    }
    if (terrain.material() != nullptr) {
        generate_render_mesh(terrain);
    }
    if (terrain.render_mode() == TerrainRenderMode::HeightDebugPlane && terrain.debug_plane_material() != nullptr) {
        generate_debug_plane(terrain);
    }
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

// Creates the chunk-local heightfield render mesh from generated samples.
void TerrainChunk::generate_render_mesh(const Terrain& terrain) {
    if (m_render_mesh != nullptr) {
        return;
    }
    if (!has_heightfield()) {
        generate_heightfield(terrain);
    }

    Material* material = terrain.material();
    if (material == nullptr) {
        throw EngineError("Terrain render mesh generation requires Terrain::material().");
    }

    std::vector<MeshVertex> vertices = terrain_render_vertices(terrain, *this);
    std::vector<std::uint32_t> indices = terrain_render_indices();
    std::vector<SubMesh> submeshes{SubMesh{"terrain clay", 0, static_cast<std::uint32_t>(indices.size()), material}};

    Mesh& mesh = Resources::create_mesh("OFG terrain render mesh" + chunk_label_suffix(m_id));
    mesh.init(std::move(vertices), std::move(indices), std::move(submeshes));
    m_render_mesh = &mesh;
}

// Creates chunk-owned debug plane mesh, texture, and texture material override.
void TerrainChunk::generate_debug_plane(const Terrain& terrain) {
    if (!has_heightfield()) {
        generate_heightfield(terrain);
    }

    Material* debug_material = terrain.debug_plane_material();
    if (debug_material == nullptr) {
        throw EngineError("Terrain debug plane generation requires Terrain::debug_plane_material().");
    }

    Texture* texture = m_debug_plane_texture.get();
    if (texture == nullptr) {
        Texture& created_texture =
            Resources::create_texture("OFG terrain height debug texture" + chunk_label_suffix(m_id));
        created_texture.init_from_r16_float_pixels(static_cast<std::uint32_t>(terrain_chunk_lod0_vertices_per_edge),
            static_cast<std::uint32_t>(terrain_chunk_lod0_vertices_per_edge),
            heightfield_debug_pixels(*this));
        m_debug_plane_texture = &created_texture;
        texture = &created_texture;
    }

    if (m_debug_plane_mesh == nullptr) {
        std::vector<SubMesh> submeshes{SubMesh{"terrain height debug", 0, 6, debug_material}};
        Mesh& mesh = Resources::create_mesh("OFG terrain debug plane mesh" + chunk_label_suffix(m_id));
        mesh.init(terrain_debug_plane_vertices(), {0, 1, 2, 0, 2, 3}, std::move(submeshes));
        m_debug_plane_mesh = &mesh;
    }

    const bool has_live_override =
        !m_debug_plane_material_overrides.empty() && m_debug_plane_material_overrides[0].m_material.get() != nullptr;
    if (!has_live_override) {
        PropertyBag properties;
        const float divisor = std::max(terrain.config().m_height_scale * 2.0f, 0.0001f);
        properties.set("height_debug_options", math::vec4(divisor, 0.0f, 0.0f, 0.0f));
        properties.set("heightfield_texture", texture);

        Material& material = Resources::create_material("OFG terrain height debug material" + chunk_label_suffix(m_id));
        material.init(debug_material->shader(), std::move(properties));
        m_debug_plane_material_overrides = {MaterialOverride{0, &material}};
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
