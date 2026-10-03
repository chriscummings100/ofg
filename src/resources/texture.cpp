// Pixel-layout validation, explicit mip ranges, and cancellable platform image loading.
#include "resources/texture.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stb_image.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

namespace ofg {
namespace {
constexpr size_t maxEncodedBytes = 64 * 1024 * 1024;
constexpr size_t maxPixelBytes = 256 * 1024 * 1024;
} // namespace

// Owns one platform read. Callbacks only record completion; decode stays on the resource scheduler.
struct TextureLoad
{
#ifdef __EMSCRIPTEN__
    emscripten_fetch_t* fetch = nullptr;
    bool finished = false;
    bool succeeded = false;
    bool cancelling = false;
    // Records completion without allocation or exceptions crossing the C callback boundary.
    static void complete(emscripten_fetch_t* request)
    {
        auto& load = *static_cast<TextureLoad*>(request->userData);
        if (!load.cancelling)
        {
            load.finished = true;
            load.succeeded = request->status >= 200 && request->status < 300;
        }
    }
    // Closes pending or completed I/O; close can synchronously invoke complete().
    ~TextureLoad()
    {
        cancelling = true;
        if (fetch)
        {
            emscripten_fetch_close(fetch);
        }
    }
#else
    std::vector<std::byte> bytes;
    std::ifstream file;
    size_t read = 0;
#endif

    // Starts I/O once; the owning resource must retain this state until cancellation/completion.
    void start(const std::string& path)
    {
#ifdef __EMSCRIPTEN__
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
        attr.timeoutMSecs = 30000;
        attr.userData = this;
        attr.onsuccess = complete;
        attr.onerror = complete;
        fetch = emscripten_fetch(&attr, path.c_str());
        if (!fetch)
        {
            throw EngineError("Could not start image fetch.");
        }
#else
        file.open(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw EngineError("Could not open image file.");
        }
        const auto size = file.tellg();
        if (size <= 0 || uint64_t(size) > maxEncodedBytes)
        {
            throw EngineError("Invalid image file size (limit 64 MiB).");
        }
        bytes.resize(size_t(size));
        file.seekg(0);
#endif
    }

    // Advances at most 256 KiB of native reading, or polls the browser callback result.
    bool poll()
    {
#ifdef __EMSCRIPTEN__
        if (!finished)
        {
            return false;
        }
        if (!succeeded)
        {
            throw EngineError("Image fetch failed, HTTP " + std::to_string(fetch->status));
        }
        if (!fetch->numBytes || fetch->numBytes > maxEncodedBytes)
        {
            throw EngineError("Invalid image response size (limit 64 MiB).");
        }
#else
        if (read < bytes.size())
        {
            const auto count = std::min(size_t(256 * 1024), bytes.size() - read);
            if (!file.read(reinterpret_cast<char*>(bytes.data() + read), count))
            {
                throw EngineError("Image file read failed.");
            }
            read += count;
            if (read == bytes.size())
            {
                file.close();
            }
            return false;
        }
#endif
        return true;
    }

    // Borrows completed bytes until this request state is destroyed; poll() must have succeeded.
    std::span<const std::byte> data() const
    {
#ifdef __EMSCRIPTEN__
        return {reinterpret_cast<const std::byte*>(fetch->data), size_t(fetch->numBytes)};
#else
        return bytes;
#endif
    }
};

size_t texturePixelSize(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8Unorm:
        return 1;
    case TextureFormat::RG8Unorm:
    case TextureFormat::R16Float:
        return 2;
    case TextureFormat::RGBA8Unorm:
    case TextureFormat::RGBA8UnormSrgb:
    case TextureFormat::RG16Float:
    case TextureFormat::R32Float:
        return 4;
    case TextureFormat::RGBA16Float:
    case TextureFormat::RG32Float:
        return 8;
    case TextureFormat::RGBA32Float:
        return 16;
    default:
        throw EngineError("Unknown texture format.");
    }
}

Texture::Texture() = default;
Texture::Texture(std::string path)
    : Resource(std::move(path))
{
}
Texture::~Texture() = default;
TextureView::TextureView(std::shared_ptr<Texture> texture, TextureViewDesc desc)
    : m_texture(std::move(texture))
    , m_desc(desc)
{
}

