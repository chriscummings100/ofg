// Minimal PNG writer for native render-smoke RGBA8 output.
//
// This file intentionally implements only the PNG subset OFG's smoke test
// needs: 8-bit RGBA images, filter type 0, and uncompressed zlib blocks. Keeping
// it local avoids a native image dependency in the Dawn smoke executable.
#include "ofg/native/png_writer.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ofg::native {
namespace {

constexpr std::uint32_t kBytesPerPixel = 4;

// Appends a 32-bit integer in PNG/network byte order.
void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

// Appends a 16-bit integer in the little-endian order used by deflate blocks.
void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

// Computes the CRC used by PNG chunks.
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

// Computes the Adler-32 checksum required at the end of a zlib stream.
std::uint32_t adler32(std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t modulus = 65521U;
  std::uint32_t a = 1U;
  std::uint32_t b = 0U;
  for (const std::uint8_t byte : bytes) {
    a = (a + byte) % modulus;
    b = (b + a) % modulus;
  }
  return (b << 16U) | a;
}

// Appends one complete PNG chunk including length, type, data, and CRC.
void append_chunk(
  std::vector<std::uint8_t>& png,
  const std::array<char, 4>& type,
  std::span<const std::uint8_t> data
) {
  append_u32_be(png, static_cast<std::uint32_t>(data.size()));
  const std::size_t crc_start = png.size();
  for (const char item : type) {
    png.push_back(static_cast<std::uint8_t>(item));
  }
  png.insert(png.end(), data.begin(), data.end());
  append_u32_be(
    png,
    crc32(std::span<const std::uint8_t>{png.data() + crc_start, 4 + data.size()})
  );
}

// Converts tightly packed RGBA pixels into PNG scanlines with filter bytes.
std::vector<std::uint8_t> build_scanlines(
  std::span<const std::uint8_t> rgba,
  std::uint32_t width,
  std::uint32_t height
) {
  const std::size_t bytes_per_row =
    static_cast<std::size_t>(width) * kBytesPerPixel;
  const std::size_t expected = bytes_per_row * height;
  if (rgba.size() != expected) {
    throw std::runtime_error(
      "Expected " + std::to_string(expected) +
      " RGBA bytes, got " + std::to_string(rgba.size()) + "."
    );
  }

  std::vector<std::uint8_t> scanlines;
  scanlines.reserve((bytes_per_row + 1U) * height);
  for (std::uint32_t row = 0; row < height; ++row) {
    scanlines.push_back(0);
    const std::size_t offset = static_cast<std::size_t>(row) * bytes_per_row;
    scanlines.insert(
      scanlines.end(),
      rgba.begin() + static_cast<std::ptrdiff_t>(offset),
      rgba.begin() + static_cast<std::ptrdiff_t>(offset + bytes_per_row)
    );
  }
  return scanlines;
}

// Builds a zlib stream using stored deflate blocks, avoiding compression code.
std::vector<std::uint8_t> build_zlib_stored_stream(
  std::span<const std::uint8_t> bytes
) {
  std::vector<std::uint8_t> stream;
  stream.reserve(bytes.size() + (bytes.size() / 65535U + 1U) * 5U + 6U);
  stream.push_back(0x78);
  stream.push_back(0x01);

  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const std::uint16_t block_size =
      static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535U));
    const bool final_block = offset + block_size == bytes.size();
    stream.push_back(final_block ? 0x01 : 0x00);
    append_u16_le(stream, block_size);
    append_u16_le(stream, static_cast<std::uint16_t>(~block_size));
    stream.insert(
      stream.end(),
      bytes.begin() + static_cast<std::ptrdiff_t>(offset),
      bytes.begin() + static_cast<std::ptrdiff_t>(offset + block_size)
    );
    offset += block_size;
  }

  append_u32_be(stream, adler32(bytes));
  return stream;
}

} // namespace

// Writes a valid RGBA PNG file for the native render smoke artifact.
void write_rgba_png(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> rgba,
  std::uint32_t width,
  std::uint32_t height
) {
  if (width == 0 || height == 0) {
    throw std::runtime_error("PNG dimensions must be non-zero.");
  }

  std::vector<std::uint8_t> png{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
  };

  std::vector<std::uint8_t> ihdr;
  ihdr.reserve(13);
  append_u32_be(ihdr, width);
  append_u32_be(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  append_chunk(png, {'I', 'H', 'D', 'R'}, ihdr);

  const std::vector<std::uint8_t> scanlines = build_scanlines(rgba, width, height);
  const std::vector<std::uint8_t> idat = build_zlib_stored_stream(scanlines);
  append_chunk(png, {'I', 'D', 'A', 'T'}, idat);
  append_chunk(png, {'I', 'E', 'N', 'D'}, {});

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PNG path for writing: " + path.string());
  }
  file.write(
    reinterpret_cast<const char*>(png.data()),
    static_cast<std::streamsize>(png.size())
  );
  if (!file) {
    throw std::runtime_error("Failed while writing PNG: " + path.string());
  }
}

} // namespace ofg::native
