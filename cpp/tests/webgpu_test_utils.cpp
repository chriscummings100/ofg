// Test-only Dawn WebGPU device helper for OFG resource tests.
#include "webgpu_test_utils.hpp"

#include "ofg/gpu/common.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace ofg::tests {
namespace {

constexpr std::uint64_t _wait_timeout_ns = 15'000'000'000ULL;

struct AdapterRequest {
    WGPURequestAdapterStatus m_status{WGPURequestAdapterStatus_Unavailable};
    WGPUAdapter m_adapter{nullptr};
    std::string m_message;
};

struct DeviceRequest {
    WGPURequestDeviceStatus m_status{WGPURequestDeviceStatus_Error};
    WGPUDevice m_device{nullptr};
    std::string m_message;
};

// Converts wait statuses into test failure text.
std::string wait_status_name(WGPUWaitStatus status) {
    switch (status) {
    case WGPUWaitStatus_Success:
        return "success";
    case WGPUWaitStatus_TimedOut:
        return "timed out";
    case WGPUWaitStatus_Error:
        return "error";
    case WGPUWaitStatus_Force32:
        break;
    }
    return "unknown";
}

// Converts adapter request statuses into test failure text.
std::string adapter_status_name(WGPURequestAdapterStatus status) {
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

// Converts device request statuses into test failure text.
std::string device_status_name(WGPURequestDeviceStatus status) {
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

// Waits for a Dawn future so asynchronous requests complete deterministically.
bool wait_for_future(WGPUInstance instance, WGPUFuture future, const char* operation, std::string& error) {
    WGPUFutureWaitInfo wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
    wait_info.future = future;
    const WGPUWaitStatus status = wgpuInstanceWaitAny(instance, 1, &wait_info, _wait_timeout_ns);
    if (status != WGPUWaitStatus_Success || wait_info.completed != WGPU_TRUE) {
        error = std::string(operation) + " wait failed with status " + wait_status_name(status) + ".";
        return false;
    }
    return true;
}

// Stores Dawn's requestAdapter callback result.
void handle_adapter_request(
    WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<AdapterRequest*>(userdata1);
    request->m_status = status;
    request->m_adapter = adapter;
    request->m_message = gpu::string_from_view(message);
}

// Stores Dawn's requestDevice callback result.
void handle_device_request(
    WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<DeviceRequest*>(userdata1);
    request->m_status = status;
    request->m_device = device;
    request->m_message = gpu::string_from_view(message);
}

} // namespace

// Stores already-created Dawn handles; use create() for validation.
TestGpuContext::TestGpuContext(WGPUInstance instance,
    WGPUAdapter adapter,
    WGPUDevice device,
    WGPUQueue queue,
    std::string adapter_name,
    std::string backend)
    : m_instance(instance), m_adapter(adapter), m_device(device), m_queue(queue),
      m_adapter_name(std::move(adapter_name)), m_backend(std::move(backend)) {}

// Transfers owned Dawn handles.
TestGpuContext::TestGpuContext(TestGpuContext&& other) noexcept
    : m_instance(std::exchange(other.m_instance, nullptr)), m_adapter(std::exchange(other.m_adapter, nullptr)),
      m_device(std::exchange(other.m_device, nullptr)), m_queue(std::exchange(other.m_queue, nullptr)),
      m_adapter_name(std::move(other.m_adapter_name)), m_backend(std::move(other.m_backend)) {}

// Releases current handles, then transfers owned Dawn handles.
TestGpuContext& TestGpuContext::operator=(TestGpuContext&& other) noexcept {
    if (this != &other) {
        release();
        m_instance = std::exchange(other.m_instance, nullptr);
        m_adapter = std::exchange(other.m_adapter, nullptr);
        m_device = std::exchange(other.m_device, nullptr);
        m_queue = std::exchange(other.m_queue, nullptr);
        m_adapter_name = std::move(other.m_adapter_name);
        m_backend = std::move(other.m_backend);
    }
    return *this;
}

// Releases the test device handles.
TestGpuContext::~TestGpuContext() {
    release();
}

// Creates a Dawn null-backend device for resource lifecycle tests.
std::optional<TestGpuContext> TestGpuContext::create(std::string& error) {
    const WGPUInstanceFeatureName instance_feature = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceLimits instance_limits = WGPU_INSTANCE_LIMITS_INIT;
    instance_limits.timedWaitAnyMaxCount = 1;
    WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_descriptor.requiredFeatureCount = 1;
    instance_descriptor.requiredFeatures = &instance_feature;
    instance_descriptor.requiredLimits = &instance_limits;

    WGPUInstance instance = wgpuCreateInstance(&instance_descriptor);
    if (instance == nullptr) {
        error = "wgpuCreateInstance returned null.";
        return std::nullopt;
    }

    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.backendType = WGPUBackendType_Null;

    AdapterRequest adapter_request;
    WGPURequestAdapterCallbackInfo adapter_callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_callback.callback = handle_adapter_request;
    adapter_callback.userdata1 = &adapter_request;
    if (!wait_for_future(instance,
            wgpuInstanceRequestAdapter(instance, &adapter_options, adapter_callback),
            "requestAdapter",
            error)) {
        wgpuInstanceRelease(instance);
        return std::nullopt;
    }
    if (adapter_request.m_status != WGPURequestAdapterStatus_Success || adapter_request.m_adapter == nullptr) {
        error = "requestAdapter failed with status " + adapter_status_name(adapter_request.m_status) + ": " +
                adapter_request.m_message;
        wgpuInstanceRelease(instance);
        return std::nullopt;
    }

    std::string adapter_name{"Dawn null adapter"};
    std::string backend{"Null"};
    WGPUAdapterInfo adapter_info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter_request.m_adapter, &adapter_info) == WGPUStatus_Success) {
        adapter_name = gpu::string_from_view(adapter_info.device);
        if (adapter_name.empty()) {
            adapter_name = gpu::string_from_view(adapter_info.description);
        }
        backend = gpu::backend_type_name(adapter_info.backendType);
        wgpuAdapterInfoFreeMembers(adapter_info);
    }

    WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_descriptor.label = gpu::cstring_view("OFG test WebGPU device");
    device_descriptor.defaultQueue.label = gpu::cstring_view("OFG test WebGPU queue");

    DeviceRequest device_request;
    WGPURequestDeviceCallbackInfo device_callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    device_callback.callback = handle_device_request;
    device_callback.userdata1 = &device_request;
    if (!wait_for_future(instance,
            wgpuAdapterRequestDevice(adapter_request.m_adapter, &device_descriptor, device_callback),
            "requestDevice",
            error)) {
        wgpuAdapterRelease(adapter_request.m_adapter);
        wgpuInstanceRelease(instance);
        return std::nullopt;
    }
    if (device_request.m_status != WGPURequestDeviceStatus_Success || device_request.m_device == nullptr) {
        error = "requestDevice failed with status " + device_status_name(device_request.m_status) + ": " +
                device_request.m_message;
        wgpuAdapterRelease(adapter_request.m_adapter);
        wgpuInstanceRelease(instance);
        return std::nullopt;
    }

    WGPUQueue queue = wgpuDeviceGetQueue(device_request.m_device);
    if (queue == nullptr) {
        error = "wgpuDeviceGetQueue returned null.";
        wgpuDeviceRelease(device_request.m_device);
        wgpuAdapterRelease(adapter_request.m_adapter);
        wgpuInstanceRelease(instance);
        return std::nullopt;
    }

    error.clear();
    return TestGpuContext(instance, adapter_request.m_adapter, device_request.m_device, queue, adapter_name, backend);
}

// Returns borrowed handles suitable for OFG resource construction.
GpuContext TestGpuContext::borrowed_context() const noexcept {
    return GpuContext{m_device, m_queue, m_adapter_name, m_backend};
}

// Releases owned Dawn handles in dependency order.
void TestGpuContext::release() noexcept {
    if (m_queue != nullptr) {
        wgpuQueueRelease(m_queue);
        m_queue = nullptr;
    }
    if (m_device != nullptr) {
        wgpuDeviceRelease(m_device);
        m_device = nullptr;
    }
    if (m_adapter != nullptr) {
        wgpuAdapterRelease(m_adapter);
        m_adapter = nullptr;
    }
    if (m_instance != nullptr) {
        wgpuInstanceRelease(m_instance);
        m_instance = nullptr;
    }
}

} // namespace ofg::tests
