// Borrowed WebGPU handles used to construct a device-bound OFG Game.
//
// Platform code owns the device and queue lifetimes. Game stores these handles
// only for the lifetime of that platform device and never releases them.
#pragma once

#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

struct GpuContext {
  WGPUDevice device{nullptr};
  WGPUQueue queue{nullptr};
  std::string adapter_name{"Unavailable"};
  std::string backend{"SharedGame"};
};

} // namespace ofg
