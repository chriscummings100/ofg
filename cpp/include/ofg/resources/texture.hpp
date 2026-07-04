// Mutable texture resource for generated or caller-provided pixels.
//
// Textures own CPU pixels plus deterministic mip chains where supported, and
// they eagerly prepare WebGPU texture/view/sampler state when created with a
// ready GpuContext. Terrain height debug textures use the narrow R16Float path.
#pragma once

#include "ofg/core/object.hpp"
#include "ofg/game/gpu_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

enum class TextureColorSpace {
    Srgb,
    Linear,
};

enum class TexturePixelFormat {
    Rgba8,
    Rgba8Srgb,
    R16Float,
};

enum class MipMapPolicy {
    None,
    GenerateCpuFullChain,
};

class Texture : public Object {
public:
    // Allocates a labeled texture resource using the creating Resources context.
    Texture(GpuContext gpu, std::string label);
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) = delete;
    Texture& operator=(Texture&& other) = delete;
    ~Texture() override;

    // Initializes this texture from tightly packed RGBA8 level-zero pixels.
    void init_from_rgba8_pixels(std::uint32_t width,
        std::uint32_t height,
        TextureColorSpace color_space,
        std::vector<std::byte> pixels,
        MipMapPolicy mip_map_policy);
    // Initializes this texture from tightly packed little-endian R16Float pixels.
    void init_from_r16_float_pixels(std::uint32_t width, std::uint32_t height, std::vector<std::byte> pixels);

    // Replaces level-zero pixels and regenerates CPU mips.
    void update_pixels(std::vector<std::byte> pixels);
    // Returns the texture label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns the texture width in pixels.
    [[nodiscard]] std::uint32_t width() const noexcept;
    // Returns the texture height in pixels.
    [[nodiscard]] std::uint32_t height() const noexcept;
    // Returns the selected pixel format.
    [[nodiscard]] TexturePixelFormat pixel_format() const noexcept;
    // Returns the requested mip-map policy.
    [[nodiscard]] MipMapPolicy mip_map_policy() const noexcept;
    // Returns the stored mip level count.
    [[nodiscard]] std::uint32_t mip_level_count() const noexcept;
    // Returns CPU pixels for one mip level.
    [[nodiscard]] std::span<const std::byte> pixels(std::uint32_t mip_level) const;
    // Returns the WebGPU texture, null for CPU-only resources.
    [[nodiscard]] WGPUTexture texture() const noexcept;
    // Returns the WebGPU texture view, null for CPU-only resources.
    [[nodiscard]] WGPUTextureView view() const noexcept;
    // Returns the WebGPU sampler, null for CPU-only resources.
    [[nodiscard]] WGPUSampler sampler() const noexcept;
    // Returns the current texture revision.
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    // Creates GPU texture/view/sampler handles and uploads every mip level.
    void prepare_gpu_state();
    // Uploads all stored CPU mip levels into the current WebGPU texture.
    void upload_mip_chain() const;
    // Releases all owned WebGPU texture handles.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    std::string m_label;
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    TexturePixelFormat m_pixel_format{TexturePixelFormat::Rgba8};
    MipMapPolicy m_mip_map_policy{MipMapPolicy::None};
    std::vector<std::vector<std::byte>> m_mip_pixels;
    WGPUTexture m_texture{nullptr};
    WGPUTextureView m_view{nullptr};
    WGPUSampler m_sampler{nullptr};
    std::uint64_t m_revision{0};
};

// Converts a color-space request into the concrete RGBA8 texture format.
[[nodiscard]] TexturePixelFormat texture_pixel_format_for_color_space(TextureColorSpace color_space) noexcept;

// Returns the byte width of one texel in a supported texture format.
[[nodiscard]] std::size_t texture_pixel_format_bytes_per_pixel(TexturePixelFormat pixel_format) noexcept;

// Converts a finite or non-finite float into IEEE 754 binary16 bits for R16Float textures.
[[nodiscard]] std::uint16_t float_to_r16_float_bits(float value) noexcept;

// Converts IEEE 754 binary16 bits back to a float for tests and diagnostics.
[[nodiscard]] float r16_float_bits_to_float(std::uint16_t bits) noexcept;

// Packs float values into little-endian binary16 texel bytes.
[[nodiscard]] std::vector<std::byte> pack_r16_float_pixels(std::span<const float> values);

// Computes the number of mip levels needed by a full mip chain.
[[nodiscard]] std::uint32_t full_mip_level_count(std::uint32_t width, std::uint32_t height) noexcept;

} // namespace ofg
