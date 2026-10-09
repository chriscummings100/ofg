// Half-float terrain textures preserve intermediate height detail on the portable filterable GPU format.
#include "terrain/terrain-shading.h"
#include "core/engine-error.h"
#include <bit>
#include <cmath>

namespace ofg::terrain {
namespace {
// Converts finite binary32 to binary16, rounding ties to even; rejects overflow rather than producing infinity.
uint16_t half(float value)
{
    const auto bits = std::bit_cast<uint32_t>(value);
    const auto sign = (bits >> 16) & 0x8000;
    const int exponent = int((bits >> 23) & 255) - 127;
    if (!std::isfinite(value) || std::abs(value) > 65504)
        throw EngineError("Terrain attribute exceeds the shading texture range.");
    if (exponent < -25)
        return uint16_t(sign);
    const uint32_t significand = (bits & 0x7fffff) | 0x800000;
    const int shift = exponent < -14 ? -exponent - 1 : 13;
    const uint32_t rounded = (significand + ((1u << (shift - 1)) - 1) + ((significand >> shift) & 1)) >> shift;
    return uint16_t(sign | (exponent < -14 ? rounded : uint32_t(exponent + 14) * 1024 + rounded));
}
// Decodes finite binary16 for the residual calculation; no GPU or platform intrinsics are required.
float full(uint16_t value)
{
    const int exponent = (value >> 10) & 31;
    const float magnitude = std::ldexp(float((value & 1023) + (exponent ? 1024 : 0)), exponent ? exponent - 25 : -24);
    return value & 0x8000 ? -magnitude : magnitude;
}
} // namespace

std::vector<uint16_t> encodeTerrainTexture(const TerrainTile& source)
{
    const size_t count = size_t(terrainDataSamples) * terrainDataSamples;
    if (source.surface.size() != count || source.bedrock.size() != count || source.water.size() != count ||
        source.validity.size() != count)
        throw EngineError("Terrain shading requires complete source channels.");
    std::vector<uint16_t> pixels(count * 4);
    for (size_t i = 0; i < count; ++i)
    {
        const float height = source.surface[i] / 1024;
        pixels[i * 4] = half(height);
        pixels[i * 4 + 1] = half(height - full(pixels[i * 4]));
        pixels[i * 4 + 2] = half(source.validity[i] & 1 ? (source.surface[i] - source.bedrock[i]) / 1024 : -1);
        pixels[i * 4 + 3] = half(source.validity[i] & 2 ? source.water[i] / 1024 : -1);
    }
    return pixels;
}

std::array<float, 4> terrainTextureMapping(const TerrainTile& source, NodeAddress node)
{
    const auto origin = source.sourcePosition(node, 0, 0);
    const double spacing = std::ldexp(131072.0 / terrainDataIntervals, -source.address.depth);
    const double width = std::ldexp(131072.0, -node.depth);
    const float scale = float(width / spacing / terrainDataSamples);
    return {
        scale,
        scale,
        float((origin[0] / spacing + 1.5) / terrainDataSamples),
        float((origin[1] / spacing + 1.5) / terrainDataSamples)
    };
}
} // namespace ofg::terrain
