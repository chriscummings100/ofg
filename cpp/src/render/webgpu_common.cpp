// Small WebGPU helpers shared by browser Emdawnwebgpu and native Dawn paths.
//
// WebGPU's C API represents strings as pointer+length views. This module keeps
// that conversion in one place so renderer/native code can label objects and
// reports without repeating low-level string-view rules.
#include "ofg/render/webgpu_common.hpp"

namespace ofg::gpu {

// Builds a null-terminated WebGPU string view from a string literal.
WGPUStringView cstring_view(const char* value) noexcept {
  return WGPUStringView{value, WGPU_STRLEN};
}

// Builds a bounded WebGPU string view from a live std::string.
WGPUStringView string_view(const std::string& value) noexcept {
  return WGPUStringView{value.c_str(), value.size()};
}

// Copies a WebGPU string view into a standard string.
std::string string_from_view(WGPUStringView value) {
  if (value.data == nullptr) {
    return {};
  }
  if (value.length == WGPU_STRLEN) {
    return std::string(value.data);
  }
  return std::string(value.data, value.length);
}

// Converts a texture format into the public OFG status/report label.
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

// Converts a native Dawn backend type into the smoke report label.
std::string backend_type_name(WGPUBackendType backend) {
  switch (backend) {
  case WGPUBackendType_Null:
    return "Null";
  case WGPUBackendType_WebGPU:
    return "WebGPU";
  case WGPUBackendType_D3D11:
    return "D3D11";
  case WGPUBackendType_D3D12:
    return "D3D12";
  case WGPUBackendType_Metal:
    return "Metal";
  case WGPUBackendType_Vulkan:
    return "Vulkan";
  case WGPUBackendType_OpenGL:
    return "OpenGL";
  case WGPUBackendType_OpenGLES:
    return "OpenGLES";
  case WGPUBackendType_Undefined:
  case WGPUBackendType_Force32:
    break;
  }
  return "Unknown";
}

} // namespace ofg::gpu
