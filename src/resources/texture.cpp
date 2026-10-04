// Pixel-layout validation, explicit mip ranges, and cancellable platform image loading.
#include "resources/texture.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cstring>
#include "resources/asset-read.h"
#include "resources/image-decoder.h"

namespace ofg {
namespace {
constexpr size_t maxPixelBytes = 256 * 1024 * 1024;
} // namespace

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
            m_load = std::make_unique<AssetRead>();
            m_load->start(key());
            return false;
        }
        if (!m_load->poll())
        {
            return false;
        }

        const auto image = decodeImage(m_load->data());
        setPixels({image.width, image.height, TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate}, {image.pixels});
        m_load.reset();
        return true;
    } catch (const std::exception& error)
    {
        m_load.reset();
        throw EngineError("Texture " + key() + ": " + error.what());
    }
}
} // namespace ofg
