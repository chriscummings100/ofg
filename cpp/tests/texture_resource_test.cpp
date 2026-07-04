// Doctest coverage for CPU-side OFG texture resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/texture.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Builds a byte vector from RGBA8 channel values.
std::vector<std::byte> rgba_bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    for (std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Reads one little-endian u16 value from a byte vector.
std::uint16_t read_u16_le(const std::vector<std::byte>& bytes, std::size_t index) {
    const std::size_t byte_index = index * 2U;
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[byte_index + 0U])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[byte_index + 1U]) << 8U);
}

} // namespace

// Verifies texture format and pixel validation.
TEST_CASE("texture resource validates RGBA8 construction") {
    ofg::Texture bad_texture{ofg::GpuContext{}, "bad"};
    try {
        bad_texture.init_from_rgba8_pixels(
            2, 2, ofg::TextureColorSpace::Linear, rgba_bytes({0, 1, 2, 3}), ofg::MipMapPolicy::None);
        FAIL("Expected texture init with the wrong byte count to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("byte count") != std::string::npos);
    }

    ofg::Texture texture{ofg::GpuContext{}, "ok"};
    texture.init_from_rgba8_pixels(
        1, 1, ofg::TextureColorSpace::Srgb, rgba_bytes({255, 0, 0, 255}), ofg::MipMapPolicy::None);
    CHECK(texture.label() == "ok");
    CHECK(texture.width() == 1);
    CHECK(texture.height() == 1);
    CHECK(texture.pixel_format() == ofg::TexturePixelFormat::Rgba8Srgb);
    CHECK(texture.mip_map_policy() == ofg::MipMapPolicy::None);
    CHECK(texture.mip_level_count() == 1);
    CHECK(texture.view() == nullptr);
    CHECK(texture.sampler() == nullptr);
}

// Verifies texture resource rejects invalid labels and dimensions.
TEST_CASE("texture resource rejects invalid identity and dimensions") {
    try {
        ofg::Texture texture{ofg::GpuContext{}, ""};
        (void)texture;
        FAIL("Expected empty texture label to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }

    ofg::Texture texture{ofg::GpuContext{}, "zero"};
    try {
        texture.init_from_rgba8_pixels(0, 1, ofg::TextureColorSpace::Linear, {}, ofg::MipMapPolicy::None);
        FAIL("Expected zero texture dimensions to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("dimensions") != std::string::npos);
    }
}

// Verifies full CPU mip generation is deterministic.
TEST_CASE("texture resource generates full CPU mip chain") {
    ofg::Texture texture{ofg::GpuContext{}, "checker"};
    texture.init_from_rgba8_pixels(2,
        2,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255, 255, 0, 0, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain);
    CHECK(texture.mip_level_count() == 2);
    CHECK(ofg::full_mip_level_count(2, 2) == 2);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(1)[0]) == 138);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(1)[3]) == 255);

    texture.update_pixels(rgba_bytes({10, 0, 0, 255, 10, 0, 0, 255, 10, 0, 0, 255, 10, 0, 0, 255}));
    CHECK(texture.revision() == 2);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(1)[0]) == 10);
    try {
        texture.update_pixels(rgba_bytes({10, 0, 0, 255}));
        FAIL("Expected texture update with the wrong byte count to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("byte count") != std::string::npos);
    }
    CHECK(ofg::full_mip_level_count(0, 2) == 0);
    CHECK(ofg::full_mip_level_count(4, 2) == 3);
}

// Verifies odd-sized textures keep every expected full-chain mip level.
TEST_CASE("texture resource generates ceil-halved odd mip levels") {
    ofg::Texture texture{ofg::GpuContext{}, "odd"};
    texture.init_from_rgba8_pixels(3,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain);
    CHECK(texture.mip_level_count() == 3);
    CHECK(texture.pixels(1).size() == 8);
    CHECK(texture.pixels(2).size() == 4);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(1)[0]) == 50);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(1)[4]) == 200);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(2)[0]) == 125);
    CHECK(ofg::full_mip_level_count(3, 1) == 3);
}

