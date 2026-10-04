// Private PNG/JPEG memory decoder shared by Texture and glTF image import.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ofg {
struct DecodedImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> pixels; // RGBA8, top-to-bottom; color interpretation belongs to the caller.
};
// Decodes at most 64 MiB of PNG/JPEG data, with 8192-axis and 256 MiB base-pixel limits.
// Validates dimensions before decoder allocation; throws EngineError on failure.
DecodedImage decodeImage(std::span<const std::byte> encoded);
} // namespace ofg
