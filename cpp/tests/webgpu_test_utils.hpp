// Test-only Dawn WebGPU device helper for OFG resource tests.
//
// The helper owns a null-backend Dawn instance, adapter, device, and queue.
// Tests borrow `ofg::GpuContext` from it while production code continues to
// receive platform-owned WebGPU handles.
#pragma once

#include "ofg/game/gpu_context.hpp"

#include <optional>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg::tests {

class TestGpuContext {
public:
    TestGpuContext(const TestGpuContext&) = delete;
    TestGpuContext& operator=(const TestGpuContext&) = delete;
    TestGpuContext(TestGpuContext&& other) noexcept;
    TestGpuContext& operator=(TestGpuContext&& other) noexcept;
    ~TestGpuContext();

    // Creates a Dawn null-backend device for resource lifecycle tests.
    [[nodiscard]] static std::optional<TestGpuContext> create(std::string& error);
    // Returns borrowed handles suitable for OFG resource construction.
    [[nodiscard]] GpuContext borrowed_context() const noexcept;

private:
    // Stores already-created Dawn handles; use create() for validation.
    TestGpuContext(WGPUInstance instance,
        WGPUAdapter adapter,
        WGPUDevice device,
        WGPUQueue queue,
        std::string adapter_name,
        std::string backend);

    // Releases owned Dawn handles in dependency order.
    void release() noexcept;

    WGPUInstance m_instance{nullptr};
    WGPUAdapter m_adapter{nullptr};
    WGPUDevice m_device{nullptr};
    WGPUQueue m_queue{nullptr};
    std::string m_adapter_name{"Unavailable"};
    std::string m_backend{"Unknown"};
};

} // namespace ofg::tests