// Verifies the tested float-to-binary16 conversion used by terrain height debug textures.
TEST_CASE("texture resource converts and packs R16Float pixels") {
    CHECK(ofg::texture_pixel_format_bytes_per_pixel(ofg::TexturePixelFormat::R16Float) == 2);
    CHECK(ofg::float_to_r16_float_bits(0.0f) == 0x0000U);
    CHECK(ofg::float_to_r16_float_bits(1.0f) == 0x3c00U);
    CHECK(ofg::float_to_r16_float_bits(0.5f) == 0x3800U);
    CHECK(ofg::float_to_r16_float_bits(-2.0f) == 0xc000U);
    CHECK(ofg::float_to_r16_float_bits(std::numeric_limits<float>::infinity()) == 0x7c00U);
    CHECK(std::isnan(
        ofg::r16_float_bits_to_float(ofg::float_to_r16_float_bits(std::numeric_limits<float>::quiet_NaN()))));

    const std::array<float, 4> values{0.0f, 1.0f, -2.0f, 0.5f};
    const std::vector<std::byte> pixels = ofg::pack_r16_float_pixels(values);

    CHECK(pixels.size() == values.size() * 2U);
    CHECK(read_u16_le(pixels, 0) == 0x0000U);
    CHECK(read_u16_le(pixels, 1) == 0x3c00U);
    CHECK(read_u16_le(pixels, 2) == 0xc000U);
    CHECK(read_u16_le(pixels, 3) == 0x3800U);
    CHECK(ofg::r16_float_bits_to_float(read_u16_le(pixels, 1)) == doctest::Approx(1.0f));
}

// Verifies R16Float textures keep the half-float data path narrow and mip-free.
TEST_CASE("texture resource stores R16Float pixels without mip generation") {
    ofg::Texture texture{ofg::GpuContext{}, "terrain heights"};
    std::array<float, 4> values{0.0f, 1.0f, -1.0f, 4.0f};
    texture.init_from_r16_float_pixels(2, 2, ofg::pack_r16_float_pixels(values));

    CHECK(texture.width() == 2);
    CHECK(texture.height() == 2);
    CHECK(texture.pixel_format() == ofg::TexturePixelFormat::R16Float);
    CHECK(texture.mip_map_policy() == ofg::MipMapPolicy::None);
    CHECK(texture.mip_level_count() == 1);
    CHECK(texture.pixels(0).size() == 8);
    CHECK(texture.texture() == nullptr);
    CHECK(texture.view() == nullptr);
    CHECK(texture.sampler() == nullptr);
    CHECK(texture.revision() == 1);

    values = std::array<float, 4>{2.0f, 3.0f, 4.0f, 5.0f};
    texture.update_pixels(ofg::pack_r16_float_pixels(values));
    CHECK(texture.revision() == 2);
    CHECK(std::to_integer<std::uint8_t>(texture.pixels(0)[0]) ==
          static_cast<std::uint8_t>(ofg::float_to_r16_float_bits(2.0f) & 0xffU));

    CHECK_THROWS_WITH_AS(([&]() { texture.update_pixels(rgba_bytes({0, 1, 2, 3})); }()),
        doctest::Contains("byte count"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Texture bad{ofg::GpuContext{}, "bad r16"};
        bad.init_from_r16_float_pixels(2, 2, rgba_bytes({0, 1, 2, 3}));
    }()),
        doctest::Contains("R16Float"),
        ofg::EngineError);
}

// Verifies texture resources are address-stable Object-derived values.
TEST_CASE("texture resource is not movable") {
    CHECK_FALSE(std::is_move_constructible_v<ofg::Texture>);
    CHECK_FALSE(std::is_move_assignable_v<ofg::Texture>);
}
