// Browser-only helpers for keeping BrowserGame focused on lifecycle ownership.
#include "ofg/web/webgpu_utils.hpp"

#ifdef __EMSCRIPTEN__

#include <array>

namespace ofg::webgpu {

// Builds a WGPUStringView for static C strings understood by webgpu.h.
WGPUStringView cstring_view(const char* value) noexcept {
  return WGPUStringView{value, WGPU_STRLEN};
}

// Builds a WGPUStringView that references an existing std::string.
WGPUStringView string_view(const std::string& value) noexcept {
  return WGPUStringView{value.c_str(), value.size()};
}

// Copies a WebGPU string view into a C++ string.
std::string string_from_view(WGPUStringView value) {
  if (value.data == nullptr) {
    return {};
  }
  if (value.length == WGPU_STRLEN) {
    return std::string(value.data);
  }
  return std::string(value.data, value.length);
}

// Converts requestAdapter status enums into stable debug text.
std::string request_adapter_status_name(WGPURequestAdapterStatus status) {
  switch (status) {
  case WGPURequestAdapterStatus_Success:
    return "success";
  case WGPURequestAdapterStatus_CallbackCancelled:
    return "callback cancelled";
  case WGPURequestAdapterStatus_Unavailable:
    return "unavailable";
  case WGPURequestAdapterStatus_Error:
    return "error";
  case WGPURequestAdapterStatus_Force32:
    break;
  }
  return "unknown";
}

// Converts requestDevice status enums into stable debug text.
std::string request_device_status_name(WGPURequestDeviceStatus status) {
  switch (status) {
  case WGPURequestDeviceStatus_Success:
    return "success";
  case WGPURequestDeviceStatus_CallbackCancelled:
    return "callback cancelled";
  case WGPURequestDeviceStatus_Error:
    return "error";
  case WGPURequestDeviceStatus_Force32:
    break;
  }
  return "unknown";
}

// Converts device-lost reason enums into stable debug text.
std::string device_lost_reason_name(WGPUDeviceLostReason reason) {
  switch (reason) {
  case WGPUDeviceLostReason_Unknown:
    return "unknown";
  case WGPUDeviceLostReason_Destroyed:
    return "destroyed";
  case WGPUDeviceLostReason_CallbackCancelled:
    return "callback cancelled";
  case WGPUDeviceLostReason_FailedCreation:
    return "failed creation";
  case WGPUDeviceLostReason_Force32:
    break;
  }
  return "unknown";
}

// Converts uncaptured WebGPU error types into stable debug text.
std::string error_type_name(WGPUErrorType type) {
  switch (type) {
  case WGPUErrorType_NoError:
    return "no error";
  case WGPUErrorType_Validation:
    return "validation";
  case WGPUErrorType_OutOfMemory:
    return "out of memory";
  case WGPUErrorType_Internal:
    return "internal";
  case WGPUErrorType_Unknown:
    return "unknown";
  case WGPUErrorType_Force32:
    break;
  }
  return "unknown";
}

// Converts surface-texture acquisition status into stable debug text.
std::string surface_texture_status_name(
  WGPUSurfaceGetCurrentTextureStatus status
) {
  switch (status) {
  case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    return "success optimal";
  case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
    return "success suboptimal";
  case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    return "timeout";
  case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    return "outdated";
  case WGPUSurfaceGetCurrentTextureStatus_Lost:
    return "lost";
  case WGPUSurfaceGetCurrentTextureStatus_Error:
    return "error";
  case WGPUSurfaceGetCurrentTextureStatus_Force32:
    break;
  }
  return "unknown";
}

// Combines an operation name, status string, and optional WebGPU message.
std::string failure_message(
  const char* operation,
  const std::string& status,
  WGPUStringView message
) {
  const std::string detail = string_from_view(message);
  if (detail.empty()) {
    return std::string(operation) + " failed with status " + status + ".";
  }
  return std::string(operation) + " failed with status " + status + ": " + detail;
}

// Converts texture formats used by the bootstrap smoke into contract names.
std::string texture_format_name(WGPUTextureFormat format) {
  switch (format) {
  case WGPUTextureFormat_BGRA8Unorm:
    return "Bgra8Unorm";
  case WGPUTextureFormat_BGRA8UnormSrgb:
    return "Bgra8UnormSrgb";
  case WGPUTextureFormat_RGBA8Unorm:
    return "Rgba8Unorm";
  case WGPUTextureFormat_RGBA8UnormSrgb:
    return "Rgba8UnormSrgb";
  case WGPUTextureFormat_RGBA16Float:
    return "Rgba16Float";
  default:
    return "Unknown";
  }
}

// Chooses a browser surface format from capabilities using OFG preferences.
WGPUTextureFormat choose_surface_format(
  const WGPUSurfaceCapabilities& capabilities
) {
  constexpr std::array preferred_formats{
    WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb,
    WGPUTextureFormat_RGBA8UnormSrgb
  };
  for (const WGPUTextureFormat format : preferred_formats) {
    for (std::size_t index = 0; index < capabilities.formatCount; ++index) {
      if (capabilities.formats[index] == format) {
        return format;
      }
    }
  }
  if (capabilities.formatCount > 0) {
    return capabilities.formats[0];
  }
  return WGPUTextureFormat_Undefined;
}

// Extracts the best available adapter name for debug status.
std::string adapter_name_from_info(WGPUAdapter adapter) {
  WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
  if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
    return "Browser WebGPU adapter";
  }

  std::string name = string_from_view(info.device);
  if (name.empty()) {
    name = string_from_view(info.description);
  }
  if (name.empty()) {
    name = string_from_view(info.vendor);
  }
  wgpuAdapterInfoFreeMembers(info);

  if (name.empty()) {
    return "Browser WebGPU adapter";
  }
  return name;
}

} // namespace ofg::webgpu

#endif
