// Doctest coverage for CPU-side OFG texture resources.
#include "doctest.h"

#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
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
    std::string error;
    CHECK(ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
              "bad",
              2,
              2,
              ofg::TextureColorSpace::Linear,
              rgba_bytes({0, 1, 2, 3}),
              ofg::MipMapPolicy::None,
              error)
              .has_value() == false);
    CHECK(error.find("byte count") != std::string::npos);

    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "ok",
        1,
        1,
        ofg::TextureColorSpace::Srgb,
        rgba_bytes({255, 0, 0, 255}),
        ofg::MipMapPolicy::None,
        error);
    REQUIRE(texture.has_value());
    CHECK(texture->label() == "ok");
    CHECK(texture->width() == 1);
    CHECK(texture->height() == 1);
    CHECK(texture->pixel_format() == ofg::TexturePixelFormat::Rgba8Srgb);
    CHECK(texture->mip_map_policy() == ofg::MipMapPolicy::None);
    CHECK(texture->mip_level_count() == 1);
    CHECK(texture->view() == nullptr);
    CHECK(texture->sampler() == nullptr);
}

// Verifies texture resource rejects invalid labels and dimensions.
TEST_CASE("texture resource rejects invalid identity and dimensions") {
    std::string error;
    CHECK(ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
              "",
              1,
              1,
              ofg::TextureColorSpace::Linear,
              rgba_bytes({255, 255, 255, 255}),
              ofg::MipMapPolicy::None,
              error)
              .has_value() == false);
    CHECK(error.find("label") != std::string::npos);

    CHECK(ofg::Texture::from_rgba8_pixels(
              ofg::GpuContext{}, "zero", 0, 1, ofg::TextureColorSpace::Linear, {}, ofg::MipMapPolicy::None, error)
              .has_value() == false);
    CHECK(error.find("dimensions") != std::string::npos);
}

// Verifies full CPU mip generation is deterministic.
TEST_CASE("texture resource generates full CPU mip chain") {
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "checker",
        2,
        2,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255, 255, 0, 0, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain,
        error);
    REQUIRE(texture.has_value());
    CHECK(texture->mip_level_count() == 2);
    CHECK(ofg::full_mip_level_count(2, 2) == 2);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[0]) == 138);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[3]) == 255);

    REQUIRE(texture->update_pixels(rgba_bytes({10, 0, 0, 255, 10, 0, 0, 255, 10, 0, 0, 255, 10, 0, 0, 255}), error));
    CHECK(texture->revision() == 2);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[0]) == 10);
    CHECK(texture->update_pixels(rgba_bytes({10, 0, 0, 255}), error) == false);
    CHECK(error.find("byte count") != std::string::npos);
    CHECK(ofg::full_mip_level_count(0, 2) == 0);
    CHECK(ofg::full_mip_level_count(4, 2) == 3);
}

// Verifies odd-sized textures keep every expected full-chain mip level.
TEST_CASE("texture resource generates ceil-halved odd mip levels") {
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "odd",
        3,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({0, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain,
        error);
    REQUIRE(texture.has_value());
    CHECK(texture->mip_level_count() == 3);
    CHECK(texture->pixels(1).size() == 8);
    CHECK(texture->pixels(2).size() == 4);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[0]) == 50);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(1)[4]) == 200);
    CHECK(std::to_integer<std::uint8_t>(texture->pixels(2)[0]) == 125);
    CHECK(ofg::full_mip_level_count(3, 1) == 3);
}

// Verifies texture move assignment transfers CPU data and empty GPU handles.
TEST_CASE("texture resource supports move assignment") {
    std::string error;
    std::optional<ofg::Texture> destination = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "destination",
        1,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({1, 2, 3, 4}),
        ofg::MipMapPolicy::None,
        error);
    std::optional<ofg::Texture> source = ofg::Texture::from_rgba8_pixels(ofg::GpuContext{},
        "source",
        1,
        1,
        ofg::TextureColorSpace::Srgb,
        rgba_bytes({5, 6, 7, 8}),
        ofg::MipMapPolicy::None,
        error);
    REQUIRE(destination.has_value());
    REQUIRE(source.has_value());

    *destination = std::move(*source);
    CHECK(destination->label() == "source");
    CHECK(destination->pixel_format() == ofg::TexturePixelFormat::Rgba8Srgb);
    CHECK(std::to_integer<std::uint8_t>(destination->pixels(0)[0]) == 5);
    CHECK(destination->view() == nullptr);
    CHECK(destination->sampler() == nullptr);
}
