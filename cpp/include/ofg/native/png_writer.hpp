// Minimal PNG writer for native render-smoke RGBA8 output.
//
// The native smoke harness needs a real image artifact without adding another
// native dependency. This module writes simple RGBA8 PNGs with uncompressed
// zlib blocks, which is enough for deterministic screenshots and easy to audit.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace ofg::native {

// Writes tightly packed RGBA8 pixels to a PNG at `path`.
void write_rgba_png(
    const std::filesystem::path& path, std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

} // namespace ofg::native
