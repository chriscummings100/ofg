// Portable source-texture encoding and exact descendant mappings, independent of graphics objects.
#pragma once
#include "terrain/terrain-content.h"

namespace ofg::terrain {
inline constexpr size_t terrainTextureBytes = terrainDataSamples * terrainDataSamples * 9;
// Portable uploads pad each RGBA16F/R8 row to 256 bytes; staging is distinct from retained texel storage.
inline constexpr size_t terrainTextureUploadBytes =
    (((terrainDataSamples * 8 + 255) & ~size_t(255)) + ((terrainDataSamples + 255) & ~size_t(255))) *
    terrainDataSamples;
// Encodes height as two half floats in units of 1024 metres, plus sediment/water in the same units.
// Negative sediment/water denotes unknown. The source arrays remain the authoritative physical values.
std::vector<uint16_t> encodeTerrainTexture(const TerrainTile& source);
// Returns UV scale X/Z and offset X/Z, mapping mesh UV [0,1] to apron-inclusive texel centres.
std::array<float, 4> terrainTextureMapping(const TerrainTile& source, NodeAddress node);
} // namespace ofg::terrain
