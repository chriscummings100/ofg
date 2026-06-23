// Borrowed WebGPU handles used to construct a device-bound OFG Game.
//
// Platform code owns the device and queue lifetimes. Game stores these handles
// only for the lifetime of that platform device and never releases them.
#pragma once

#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

struct GpuContext {
    WGPUDevice m_device{nullptr};
    WGPUQueue m_queue{nullptr};
    std::string m_adapter_name{"Unavailable"};
    std::string m_backend{"SharedGame"};
};

// Returns true when a resource should remain CPU-only.
[[nodiscard]] inline bool gpu_context_is_empty(const GpuContext& gpu) noexcept {
    return gpu.m_device == nullptr && gpu.m_queue == nullptr;
}

// Returns true when a resource can create and update GPU state.
[[nodiscard]] inline bool gpu_context_is_ready(const GpuContext& gpu) noexcept {
    return gpu.m_device != nullptr && gpu.m_queue != nullptr;
}

} // namespace ofg
