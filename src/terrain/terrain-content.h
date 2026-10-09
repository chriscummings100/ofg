// Immutable terrain source metadata and validated sample tiles, independent of HTTP and graphics.
#pragma once

#include "terrain/terrain-address.h"
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace ofg::terrain {
inline constexpr uint32_t terrainDataIntervals = 256;
inline constexpr uint32_t terrainDataSamples = 259;
inline constexpr size_t terrainResponseLimit = 2ull << 20;

struct TerrainManifest
{
    std::string island, revision;
    std::array<uint8_t, 16> revisionBytes{};
    double rootWidth = 131072, sourceSpacing = 32, minimumHeight = 0, maximumHeight = 0, seaLevel = 0;
    uint8_t terminalDepth = 4;
    WorldPosition sourceOrigin;
    double sourceWidth = 0;
    std::string parametersJson;
};

struct TerrainTile
{
    NodeAddress address; // Horizontal identity: cell.y and y are always zero.
    std::array<uint8_t, 16> revision{};
    double sourceSpacing = 0, minimumHeight = 0, maximumHeight = 0;
    std::vector<float> surface, bedrock, water;
    std::vector<uint16_t> material;
    std::vector<uint8_t> validity;

    // Bilinearly samples core/apron coordinates relative to this tile's lower X/Z edge, in metres.
    double height(double x, double z) const;
    // Maps a descendant's node-local X/Z metres directly into this source tile, without copying samples.
    std::array<double, 2> sourcePosition(NodeAddress node, double x, double z) const;
    // Reports actual owned CPU array capacities, excluding non-owning references.
    size_t allocatedBytes() const noexcept
    {
        return (surface.capacity() + bedrock.capacity() + water.capacity()) * sizeof(float) +
               material.capacity() * sizeof(uint16_t) + validity.capacity();
    }
    // Returns true at the source-resolution cutoff; descendants reuse this same allocation.
    bool terminal() const noexcept;
};

// Parses a bounded immutable manifest and checks format, canonical lattice and representable placement.
TerrainManifest decodeTerrainManifest(std::string_view json);
// Parses only the latest pointer's immutable revision, rejecting invalid path fragments.
std::string decodeTerrainRevision(std::string_view json);
// Parses an explicit wire layout, verifies checksum and exact captured identity before returning owned arrays.
std::shared_ptr<const TerrainTile> decodeTerrainTile(
    std::span<const std::byte> bytes,
    const TerrainManifest& manifest,
    NodeAddress expected
);
// Forms a horizontal tile identity at the node depth or its terminal source ancestor's depth.
NodeAddress terrainTileAddress(NodeAddress node, uint8_t terminalDepth);
// Builds the immutable relative URL; address validation prevents path fragments from user data.
std::string terrainTilePath(const TerrainManifest& manifest, NodeAddress node);
} // namespace ofg::terrain