void Texture::setPixels(TextureDesc desc, TextureData data)
{
    const size_t pixelSize = texturePixelSize(desc.format);
    if (!desc.width || !desc.height || desc.width > 8192 || desc.height > 8192 ||
        (desc.mipmaps != TextureMipmaps::None && desc.mipmaps != TextureMipmaps::Generate))
    {
        throw EngineError("Texture requires dimensions in [1,8192] and a valid mip policy.");
    }
    const size_t rowBytes = size_t(desc.width) * pixelSize;
    if (rowBytes > maxPixelBytes / desc.height)
    {
        throw EngineError("Texture exceeds the 256 MiB base-pixel budget.");
    }
    const size_t pitch = data.rowPitch ? data.rowPitch : rowBytes;
    if (pitch < rowBytes || (desc.height > 1 && pitch > (SIZE_MAX - rowBytes) / (desc.height - 1)) ||
        data.bytes.size() < pitch * (desc.height - 1) + rowBytes)
    {
        throw EngineError("Texture data has an invalid row pitch or insufficient bytes.");
    }
    std::vector<std::byte> pixels(rowBytes * desc.height);
    for (uint32_t y = 0; y < desc.height; ++y)
    {
        std::memcpy(pixels.data() + y * rowBytes, data.bytes.data() + y * pitch, rowBytes);
    }
    m_desc = desc;
    m_pixels = std::move(pixels);
}

std::shared_ptr<Texture> Texture::create(TextureDesc desc, TextureData data)
{
    auto result = std::shared_ptr<Texture>(new Texture());
    result->setPixels(desc, data);
    return result;
}

const TextureDesc& Texture::desc() const
{
    if (!isLoaded())
    {
        throw EngineError("Texture metadata is not ready: " + key());
    }
    return m_desc;
}

uint32_t Texture::mipCount() const
{
    const auto& image = desc();
    uint32_t levels = 1;
    if (image.mipmaps == TextureMipmaps::Generate)
    {
        for (auto size = std::max(image.width, image.height); size > 1; size /= 2)
        {
            ++levels;
        }
    }
    return levels;
}

TextureView Texture::defaultView()
{
    return createView({});
}

TextureView Texture::createView(TextureViewDesc desc)
{
    const auto levels = mipCount();
    if (desc.firstMip >= levels)
    {
        throw EngineError("Texture view starts outside the mip chain.");
    }
    if (!desc.mipCount)
    {
        desc.mipCount = levels - desc.firstMip;
    }
    if (desc.mipCount > levels - desc.firstMip)
    {
        throw EngineError("Texture view exceeds the mip chain.");
    }
    return TextureView(shared_from_this(), desc);
}

bool Texture::loadStep()
{
    try
    {
        if (!m_load)
        {
            m_load = std::make_unique<TextureLoad>();
            m_load->start(key());
            return false;
        }
        if (!m_load->poll())
        {
            return false;
        }

        // Header validation precedes decoder allocation; decoding is deliberately one CPU stage.
        const auto encoded = m_load->data();
        const auto size = encoded.size();
        const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(bytes, int(size), &width, &height, &channels) || width <= 0 || height <= 0 ||
            width > 8192 || height > 8192 || uint64_t(width) * height * 4 > maxPixelBytes)
        {
            throw EngineError("Invalid PNG/JPEG header or image exceeds the pixel budget.");
        }
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(bytes, int(size), &width, &height, &channels, 4),
            stbi_image_free
        );
        if (!pixels)
        {
            throw EngineError(std::string("Image decode failed: ") + stbi_failure_reason());
        }
        setPixels(
            {uint32_t(width), uint32_t(height), TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate},
            {std::span(reinterpret_cast<const std::byte*>(pixels.get()), size_t(width) * height * 4)}
        );
        m_load.reset();
        return true;
    } catch (const std::exception& error)
    {
        m_load.reset();
        throw EngineError("Texture " + key() + ": " + error.what());
    }
}
} // namespace ofg
