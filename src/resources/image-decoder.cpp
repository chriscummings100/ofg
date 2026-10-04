// One CPU-only stb implementation; only PNG/JPEG memory decoding is enabled.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>

#include "resources/image-decoder.h"
#include "core/engine-error.h"
#include <cstring>
#include <memory>

namespace ofg {
DecodedImage decodeImage(std::span<const std::byte> encoded)
{
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    int width = 0, height = 0, channels = 0;
    if (encoded.empty() || encoded.size() > 64 * 1024 * 1024 ||
        !stbi_info_from_memory(bytes, int(encoded.size()), &width, &height, &channels) || width <= 0 || height <= 0 ||
        width > 8192 || height > 8192 || uint64_t(width) * height * 4 > 256 * 1024 * 1024)
    {
        throw EngineError("Invalid PNG/JPEG header or image exceeds the pixel budget.");
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes, int(encoded.size()), &width, &height, &channels, 4),
        stbi_image_free
    );
    if (!pixels)
    {
        throw EngineError(std::string("Image decode failed: ") + stbi_failure_reason());
    }
    DecodedImage result{uint32_t(width), uint32_t(height), std::vector<std::byte>(size_t(width) * height * 4)};
    std::memcpy(result.pixels.data(), pixels.get(), result.pixels.size());
    return result;
}
} // namespace ofg
