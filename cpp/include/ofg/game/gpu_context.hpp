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

} // namespace ofg
