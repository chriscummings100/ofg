// Mutable texture resource for generated or caller-provided RGBA8 pixels.
#include "ofg/resources/texture.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Returns the byte count for one tightly packed RGBA8 level.
std::optional<std::size_t> rgba8_byte_count(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
}

// Converts the resource pixel format into a WebGPU texture format.
WGPUTextureFormat webgpu_texture_format(TexturePixelFormat pixel_format) noexcept {
    return pixel_format == TexturePixelFormat::Rgba8Srgb ? WGPUTextureFormat_RGBA8UnormSrgb
                                                         : WGPUTextureFormat_RGBA8Unorm;
}

// Returns the extent of one mip level.
std::uint32_t mip_extent(std::uint32_t base_extent, std::uint32_t mip_level) noexcept {
    for (std::uint32_t level = 0; level < mip_level; ++level) {
        base_extent = std::max(1U, (base_extent + 1U) / 2U);
    }
    return base_extent;
}

// Reads one RGBA8 channel from a byte vector.
std::uint8_t pixel_channel(const std::vector<std::byte>& pixels,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t channel) {
    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U + channel;
    return std::to_integer<std::uint8_t>(pixels[index]);
}

// Writes one RGBA8 channel into a byte vector.
void set_pixel_channel(std::vector<std::byte>& pixels,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t channel,
    std::uint8_t value) {
    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U + channel;
    pixels[index] = static_cast<std::byte>(value);
}

// Downsamples one RGBA8 mip level to the next using box filtering.
std::vector<std::byte> downsample_rgba8(
    const std::vector<std::byte>& source, std::uint32_t width, std::uint32_t height) {
    const std::uint32_t next_width = std::max(1U, (width + 1U) / 2U);
    const std::uint32_t next_height = std::max(1U, (height + 1U) / 2U);
    std::vector<std::byte> result(static_cast<std::size_t>(next_width) * next_height * 4U);

    for (std::uint32_t y = 0; y < next_height; ++y) {
        for (std::uint32_t x = 0; x < next_width; ++x) {
            for (std::uint32_t channel = 0; channel < 4; ++channel) {
                std::uint32_t sum = 0;
                std::uint32_t count = 0;
                for (std::uint32_t dy = 0; dy < 2; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2; ++dx) {
                        const std::uint32_t sx = std::min(width - 1U, x * 2U + dx);
                        const std::uint32_t sy = std::min(height - 1U, y * 2U + dy);
                        sum += pixel_channel(source, width, sx, sy, channel);
                        count += 1;
                    }
                }
                set_pixel_channel(result, next_width, x, y, channel, static_cast<std::uint8_t>(sum / count));
            }
        }
    }
    return result;
}

// Generates the requested CPU mip chain.
std::vector<std::vector<std::byte>> generate_mip_chain(
    std::uint32_t width, std::uint32_t height, std::vector<std::byte> level_zero, MipMapPolicy policy) {
    std::vector<std::vector<std::byte>> mip_pixels;
    mip_pixels.push_back(std::move(level_zero));
    if (policy == MipMapPolicy::None) {
        return mip_pixels;
    }

    std::uint32_t current_width = width;
    std::uint32_t current_height = height;
    while (current_width > 1U || current_height > 1U) {
        std::vector<std::byte> next = downsample_rgba8(mip_pixels.back(), current_width, current_height);
        mip_pixels.push_back(std::move(next));
        current_width = std::max(1U, (current_width + 1U) / 2U);
        current_height = std::max(1U, (current_height + 1U) / 2U);
    }
    return mip_pixels;
}

} // namespace

// Allocates a labeled texture resource using the creating Resources context.
Texture::Texture(GpuContext gpu, std::string label) : m_gpu(std::move(gpu)), m_label(std::move(label)) {
    if (m_label.empty()) {
        throw EngineError("Texture label must not be empty.");
    }
}

// Releases owned GPU texture state.
Texture::~Texture() {
    release_gpu_state();
}

