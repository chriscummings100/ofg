// Immutable baked RGBA16F cube mip chains and paired BRDF lookup; Graphics owns GPU preparation.
#pragma once
#include "resources/resource.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace ofg {
struct EnvironmentGpuData;
class Graphics;
class EnvironmentRenderer;
class Environment : public Resource
{
public:
    // Reads the bounded OFGIBL1 little-endian bake format; rejects malformed/nonfinite half-float payloads.
    static std::shared_ptr<Environment> fromBytes(std::span<const uint8_t> bytes);
    // Returns immutable face-major, then mip-major RGBA16F bytes for diffuse/GGX/Charlie respectively.
    const std::vector<uint16_t>& cube(size_t index) const { return m_cubes.at(index); }
    // Returns the combined lookup: RG = GGX, B = Charlie radiance, A = sheen directional albedo.
    const std::vector<uint16_t>& lookup() const noexcept { return m_lookup; }
    // Returns the common cube edge length in texels.
    uint32_t size() const noexcept { return m_size; }
    // Returns the complete mip-chain length shared by the three cubes.
    uint32_t mipCount() const noexcept { return m_mipCount; }
    // Returns the square lookup texture edge length in texels.
    uint32_t lookupSize() const noexcept { return m_lookupSize; }
    // Counts retained CPU payload bytes (also the unpadded GPU texel payload).
    size_t byteSize() const noexcept;

private:
    friend class Graphics;
    friend class EnvironmentRenderer;
    // Constructs validated data through fromBytes only.
    Environment() = default;
    uint32_t m_size = 0, m_mipCount = 0, m_lookupSize = 0;
    std::array<std::vector<uint16_t>, 3> m_cubes;
    std::vector<uint16_t> m_lookup;
    std::shared_ptr<EnvironmentGpuData> m_gpu;
};
} // namespace ofg
