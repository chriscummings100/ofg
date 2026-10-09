// Strict little-endian terrain decoding and exact ancestor sample mapping shared by both hosts.
#include "terrain/terrain-content.h"
#include "core/engine-error.h"
#include <json.hpp>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>

namespace ofg::terrain {
std::string decodeTerrainRevision(std::string_view json)
{
    const auto value = nlohmann::json::parse(json).at("revision").get<std::string>();
    if (value.size() != 32 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw EngineError("Invalid latest terrain revision.");
    return value;
}
namespace {
constexpr size_t headerBytes = 192, sampleCount = size_t(terrainDataSamples) * terrainDataSamples;
constexpr size_t responseBytes = headerBytes + sampleCount * 15;

// Reads a little-endian integer explicitly; callers have already validated the fixed response length.
uint64_t readInteger(std::span<const std::byte> bytes, size_t offset, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i)
        value |= uint64_t(std::to_integer<uint8_t>(bytes[offset + i])) << (8 * i);
    return value;
}

// Decodes IEEE-754 double bits without alignment assumptions.
double readDouble(std::span<const std::byte> bytes, size_t offset)
{
    return std::bit_cast<double>(readInteger(bytes, offset, 8));
}

// Computes IEEE CRC-32 over header and body, treating the stored checksum field as zero.
uint32_t responseCrc(std::span<const std::byte> bytes)
{
    static const auto table = []
    {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i)
        {
            auto crc = i;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
            values[i] = crc;
        }
        return values;
    }();
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        const auto value = (i >= 136 && i < 140) ? 0 : std::to_integer<uint8_t>(bytes[i]);
        crc = (crc >> 8) ^ table[(crc ^ value) & 255];
    }
    return ~crc;
}

// Parses an exact canonical signed coordinate without a JSON double conversion.
int64_t coordinate(const nlohmann::json& value)
{
    const auto text = value.get<std::string>();
    int64_t result = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || std::to_string(result) != text)
        throw EngineError("Invalid terrain manifest coordinate.");
    return result;
}
} // namespace