// Initializes this texture from tightly packed RGBA8 level-zero pixels.
void Texture::init_from_rgba8_pixels(std::uint32_t width,
    std::uint32_t height,
    TextureColorSpace color_space,
    std::vector<std::byte> pixels,
    MipMapPolicy mip_map_policy) {
    const std::optional<std::size_t> expected_bytes = rgba8_byte_count(width, height);
    if (!expected_bytes.has_value()) {
        throw EngineError("Texture dimensions must be nonzero.");
    }
    if (pixels.size() != *expected_bytes) {
        throw EngineError("Texture RGBA8 pixel byte count does not match width and height.");
    }

    std::vector<std::vector<std::byte>> mip_pixels =
        generate_mip_chain(width, height, std::move(pixels), mip_map_policy);
    release_gpu_state();
    m_width = width;
    m_height = height;
    m_pixel_format = texture_pixel_format_for_color_space(color_space);
    m_mip_map_policy = mip_map_policy;
    m_mip_pixels = std::move(mip_pixels);
    prepare_gpu_state();
    m_revision += 1;
}

// Replaces level-zero pixels and regenerates CPU mips.
void Texture::update_pixels(std::vector<std::byte> pixels) {
    const std::optional<std::size_t> expected_bytes = rgba8_byte_count(m_width, m_height);
    if (!expected_bytes.has_value() || pixels.size() != *expected_bytes) {
        throw EngineError("Texture RGBA8 pixel byte count does not match existing dimensions.");
    }
    m_mip_pixels = generate_mip_chain(m_width, m_height, std::move(pixels), m_mip_map_policy);
    upload_mip_chain();
    m_revision += 1;
}

// Returns the texture label.
const std::string& Texture::label() const noexcept {
    return m_label;
}

// Returns the texture width in pixels.
std::uint32_t Texture::width() const noexcept {
    return m_width;
}

// Returns the texture height in pixels.
std::uint32_t Texture::height() const noexcept {
    return m_height;
}

// Returns the selected pixel format.
TexturePixelFormat Texture::pixel_format() const noexcept {
    return m_pixel_format;
}

// Returns the requested mip-map policy.
MipMapPolicy Texture::mip_map_policy() const noexcept {
    return m_mip_map_policy;
}

// Returns the stored mip level count.
std::uint32_t Texture::mip_level_count() const noexcept {
    return static_cast<std::uint32_t>(m_mip_pixels.size());
}

// Returns CPU pixels for one mip level.
std::span<const std::byte> Texture::pixels(std::uint32_t mip_level) const {
    return m_mip_pixels.at(mip_level);
}

// Returns the WebGPU texture, null for CPU-only resources.
WGPUTexture Texture::texture() const noexcept {
    return m_texture;
}

// Returns the WebGPU texture view, null for CPU-only resources.
WGPUTextureView Texture::view() const noexcept {
    return m_view;
}

// Returns the WebGPU sampler, null for CPU-only resources.
WGPUSampler Texture::sampler() const noexcept {
    return m_sampler;
}

// Returns the current texture revision.
std::uint64_t Texture::revision() const noexcept {
    return m_revision;
}

// Converts a color-space request into the concrete RGBA8 texture format.
TexturePixelFormat texture_pixel_format_for_color_space(TextureColorSpace color_space) noexcept {
    return color_space == TextureColorSpace::Srgb ? TexturePixelFormat::Rgba8Srgb : TexturePixelFormat::Rgba8;
}

// Computes the number of mip levels needed by a full mip chain.
std::uint32_t full_mip_level_count(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0 || height == 0) {
        return 0;
    }
    std::uint32_t levels = 1;
    while (width > 1U || height > 1U) {
        width = std::max(1U, (width + 1U) / 2U);
        height = std::max(1U, (height + 1U) / 2U);
        levels += 1;
    }
    return levels;
}

