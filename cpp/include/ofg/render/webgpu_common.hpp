// Small WebGPU helpers shared by browser Emdawnwebgpu and native Dawn paths.
//
// These helpers normalize the few pieces of WebGPU C API ceremony used outside
// browser-only code: string-view construction, string-view conversion, and enum
// labels for reports/status. They intentionally do not own devices, surfaces, or
// renderer state.
#pragma once

#include <string>

#ifdef OFG_ENABLE_WEBGPU_RENDERER
#include <webgpu/webgpu.h>
#endif

namespace ofg::gpu {

#ifdef OFG_ENABLE_WEBGPU_RENDERER
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
#endif

} // namespace ofg::gpu
