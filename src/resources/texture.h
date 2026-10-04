// CPU image ownership and retaining mip views; loading and GPU preparation have separate lifetimes.
#pragma once

#include "resources/resource.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ofg {
enum class TextureFormat
{
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8UnormSrgb,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGBA32Float,
};
enum class TextureMipmaps
{
    None,
    Generate
};
struct TextureDesc
{
    uint32_t width;
    uint32_t height;
    TextureFormat format;
    TextureMipmaps mipmaps = TextureMipmaps::None;
};
struct TextureData
{
    std::span<const std::byte> bytes;
    size_t rowPitch = 0; // Zero means tightly packed; rows run top to bottom.
};
struct TextureViewDesc
{
    uint32_t firstMip = 0;
    uint32_t mipCount = 0; // Zero selects all remaining levels.
};

// Returns bytes per texel, rejecting unknown format values.
size_t texturePixelSize(TextureFormat format);

class Texture;
class TextureRenderer;
struct TextureGpuData;
struct AssetRead;
class TextureView
{
public:
    // Returns the retained image owner.
    const std::shared_ptr<Texture>& texture() const noexcept { return m_texture; }
    // Returns the resolved range; views can only be created for loaded images.
    const TextureViewDesc& desc() const noexcept { return m_desc; }

private:
    friend class Texture;
    // Retains the image and its already validated range.
    TextureView(std::shared_ptr<Texture> texture, TextureViewDesc desc);
    std::shared_ptr<Texture> m_texture;
    TextureViewDesc m_desc;
};

class Texture : public Resource, public std::enable_shared_from_this<Texture>
{
public:
    // Copies base pixels into tight storage; mipmaps are generated on the GPU on first use.
    static std::shared_ptr<Texture> create(TextureDesc desc, TextureData data);
    // Starts a pending PNG/JPEG load; Resources advances I/O and decoding on update().
    explicit Texture(std::string path);
    // Cancels pending I/O and releases CPU and private GPU state.
    ~Texture() override;
    // Returns metadata after successful CPU loading; throws otherwise.
    const TextureDesc& desc() const;
    // Returns the total level count, including levels to be generated on the GPU.
    uint32_t mipCount() const;
    // Retains this loaded texture and selects every mip.
    TextureView defaultView();
    // Retains this loaded texture and selects a validated range; no format reinterpretation.
    TextureView createView(TextureViewDesc desc);

private:
    friend class TextureRenderer;
    // Constructs an uncached, ready procedural texture.
    Texture();
    // Advances one I/O stage or decodes a completed image; reports failures with its key.
    bool loadStep() override;
    // Validates and copies base rows, with a 256 MiB decoded-image budget.
    void setPixels(TextureDesc desc, TextureData data);
    TextureDesc m_desc{};
    std::vector<std::byte> m_pixels;
    std::unique_ptr<AssetRead> m_load;
    std::shared_ptr<TextureGpuData> m_gpu;
};
} // namespace ofg