TerrainManifest decodeTerrainManifest(std::string_view text)
{
    if (text.size() > (256 << 10))
        throw EngineError("Terrain manifest exceeds 256 KiB.");
    const auto json = nlohmann::json::parse(text);
    TerrainManifest result;
    result.island = json.at("island").get<std::string>();
    result.revision = json.at("revision").get<std::string>();
    if (result.island.empty() || result.island.size() > 64 ||
        result.island.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos ||
        result.revision.size() != 32 || result.revision.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw EngineError("Invalid terrain island or revision identity.");
    for (size_t i = 0; i < 16; ++i)
    {
        unsigned value = 0;
        std::from_chars(result.revision.data() + i * 2, result.revision.data() + i * 2 + 2, value, 16);
        result.revisionBytes[i] = uint8_t(value);
    }
    result.rootWidth = json.at("root_width").get<double>();
    result.sourceSpacing = json.at("source_spacing").get<double>();
    const int depth = json.at("terminal_depth").get<int>();
    const auto intervals = json.at("source_intervals").get<uint32_t>();
    if (json.at("format_version") != 1 || json.at("intervals") != terrainDataIntervals || json.at("halo") != 1 ||
        result.rootWidth != 131072 || !std::isfinite(result.sourceSpacing) || result.sourceSpacing < 1 ||
        result.sourceSpacing > 512 || std::floor(std::log2(result.sourceSpacing)) != std::log2(result.sourceSpacing) ||
        depth < 0 || depth > 9 || std::ldexp(result.rootWidth / terrainDataIntervals, -depth) != result.sourceSpacing ||
        intervals < 256 || intervals > 2048 || (intervals & (intervals - 1)))
        throw EngineError("Unsupported terrain source lattice or dimensions.");
    result.terminalDepth = uint8_t(depth);
    result.sourceWidth = intervals * result.sourceSpacing;
    result.minimumHeight = json.at("height_min").get<double>();
    result.maximumHeight = json.at("height_max").get<double>();
    result.seaLevel = json.at("sea_level").get<double>();
    result.sourceOrigin.cell = {coordinate(json.at("origin_root_x")), 0, coordinate(json.at("origin_root_z"))};
    result.sourceOrigin.local = {json.at("origin_local_x").get<double>(), 0, json.at("origin_local_z").get<double>()};
    for (const auto v :
         {result.minimumHeight,
          result.maximumHeight,
          result.seaLevel,
          result.sourceOrigin.local[0],
          result.sourceOrigin.local[2]})
        if (!std::isfinite(v))
            throw EngineError("Nonfinite terrain manifest value.");
    if (result.minimumHeight > result.maximumHeight || std::abs(result.minimumHeight) > 10000 ||
        std::abs(result.maximumHeight) > 10000)
        throw EngineError("Invalid terrain manifest height bounds.");
    for (size_t axis : {size_t(0), size_t(2)})
        if (result.sourceOrigin.local[axis] < 0 || result.sourceOrigin.local[axis] >= result.rootWidth ||
            std::fmod(result.sourceOrigin.local[axis], result.sourceSpacing) != 0)
            throw EngineError("Terrain source origin is not on its canonical lattice.");
    result.parametersJson = json.at("parameters").dump();
    const auto& palette = json.at("material_palette");
    if (!palette.is_array() || palette.empty() || palette.size() > 2 || palette[0].at("id") != 1 ||
        (palette.size() == 2 && palette[1].at("id") != 2))
        throw EngineError("Unsupported terrain material palette.");
    return result;
}

NodeAddress terrainTileAddress(NodeAddress node, uint8_t terminalDepth)
{
    validateAddress(node);
    if (terminalDepth > 9)
        throw EngineError("Unsupported terrain terminal depth.");
    if (node.depth > terminalDepth)
    {
        const auto shift = node.depth - terminalDepth;
        node.x >>= shift;
        node.z >>= shift;
        node.depth = terminalDepth;
    }
    node.y = 0;
    node.cell.y = 0;
    return node;
}

std::string terrainTilePath(const TerrainManifest& manifest, NodeAddress node)
{
    const auto address = terrainTileAddress(node, manifest.terminalDepth);
    return "/v1/islands/" + manifest.island + "/revisions/" + manifest.revision + "/terrain/" +
           std::to_string(address.cell.x) + "/" + std::to_string(address.cell.z) + "/" + std::to_string(address.depth) +
           "/" + std::to_string(address.x) + "/" + std::to_string(address.z) + ".bin";
}

std::shared_ptr<const TerrainTile> decodeTerrainTile(
    std::span<const std::byte> bytes,
    const TerrainManifest& manifest,
    NodeAddress expected
)
{
    expected = terrainTileAddress(expected, manifest.terminalDepth);
    if (bytes.size() != responseBytes || std::memcmp(bytes.data(), "OFGTRN1\0", 8) || readInteger(bytes, 8, 4) != 1 ||
        readInteger(bytes, 12, 4) != headerBytes || readInteger(bytes, 16, 4) != sampleCount * 15 ||
        readInteger(bytes, 20, 4) != terrainDataIntervals || readInteger(bytes, 24, 4) != 1)
        throw EngineError("Invalid terrain response size or format.");
    if (readInteger(bytes, 28, 4) != expected.depth ||
        std::bit_cast<int64_t>(readInteger(bytes, 32, 8)) != expected.cell.x ||
        std::bit_cast<int64_t>(readInteger(bytes, 40, 8)) != expected.cell.z ||
        readInteger(bytes, 48, 4) != expected.x || readInteger(bytes, 52, 4) != expected.z ||
        std::memcmp(bytes.data() + 56, manifest.revisionBytes.data(), 16) ||
        readDouble(bytes, 72) != manifest.rootWidth || readDouble(bytes, 80) != manifest.sourceSpacing)
        throw EngineError("Terrain response identity does not match its request.");
    if (responseCrc(bytes) != readInteger(bytes, 136, 4))
        throw EngineError("Terrain response checksum mismatch.");
    for (size_t i = 140; i < headerBytes; ++i)
        if (bytes[i] != std::byte{})
            throw EngineError("Terrain response reserved bytes are nonzero.");
    const double width = std::ldexp(manifest.rootWidth, -expected.depth);
    const double low = readDouble(bytes, 96), high = readDouble(bytes, 120);
    if (!std::isfinite(low) || !std::isfinite(high) || low > high || low < manifest.minimumHeight - 1e-5 ||
        high > manifest.maximumHeight + 1e-5 || readDouble(bytes, 88) != expected.x * width ||
        readDouble(bytes, 104) != expected.z * width || readDouble(bytes, 112) != (expected.x + 1) * width ||
        readDouble(bytes, 128) != (expected.z + 1) * width)
        throw EngineError("Invalid terrain conservative bounds.");
    auto tile = std::make_shared<TerrainTile>();
    tile->address = expected;
    tile->revision = manifest.revisionBytes;
    tile->sourceSpacing = manifest.sourceSpacing;
    tile->minimumHeight = low;
    tile->maximumHeight = high;
    size_t offset = headerBytes;
    for (auto* field : {&tile->surface, &tile->bedrock, &tile->water})
    {
        field->resize(sampleCount);
        for (auto& value : *field)
        {
            value = std::bit_cast<float>(uint32_t(readInteger(bytes, offset, 4)));
            offset += 4;
            if (!std::isfinite(value))
                throw EngineError("Nonfinite terrain sample.");
        }
    }
    tile->material.resize(sampleCount);
    tile->validity.resize(sampleCount);
    for (auto& value : tile->material)
    {
        value = uint16_t(readInteger(bytes, offset, 2));
        offset += 2;
    }
    for (auto& value : tile->validity)
        value = std::to_integer<uint8_t>(bytes[offset++]);
    for (size_t i = 0; i < sampleCount; ++i)
    {
        const auto bits = tile->validity[i];
        if ((bits & ~7) || ((bits & 1) && tile->bedrock[i] > tile->surface[i]) || ((bits & 2) && tile->water[i] < 0) ||
            ((bits & 4) && tile->material[i] != 1 && tile->material[i] != 2) ||
            (!(bits & 1) && tile->bedrock[i] != 0) || (!(bits & 2) && tile->water[i] != 0) ||
            (!(bits & 4) && tile->material[i] != 0))
            throw EngineError("Invalid terrain attributes.");
        const auto x = i % terrainDataSamples, z = i / terrainDataSamples;
        if (x > 0 && x < 258 && z > 0 && z < 258 && (tile->surface[i] < low || tile->surface[i] > high))
            throw EngineError("Terrain bounds exclude a returned sample.");
    }
    return tile;
}

double TerrainTile::height(double x, double z) const
{
    const double spacing = std::ldexp(131072.0 / terrainDataIntervals, -address.depth);
    x = x / spacing + 1;
    z = z / spacing + 1;
    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 || x > 258 || z > 258)
        throw EngineError("Terrain sampling leaves its source apron.");
    const auto ix = std::min(uint32_t(x), 257u), iz = std::min(uint32_t(z), 257u);
    const double tx = x - ix, tz = z - iz;
    const size_t i = size_t(iz) * terrainDataSamples + ix;
    return std::lerp(
        std::lerp(double(surface[i]), double(surface[i + 1]), tx),
        std::lerp(double(surface[i + terrainDataSamples]), double(surface[i + terrainDataSamples + 1]), tx),
        tz
    );
}

std::array<double, 2> TerrainTile::sourcePosition(NodeAddress node, double x, double z) const
{
    validateAddress(node);
    if (node.cell.x != address.cell.x || node.cell.z != address.cell.z || node.depth < address.depth ||
        (node.x >> (node.depth - address.depth)) != address.x || (node.z >> (node.depth - address.depth)) != address.z)
        throw EngineError("Terrain input does not contain the requesting node.");
    const double width = std::ldexp(131072.0, -node.depth), sourceWidth = std::ldexp(131072.0, -address.depth);
    return {node.x * width - address.x * sourceWidth + x, node.z * width - address.z * sourceWidth + z};
}

bool TerrainTile::terminal() const noexcept
{
    return std::ldexp(131072.0 / terrainDataIntervals, -address.depth) <= sourceSpacing;
}
} // namespace ofg::terrain
