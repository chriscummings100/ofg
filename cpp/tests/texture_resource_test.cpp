// Doctest coverage for CPU-side OFG texture resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
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

// Verifies texture move assignment transfers CPU data and empty GPU handles.
TEST_CASE("texture resource supports move assignment") {
    ofg::Texture destination{ofg::GpuContext{}, "destination"};
    destination.init_from_rgba8_pixels(
        1, 1, ofg::TextureColorSpace::Linear, rgba_bytes({1, 2, 3, 4}), ofg::MipMapPolicy::None);
    ofg::Texture source{ofg::GpuContext{}, "source"};
    source.init_from_rgba8_pixels(
        1, 1, ofg::TextureColorSpace::Srgb, rgba_bytes({5, 6, 7, 8}), ofg::MipMapPolicy::None);

    destination = std::move(source);
    CHECK(destination.label() == "source");
    CHECK(destination.pixel_format() == ofg::TexturePixelFormat::Rgba8Srgb);
    CHECK(std::to_integer<std::uint8_t>(destination.pixels(0)[0]) == 5);
    CHECK(destination.view() == nullptr);
    CHECK(destination.sampler() == nullptr);
}
