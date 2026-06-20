// Browser-only helpers for translating webgpu.h enums and string views.
#pragma once

#include <string>

#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#endif

namespace ofg::webgpu {

#ifdef __EMSCRIPTEN__
// Builds a WGPUStringView for static C strings understood by webgpu.h.
[[nodiscard]] WGPUStringView cstring_view(const char* value) noexcept;
// Builds a WGPUStringView that references an existing std::string.
[[nodiscard]] WGPUStringView string_view(const std::string& value) noexcept;
// Copies a WebGPU string view into a C++ string.
[[nodiscard]] std::string string_from_view(WGPUStringView value);

// Converts requestAdapter status enums into stable debug text.
[[nodiscard]] std::string request_adapter_status_name(WGPURequestAdapterStatus status);
// Converts requestDevice status enums into stable debug text.
[[nodiscard]] std::string request_device_status_name(WGPURequestDeviceStatus status);
// Converts device-lost reason enums into stable debug text.
[[nodiscard]] std::string device_lost_reason_name(WGPUDeviceLostReason reason);
// Converts uncaptured WebGPU error types into stable debug text.
[[nodiscard]] std::string error_type_name(WGPUErrorType type);
// Converts surface-texture acquisition status into stable debug text.
[[nodiscard]] std::string surface_texture_status_name(
  WGPUSurfaceGetCurrentTextureStatus status
);

// Combines an operation name, status string, and optional WebGPU message.
[[nodiscard]] std::string failure_message(
  const char* operation,
  const std::string& status,
  WGPUStringView message
);
// Converts texture formats used by the bootstrap smoke into contract names.
[[nodiscard]] std::string texture_format_name(WGPUTextureFormat format);
// Chooses a browser surface format from capabilities using OFG preferences.
[[nodiscard]] WGPUTextureFormat choose_surface_format(
  const WGPUSurfaceCapabilities& capabilities
);
// Extracts the best available adapter name for debug status.
[[nodiscard]] std::string adapter_name_from_info(WGPUAdapter adapter);
#endif

} // namespace ofg::webgpu
