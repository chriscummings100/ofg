// Small GPU helpers shared by browser Emdawnwebgpu and native Dawn paths.
//
// OFG is WebGPU-only, so this module keeps common WebGPU C API ceremony in one
// place without pretending to wrap every graphics object. It provides string
// views, enum labels, and reusable target helpers for systems that already own
// their devices, surfaces, or render state.
#pragma once

#include <cstdint>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg::gpu {

// Builds a null-terminated WebGPU string view from a string literal.
[[nodiscard]] WGPUStringView cstring_view(const char* value) noexcept;

// Builds a bounded WebGPU string view from a live std::string.
[[nodiscard]] WGPUStringView string_view(const std::string& value) noexcept;

// Copies a WebGPU string view into a standard string.
[[nodiscard]] std::string string_from_view(WGPUStringView value);

// Converts a texture format into the public OFG status/report label.
[[nodiscard]] std::string texture_format_name(WGPUTextureFormat format);

// Converts a native Dawn backend type into the smoke report label.
[[nodiscard]] std::string backend_type_name(WGPUBackendType backend);

// Creates a 2D depth texture for render attachment use.
[[nodiscard]] WGPUTexture create_depth_texture(
    WGPUDevice device, WGPUTextureFormat depth_format, std::uint32_t width, std::uint32_t height, const char* label);

// Creates the default 2D view for a depth texture.
[[nodiscard]] WGPUTextureView create_depth_view(WGPUTexture texture, WGPUTextureFormat depth_format, const char* label);

} // namespace ofg::gpu
