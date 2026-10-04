// Strict decoding of the project's deterministic, offline-generated IBL fixture format.
#include "resources/environment.h"
#include "core/engine-error.h"
#include <cstring>
namespace ofg {
std::shared_ptr<Environment> Environment::fromBytes(std::span<const uint8_t> bytes)
{
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "OFGIBL1\0", 8) != 0)
    {
        throw EngineError("Invalid OFG IBL header.");
    }
    auto result = std::shared_ptr<Environment>(new Environment);
    size_t cursor = 8;
    // The format is explicitly little endian; host endianness is not part of the file contract.
    auto word = [&]()
    {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            value |= uint32_t(bytes[cursor++]) << (8 * i);
        }
        return value;
    };
    result->m_size = word();
    result->m_mipCount = word();
    result->m_lookupSize = word();
    if (!result->m_size || result->m_size > 256 || !result->m_lookupSize || result->m_lookupSize > 256 ||
        !result->m_mipCount || result->m_mipCount > 9 || (result->m_size & (result->m_size - 1)) ||
        (result->m_size >> (result->m_mipCount - 1)) != 1)
    {
        throw EngineError("Invalid OFG IBL dimensions/mip chain.");
    }
    size_t count = 0;
    for (uint32_t mip = 0; mip < result->m_mipCount; ++mip)
    {
        count += size_t(result->m_size >> mip) * (result->m_size >> mip) * 6 * 4;
    }
    size_t lutCount = size_t(result->m_lookupSize) * result->m_lookupSize * 4;
    if (bytes.size() != 20 + (count * 3 + lutCount) * 2)
    {
        throw EngineError("OFG IBL payload size mismatch.");
    }
    // Reject infinities, NaNs and negative radiance before any GPU allocation.
    auto read = [&](std::vector<uint16_t>& values, size_t n)
    {
        values.resize(n);
        for (auto& value : values)
        {
            value = uint16_t(bytes[cursor]) | uint16_t(bytes[cursor + 1]) << 8;
            cursor += 2;
            if ((value & 0x7c00) == 0x7c00 || (value & 0x8000))
            {
                throw EngineError("Invalid OFG IBL radiance.");
            }
        }
    };
    for (auto& cube : result->m_cubes)
    {
        read(cube, count);
    }
    read(result->m_lookup, lutCount);
    return result;
}
size_t Environment::byteSize() const noexcept
{
    return (m_cubes[0].size() + m_cubes[1].size() + m_cubes[2].size() + m_lookup.size()) * sizeof(uint16_t);
}
} // namespace ofg