// Creates GPU texture/view/sampler handles and uploads every mip level.
void Texture::prepare_gpu_state() {
    if (gpu_context_is_empty(m_gpu)) {
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Texture GPU preparation requires a WebGPU device and queue.");
    }

    release_gpu_state();

    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.label = gpu::string_view(m_label);
    texture_descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = WGPUExtent3D{m_width, m_height, 1};
    texture_descriptor.format = webgpu_texture_format(m_pixel_format);
    texture_descriptor.mipLevelCount = mip_level_count();
    texture_descriptor.sampleCount = 1;
    m_texture = wgpuDeviceCreateTexture(m_gpu.m_device, &texture_descriptor);
    if (m_texture == nullptr) {
        throw EngineError("wgpuDeviceCreateTexture returned null for texture '" + m_label + "'.");
    }

    try {
        upload_mip_chain();
    } catch (...) {
        release_gpu_state();
        throw;
    }

    WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_descriptor.label = gpu::string_view(m_label);
    view_descriptor.format = webgpu_texture_format(m_pixel_format);
    view_descriptor.dimension = WGPUTextureViewDimension_2D;
    view_descriptor.mipLevelCount = mip_level_count();
    view_descriptor.arrayLayerCount = 1;
    view_descriptor.aspect = WGPUTextureAspect_All;
    m_view = wgpuTextureCreateView(m_texture, &view_descriptor);
    if (m_view == nullptr) {
        release_gpu_state();
        throw EngineError("wgpuTextureCreateView returned null for texture '" + m_label + "'.");
    }

    WGPUSamplerDescriptor sampler_descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_descriptor.label = gpu::string_view(m_label);
    sampler_descriptor.addressModeU = WGPUAddressMode_Repeat;
    sampler_descriptor.addressModeV = WGPUAddressMode_Repeat;
    sampler_descriptor.addressModeW = WGPUAddressMode_Repeat;
    sampler_descriptor.magFilter = WGPUFilterMode_Linear;
    sampler_descriptor.minFilter = WGPUFilterMode_Linear;
    sampler_descriptor.mipmapFilter = m_mip_map_policy == MipMapPolicy::GenerateCpuFullChain
                                          ? WGPUMipmapFilterMode_Linear
                                          : WGPUMipmapFilterMode_Nearest;
    m_sampler = wgpuDeviceCreateSampler(m_gpu.m_device, &sampler_descriptor);
    if (m_sampler == nullptr) {
        release_gpu_state();
        throw EngineError("wgpuDeviceCreateSampler returned null for texture '" + m_label + "'.");
    }
}

// Uploads all stored CPU mip levels into the current WebGPU texture.
void Texture::upload_mip_chain() const {
    if (gpu_context_is_empty(m_gpu)) {
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Texture upload requires a WebGPU device and queue.");
    }
    if (m_texture == nullptr) {
        throw EngineError("Texture upload requires an initialized WebGPU texture.");
    }

    for (std::uint32_t level = 0; level < mip_level_count(); ++level) {
        const std::uint32_t level_width = mip_extent(m_width, level);
        const std::uint32_t level_height = mip_extent(m_height, level);
        const std::vector<std::byte>& level_pixels = m_mip_pixels[level];

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = m_texture;
        destination.mipLevel = level;
        destination.origin = WGPUOrigin3D{0, 0, 0};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = level_width * 4U;
        layout.rowsPerImage = level_height;

        const WGPUExtent3D write_size{level_width, level_height, 1};
        wgpuQueueWriteTexture(
            m_gpu.m_queue, &destination, level_pixels.data(), level_pixels.size(), &layout, &write_size);
    }
}

// Releases all owned WebGPU texture handles.
void Texture::release_gpu_state() noexcept {
    if (m_sampler != nullptr) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_view != nullptr) {
        wgpuTextureViewRelease(m_view);
        m_view = nullptr;
    }
    if (m_texture != nullptr) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
    }
}

} // namespace ofg
