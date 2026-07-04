// Small GPU helpers shared by browser Emdawnwebgpu and native Dawn paths.
//
// WebGPU's C API represents strings as pointer+length views and uses verbose
// descriptors for common target objects. This module keeps those repeated
// details in one place without hiding ownership of actual WebGPU handles.
#include "ofg/gpu/common.hpp"

#include "ofg/core/engine_error.hpp"

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
    case WGPUTextureFormat_Depth32Float:
        return "Depth32Float";
    case WGPUTextureFormat_Depth24Plus:
        return "Depth24Plus";
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

// Creates a 2D depth texture for render attachment use.
WGPUTexture create_depth_texture(
    WGPUDevice device, WGPUTextureFormat depth_format, std::uint32_t width, std::uint32_t height, const char* label) {
    if (device == nullptr) {
        throw EngineError("Depth texture creation requires a WebGPU device.");
    }
    if (width == 0 || height == 0) {
        throw EngineError("Depth texture creation requires non-zero dimensions.");
    }
    if (depth_format == WGPUTextureFormat_Undefined) {
        throw EngineError("Depth texture creation requires a defined depth format.");
    }

    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = cstring_view(label);
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = depth_format;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &descriptor);
    if (texture == nullptr) {
        throw EngineError("wgpuDeviceCreateTexture returned null for depth target.");
    }
    return texture;
}

// Creates the default 2D view for a depth texture.
WGPUTextureView create_depth_view(WGPUTexture texture, WGPUTextureFormat depth_format, const char* label) {
    if (texture == nullptr) {
        throw EngineError("Depth view creation requires a WebGPU texture.");
    }
    if (depth_format == WGPUTextureFormat_Undefined) {
        throw EngineError("Depth view creation requires a defined depth format.");
    }

    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.label = cstring_view(label);
    descriptor.format = depth_format;
    descriptor.dimension = WGPUTextureViewDimension_2D;
    descriptor.baseMipLevel = 0;
    descriptor.mipLevelCount = 1;
    descriptor.baseArrayLayer = 0;
    descriptor.arrayLayerCount = 1;
    descriptor.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    if (view == nullptr) {
        throw EngineError("wgpuTextureCreateView returned null for depth target.");
    }
    return view;
}

} // namespace ofg::gpu
