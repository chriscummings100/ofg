// Native Dawn render-smoke contract and implementation.
//
// This file owns the browser-free renderer validation path for the C++/WASM
// migration. It creates a native Dawn instance/device, renders OFG's shared
// plane-and-cubes demo through the Game render path, reads the offscreen texture
// back into CPU memory, writes a PNG, and records the same threshold diagnostics
// as the browser smoke. The native backend is intentionally constrained to
// Vulkan for this Windows migration path so it cannot quietly pass through
// Dawn's null backend.
#include "ofg/native/render_smoke.hpp"

#include "ofg/game/game.hpp"
#include "ofg/native/png_writer.hpp"
#include "ofg/render/demo_scene.hpp"
#include "ofg/gpu/common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg::native {
namespace {

constexpr std::uint32_t _bytes_per_pixel = 4;
constexpr std::uint64_t _wait_timeout_ns = 15'000'000'000ULL;
constexpr WGPUTextureFormat _render_format = WGPUTextureFormat_RGBA8Unorm;

struct PixelReport {
    std::uint64_t m_sampled_pixels{0};
    std::uint64_t m_scene_pixels{0};
    std::uint64_t m_background_pixels{0};
    std::uint64_t m_ground_pixels{0};
    std::uint64_t m_colored_pixels{0};
    std::uint64_t m_lower_half_sampled_pixels{0};
    std::uint64_t m_lower_half_scene_pixels{0};
    double m_scene_ratio{0.0};
    double m_background_ratio{0.0};
    double m_ground_ratio{0.0};
    double m_colored_ratio{0.0};
    double m_lower_half_scene_ratio{0.0};
    std::uint32_t m_non_background_color_buckets{0};
    std::string m_failure_reason;
};

// Owns Dawn handles in release order for the native smoke lifetime.
struct GpuContext {
    WGPUInstance m_instance{nullptr};
    WGPUAdapter m_adapter{nullptr};
    WGPUDevice m_device{nullptr};
    WGPUQueue m_queue{nullptr};
    std::string m_adapter_name{"Unavailable"};
    std::string m_backend{"Unknown"};

    GpuContext() = default;
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    // Transfers ownership of all Dawn handles from another context.
    GpuContext(GpuContext&& other) noexcept
        : m_instance(std::exchange(other.m_instance, nullptr)), m_adapter(std::exchange(other.m_adapter, nullptr)),
          m_device(std::exchange(other.m_device, nullptr)), m_queue(std::exchange(other.m_queue, nullptr)),
          m_adapter_name(std::move(other.m_adapter_name)), m_backend(std::move(other.m_backend)) {}

    // Releases current handles, then takes ownership from another context.
    GpuContext& operator=(GpuContext&& other) noexcept {
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

    // Releases Dawn handles in queue/device/adapter/instance order.
    ~GpuContext() {
        release();
    }

    // Performs the actual idempotent handle release for destructor and move assignment.
    void release() {
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
};

// Releases the static Game singleton before borrowed Dawn handles are destroyed.
struct StaticGameGuard {
    bool m_active{false};

    StaticGameGuard() = default;
    StaticGameGuard(const StaticGameGuard&) = delete;
    StaticGameGuard& operator=(const StaticGameGuard&) = delete;

    // Drains Game release during stack unwinding without throwing.
    ~StaticGameGuard() {
        if (!m_active) {
            return;
        }
        try {
            while (!ofg::Game::release()) {}
        } catch (...) {}
        ofg::Game::destroy();
    }
};

// Releases an offscreen render texture created during smoke execution.
struct ScopedTexture {
    WGPUTexture m_value{nullptr};

    // Releases the temporary offscreen texture.
    ~ScopedTexture() {
        if (m_value != nullptr) {
            wgpuTextureRelease(m_value);
        }
    }
};

// Releases the texture view used as the render pass attachment.
struct ScopedTextureView {
    WGPUTextureView m_value{nullptr};

    // Releases the render-target view.
    ~ScopedTextureView() {
        if (m_value != nullptr) {
            wgpuTextureViewRelease(m_value);
        }
    }
};

// Releases the readback buffer after mapped pixels have been copied out.
struct ScopedBuffer {
    WGPUBuffer m_value{nullptr};

    // Releases the readback buffer after unmap/copy is complete.
    ~ScopedBuffer() {
        if (m_value != nullptr) {
            wgpuBufferRelease(m_value);
        }
    }
};

// Releases a command encoder unless ownership has been consumed by Finish.
struct ScopedCommandEncoder {
    WGPUCommandEncoder m_value{nullptr};

    // Releases an unfinished encoder; finished encoders clear the handle first.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

// Releases the finished command buffer after queue submission.
struct ScopedCommandBuffer {
    WGPUCommandBuffer m_value{nullptr};

    // Releases the submitted command buffer.
    ~ScopedCommandBuffer() {
        if (m_value != nullptr) {
            wgpuCommandBufferRelease(m_value);
        }
    }
};

// Carries the result of Dawn's asynchronous adapter request callback.
struct AdapterRequest {
    WGPURequestAdapterStatus m_status{WGPURequestAdapterStatus_Unavailable};
    WGPUAdapter m_adapter{nullptr};
    std::string m_message;
};

// Carries the result of Dawn's asynchronous device request callback.
struct DeviceRequest {
    WGPURequestDeviceStatus m_status{WGPURequestDeviceStatus_Error};
    WGPUDevice m_device{nullptr};
    std::string m_message;
};

// Carries the result of Dawn's asynchronous buffer map callback.
struct MapRequest {
    WGPUMapAsyncStatus m_status{WGPUMapAsyncStatus_Error};
    std::string m_message;
};

// Carries the result of popping a WebGPU validation error scope.
struct ErrorScopeRequest {
    WGPUPopErrorScopeStatus m_status{WGPUPopErrorScopeStatus_Error};
    WGPUErrorType m_type{WGPUErrorType_Unknown};
    std::string m_message;
};

// Converts Dawn wait status values into reportable failure text.
[[nodiscard]] std::string wait_status_name(WGPUWaitStatus status) {
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

// Converts adapter request status values into reportable failure text.
[[nodiscard]] std::string adapter_status_name(WGPURequestAdapterStatus status) {
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

// Converts device request status values into reportable failure text.
[[nodiscard]] std::string device_status_name(WGPURequestDeviceStatus status) {
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

// Converts mapAsync status values into reportable failure text.
[[nodiscard]] std::string map_status_name(WGPUMapAsyncStatus status) {
    switch (status) {
    case WGPUMapAsyncStatus_Success:
        return "success";
    case WGPUMapAsyncStatus_CallbackCancelled:
        return "callback cancelled";
    case WGPUMapAsyncStatus_Error:
        return "error";
    case WGPUMapAsyncStatus_Aborted:
        return "aborted";
    case WGPUMapAsyncStatus_Force32:
        break;
    }
    return "unknown";
}

// Converts error-scope status values into reportable failure text.
[[nodiscard]] std::string error_scope_status_name(WGPUPopErrorScopeStatus status) {
    switch (status) {
    case WGPUPopErrorScopeStatus_Success:
        return "success";
    case WGPUPopErrorScopeStatus_CallbackCancelled:
        return "callback cancelled";
    case WGPUPopErrorScopeStatus_Error:
        return "error";
    case WGPUPopErrorScopeStatus_Force32:
        break;
    }
    return "unknown";
}

// Converts WebGPU error types into reportable failure text.
[[nodiscard]] std::string error_type_name(WGPUErrorType type) {
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

// Escapes strings written into the smoke report JSON.
[[nodiscard]] std::string json_escape(const std::string& value) {
    std::string escaped;
    for (const char item : value) {
        switch (item) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += item;
            break;
        }
    }
    return escaped;
}

// Formats an optional JSON string field, using null for empty failure reasons.
[[nodiscard]] std::string string_or_null(const std::string& value) {
    if (value.empty()) {
        return "null";
    }
    return "\"" + json_escape(value) + "\"";
}

// Waits for a Dawn future with a finite timeout so GPU failures do not hang.
void wait_for_future(WGPUInstance instance, WGPUFuture future, const char* operation) {
    WGPUFutureWaitInfo wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
    wait_info.future = future;
    const WGPUWaitStatus status = wgpuInstanceWaitAny(instance, 1, &wait_info, _wait_timeout_ns);
    if (status != WGPUWaitStatus_Success || wait_info.completed != WGPU_TRUE) {
        throw std::runtime_error(std::string(operation) + " wait failed with status " + wait_status_name(status) + ".");
    }
}

// Stores the adapter returned by Dawn's requestAdapter callback.
void handle_adapter_request(
    WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<AdapterRequest*>(userdata1);
    request->m_status = status;
    request->m_adapter = adapter;
    request->m_message = gpu::string_from_view(message);
}

// Stores the device returned by Dawn's requestDevice callback.
void handle_device_request(
    WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<DeviceRequest*>(userdata1);
    request->m_status = status;
    request->m_device = device;
    request->m_message = gpu::string_from_view(message);
}

// Stores the mapAsync result after Dawn finishes readback synchronization.
void handle_map_request(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<MapRequest*>(userdata1);
    request->m_status = status;
    request->m_message = gpu::string_from_view(message);
}

// Stores validation diagnostics from Dawn after the smoke render scope closes.
void handle_error_scope(
    WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    auto* request = static_cast<ErrorScopeRequest*>(userdata1);
    request->m_status = status;
    request->m_type = type;
    request->m_message = gpu::string_from_view(message);
}

// Aligns row pitch to WebGPU's texture-to-buffer copy requirements.
[[nodiscard]] std::uint32_t align_to(std::uint32_t value, std::uint32_t alignment) {
    return ((value + alignment - 1U) / alignment) * alignment;
}

// Computes RGB distance while ignoring alpha, matching the browser smoke.
[[nodiscard]] double color_distance(const std::array<std::uint8_t, 4>& left, const std::vector<std::uint8_t>& right) {
    const double dr = static_cast<double>(left[0]) - right[0];
    const double dg = static_cast<double>(left[1]) - right[1];
    const double db = static_cast<double>(left[2]) - right[2];
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// Reports whether a non-background pixel looks like neutral checker ground.
[[nodiscard]] bool is_ground_like_pixel(const std::array<std::uint8_t, 4>& pixel) {
    const std::uint8_t max_channel = std::max({pixel[0], pixel[1], pixel[2]});
    const std::uint8_t min_channel = std::min({pixel[0], pixel[1], pixel[2]});
    const std::uint32_t brightness = static_cast<std::uint32_t>(pixel[0]) + pixel[1] + pixel[2];
    return max_channel - min_channel <= 30U && brightness >= 90U && brightness <= 690U;
}

// Samples the rendered pixels and checks them against the shared smoke thresholds.
[[nodiscard]] PixelReport inspect_pixels(const std::vector<std::uint8_t>& pixels, const SmokeContract& contract) {
    const std::size_t expected_size = static_cast<std::size_t>(contract.m_width) * contract.m_height * _bytes_per_pixel;
    if (pixels.size() != expected_size) {
        throw std::runtime_error("Expected " + std::to_string(expected_size) + " RGBA bytes for inspection, got " +
                                 std::to_string(pixels.size()) + ".");
    }

    // Bucket non-background colors coarsely to tolerate interpolation variance.
    PixelReport report{};
    std::set<std::string> buckets;
    for (std::uint32_t y = 0; y < contract.m_height; y += contract.m_sample_step) {
        for (std::uint32_t x = 0; x < contract.m_width; x += contract.m_sample_step) {
            const std::size_t index = (static_cast<std::size_t>(contract.m_width) * y + x) * _bytes_per_pixel;
            const std::array<std::uint8_t, 4> pixel{
                pixels[index], pixels[index + 1U], pixels[index + 2U], pixels[index + 3U]};
            if (y >= contract.m_height / 2U) {
                report.m_lower_half_sampled_pixels += 1U;
            }
            if (color_distance(pixel, contract.m_clear_color_rgba8) <= contract.m_color_distance_tolerance) {
                report.m_background_pixels += 1U;
            } else {
                report.m_scene_pixels += 1U;
                if (y >= contract.m_height / 2U) {
                    report.m_lower_half_scene_pixels += 1U;
                }
                if (is_ground_like_pixel(pixel)) {
                    report.m_ground_pixels += 1U;
                } else {
                    report.m_colored_pixels += 1U;
                }
                buckets.insert(std::to_string(pixel[0] / contract.m_bucket_divisor) + ":" +
                               std::to_string(pixel[1] / contract.m_bucket_divisor) + ":" +
                               std::to_string(pixel[2] / contract.m_bucket_divisor));
            }
        }
    }

    // Convert counts into ratios and make exactly one failure reason visible.
    report.m_sampled_pixels = report.m_background_pixels + report.m_scene_pixels;
    report.m_scene_ratio = static_cast<double>(report.m_scene_pixels) / report.m_sampled_pixels;
    report.m_background_ratio = static_cast<double>(report.m_background_pixels) / report.m_sampled_pixels;
    report.m_ground_ratio = static_cast<double>(report.m_ground_pixels) / report.m_sampled_pixels;
    report.m_colored_ratio = static_cast<double>(report.m_colored_pixels) / report.m_sampled_pixels;
    report.m_lower_half_scene_ratio =
        report.m_lower_half_sampled_pixels == 0
            ? 0.0
            : static_cast<double>(report.m_lower_half_scene_pixels) / report.m_lower_half_sampled_pixels;
    report.m_non_background_color_buckets = static_cast<std::uint32_t>(buckets.size());

    if (report.m_scene_ratio < contract.m_min_scene_ratio) {
        report.m_failure_reason = "Scene coverage too low: " + std::to_string(report.m_scene_ratio);
    } else if (report.m_background_ratio < contract.m_min_background_ratio) {
        report.m_failure_reason = "Background coverage too low: " + std::to_string(report.m_background_ratio);
    } else if (report.m_ground_ratio < contract.m_min_ground_ratio) {
        report.m_failure_reason = "Ground coverage too low: " + std::to_string(report.m_ground_ratio);
    } else if (report.m_colored_ratio < contract.m_min_colored_ratio) {
        report.m_failure_reason = "Colored cube coverage too low: " + std::to_string(report.m_colored_ratio);
    } else if (report.m_lower_half_scene_ratio < contract.m_min_lower_half_scene_ratio) {
        report.m_failure_reason =
            "Lower-half scene coverage too low: " + std::to_string(report.m_lower_half_scene_ratio);
    } else if (report.m_non_background_color_buckets < contract.m_min_non_background_color_buckets) {
        report.m_failure_reason = "Expected at least " + std::to_string(contract.m_min_non_background_color_buckets) +
                                  " non-background color buckets; got " +
                                  std::to_string(report.m_non_background_color_buckets) + ".";
    }

    return report;
}

// Validates contract values before the expensive Dawn path starts.
void validate_contract(const SmokeContract& contract) {
    if (contract.m_width == 0 || contract.m_height == 0) {
        throw std::runtime_error("Smoke contract dimensions must be non-zero.");
    }
    if (contract.m_clear_color_rgba8.size() != 4U) {
        throw std::runtime_error("Smoke contract clear color must have four channels.");
    }
    if (contract.m_sample_step == 0) {
        throw std::runtime_error("Smoke contract sample step must be non-zero.");
    }
    if (contract.m_bucket_divisor == 0) {
        throw std::runtime_error("Smoke contract bucket divisor must be non-zero.");
    }
}

// Creates the native Dawn instance, Vulkan adapter, device, and queue.
[[nodiscard]] GpuContext create_gpu_context() {
    GpuContext context;

    // Timed waits are required so request/map futures can fail with a timeout.
    const WGPUInstanceFeatureName instance_feature = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceLimits instance_limits = WGPU_INSTANCE_LIMITS_INIT;
    instance_limits.timedWaitAnyMaxCount = 1;
    WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance_descriptor.requiredFeatureCount = 1;
    instance_descriptor.requiredFeatures = &instance_feature;
    instance_descriptor.requiredLimits = &instance_limits;
    context.m_instance = wgpuCreateInstance(&instance_descriptor);
    if (context.m_instance == nullptr) {
        throw std::runtime_error("wgpuCreateInstance returned null.");
    }

    // Request a real Vulkan adapter; null backend is not acceptable for pixels.
    WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_options.backendType = WGPUBackendType_Vulkan;

    AdapterRequest adapter_request;
    WGPURequestAdapterCallbackInfo adapter_callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_callback.callback = handle_adapter_request;
    adapter_callback.userdata1 = &adapter_request;
    wait_for_future(context.m_instance,
        wgpuInstanceRequestAdapter(context.m_instance, &adapter_options, adapter_callback),
        "requestAdapter");
    if (adapter_request.m_status != WGPURequestAdapterStatus_Success || adapter_request.m_adapter == nullptr) {
        throw std::runtime_error("requestAdapter failed with status " + adapter_status_name(adapter_request.m_status) +
                                 ": " + adapter_request.m_message);
    }
    context.m_adapter = adapter_request.m_adapter;

    // Capture adapter diagnostics before creating the device.
    WGPUAdapterInfo adapter_info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(context.m_adapter, &adapter_info) == WGPUStatus_Success) {
        context.m_adapter_name = gpu::string_from_view(adapter_info.device);
        if (context.m_adapter_name.empty()) {
            context.m_adapter_name = gpu::string_from_view(adapter_info.description);
        }
        if (context.m_adapter_name.empty()) {
            context.m_adapter_name = gpu::string_from_view(adapter_info.vendor);
        }
        context.m_backend = gpu::backend_type_name(adapter_info.backendType);
        wgpuAdapterInfoFreeMembers(adapter_info);
    }
    if (context.m_backend != "Vulkan") {
        throw std::runtime_error(
            "Native render smoke requires a real Vulkan Dawn backend; got " + context.m_backend + ".");
    }

    // Create the WebGPU device used by the shared renderer.
    WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_descriptor.label = gpu::cstring_view("OFG native render-smoke device");
    device_descriptor.defaultQueue.label = gpu::cstring_view("OFG native render-smoke queue");

    DeviceRequest device_request;
    WGPURequestDeviceCallbackInfo device_callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    device_callback.callback = handle_device_request;
    device_callback.userdata1 = &device_request;
    wait_for_future(context.m_instance,
        wgpuAdapterRequestDevice(context.m_adapter, &device_descriptor, device_callback),
        "requestDevice");
    if (device_request.m_status != WGPURequestDeviceStatus_Success || device_request.m_device == nullptr) {
        throw std::runtime_error("requestDevice failed with status " + device_status_name(device_request.m_status) +
                                 ": " + device_request.m_message);
    }

    context.m_device = device_request.m_device;
    context.m_queue = wgpuDeviceGetQueue(context.m_device);
    if (context.m_queue == nullptr) {
        throw std::runtime_error("wgpuDeviceGetQueue returned null.");
    }
    return context;
}

// Renders the demo scene offscreen and returns tightly packed RGBA pixels.
[[nodiscard]] std::vector<std::uint8_t> render_and_readback(GpuContext& context, const SmokeContract& contract) {
    wgpuDevicePushErrorScope(context.m_device, WGPUErrorFilter_Validation);

    // Build the same device-bound Game singleton used by the browser path.
    StaticGameGuard game_guard;
    ofg::Game::create(
        ofg::GpuContext{context.m_device, context.m_queue, context.m_adapter_name, context.m_backend}, _render_format);
    game_guard.m_active = true;
    ofg::Game::resize(contract.m_width, contract.m_height, 1.0);
    while (!ofg::Game::prepare()) {}
    ofg::Game::update(demo_native_smoke_time_ms());

    // Render into a copyable offscreen texture.
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.label = gpu::cstring_view("OFG native render-smoke texture");
    texture_descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = WGPUExtent3D{contract.m_width, contract.m_height, 1};
    texture_descriptor.format = _render_format;
    ScopedTexture texture{wgpuDeviceCreateTexture(context.m_device, &texture_descriptor)};
    if (texture.m_value == nullptr) {
        throw std::runtime_error("wgpuDeviceCreateTexture returned null.");
    }

    ScopedTextureView view{wgpuTextureCreateView(texture.m_value, nullptr)};
    if (view.m_value == nullptr) {
        throw std::runtime_error("wgpuTextureCreateView returned null.");
    }

    // WebGPU readback rows must be 256-byte aligned.
    const std::uint32_t unpadded_bytes_per_row = contract.m_width * _bytes_per_pixel;
    const std::uint32_t padded_bytes_per_row = align_to(unpadded_bytes_per_row, 256);
    const std::uint64_t readback_size = static_cast<std::uint64_t>(padded_bytes_per_row) * contract.m_height;

    WGPUBufferDescriptor buffer_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer_descriptor.label = gpu::cstring_view("OFG native render-smoke readback buffer");
    buffer_descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    buffer_descriptor.size = readback_size;
    buffer_descriptor.mappedAtCreation = WGPU_FALSE;
    ScopedBuffer readback{wgpuDeviceCreateBuffer(context.m_device, &buffer_descriptor)};
    if (readback.m_value == nullptr) {
        throw std::runtime_error("wgpuDeviceCreateBuffer returned null.");
    }

    // Encode the render pass and a texture-to-buffer copy in one command buffer.
    WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoder_descriptor.label = gpu::cstring_view("OFG native render-smoke encoder");
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(context.m_device, &encoder_descriptor)};
    if (encoder.m_value == nullptr) {
        throw std::runtime_error("wgpuDeviceCreateCommandEncoder returned null.");
    }

    ofg::Game::render(
        encoder.m_value, ofg::RenderTarget{view.m_value, _render_format, contract.m_width, contract.m_height});

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture.m_value;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = readback.m_value;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = padded_bytes_per_row;
    destination.layout.rowsPerImage = contract.m_height;

    const WGPUExtent3D copy_size{contract.m_width, contract.m_height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder.m_value, &source, &destination, &copy_size);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = gpu::cstring_view("OFG native render-smoke commands");
    ScopedCommandBuffer command{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    if (command.m_value == nullptr) {
        throw std::runtime_error("wgpuCommandEncoderFinish returned null.");
    }

    wgpuQueueSubmit(context.m_queue, 1, &command.m_value);

    ErrorScopeRequest error_scope;
    WGPUPopErrorScopeCallbackInfo error_scope_callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    error_scope_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    error_scope_callback.callback = handle_error_scope;
    error_scope_callback.userdata1 = &error_scope;
    wait_for_future(
        context.m_instance, wgpuDevicePopErrorScope(context.m_device, error_scope_callback), "popErrorScope");
    if (error_scope.m_status != WGPUPopErrorScopeStatus_Success) {
        throw std::runtime_error("popErrorScope failed with status " + error_scope_status_name(error_scope.m_status) +
                                 ": " + error_scope.m_message);
    }
    if (error_scope.m_type != WGPUErrorType_NoError) {
        throw std::runtime_error(
            "native render smoke WebGPU " + error_type_name(error_scope.m_type) + " error: " + error_scope.m_message);
    }

    // Map the readback buffer after submission and fail with a finite timeout.
    MapRequest map_request;
    WGPUBufferMapCallbackInfo map_callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_callback.mode = WGPUCallbackMode_WaitAnyOnly;
    map_callback.callback = handle_map_request;
    map_callback.userdata1 = &map_request;
    wait_for_future(context.m_instance,
        wgpuBufferMapAsync(
            readback.m_value, WGPUMapMode_Read, 0, static_cast<std::size_t>(readback_size), map_callback),
        "mapAsync");
    if (map_request.m_status != WGPUMapAsyncStatus_Success) {
        throw std::runtime_error(
            "mapAsync failed with status " + map_status_name(map_request.m_status) + ": " + map_request.m_message);
    }

    const auto* mapped = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(readback.m_value, 0, static_cast<std::size_t>(readback_size)));
    if (mapped == nullptr) {
        throw std::runtime_error("wgpuBufferGetConstMappedRange returned null.");
    }

    // Remove GPU row padding so the PNG writer receives tightly packed RGBA rows.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(unpadded_bytes_per_row) * contract.m_height);
    for (std::uint32_t row = 0; row < contract.m_height; ++row) {
        const std::size_t src_start = static_cast<std::size_t>(row) * padded_bytes_per_row;
        const std::size_t dst_start = static_cast<std::size_t>(row) * unpadded_bytes_per_row;
        std::copy_n(mapped + src_start, unpadded_bytes_per_row, pixels.data() + dst_start);
    }
    wgpuBufferUnmap(readback.m_value);

    return pixels;
}

// Writes the JSON report expected by the native render-smoke contract.
void write_report(const std::filesystem::path& png_path,
    const std::filesystem::path& report_path,
    const SmokeContract& contract,
    const GpuContext& context,
    const PixelReport& pixels) {
    const bool passed = pixels.m_failure_reason.empty();
    std::ofstream report(report_path);
    if (!report) {
        throw std::runtime_error("Could not open native render-smoke report path: " + report_path.string());
    }

    report << "{\n"
           << "  \"pngPath\": \"" << json_escape(png_path.generic_string()) << "\",\n"
           << "  \"reportPath\": \"" << json_escape(report_path.generic_string()) << "\",\n"
           << "  \"width\": " << contract.m_width << ",\n"
           << "  \"height\": " << contract.m_height << ",\n"
           << "  \"textureFormat\": \"" << gpu::texture_format_name(_render_format) << "\",\n"
           << "  \"adapterName\": \"" << json_escape(context.m_adapter_name) << "\",\n"
           << "  \"backend\": \"" << json_escape(context.m_backend) << "\",\n"
           << "  \"clearColor\": [\n"
           << "    " << static_cast<int>(contract.m_clear_color_rgba8[0]) << ",\n"
           << "    " << static_cast<int>(contract.m_clear_color_rgba8[1]) << ",\n"
           << "    " << static_cast<int>(contract.m_clear_color_rgba8[2]) << ",\n"
           << "    " << static_cast<int>(contract.m_clear_color_rgba8[3]) << "\n"
           << "  ],\n"
           << "  \"thresholds\": {\n"
           << "    \"sampleStep\": " << contract.m_sample_step << ",\n"
           << "    \"colorDistanceTolerance\": " << contract.m_color_distance_tolerance << ",\n"
           << "    \"bucketDivisor\": " << contract.m_bucket_divisor << ",\n"
           << "    \"minSceneRatio\": " << contract.m_min_scene_ratio << ",\n"
           << "    \"minBackgroundRatio\": " << contract.m_min_background_ratio << ",\n"
           << "    \"minGroundRatio\": " << contract.m_min_ground_ratio << ",\n"
           << "    \"minColoredRatio\": " << contract.m_min_colored_ratio << ",\n"
           << "    \"minLowerHalfSceneRatio\": " << contract.m_min_lower_half_scene_ratio << ",\n"
           << "    \"minNonBackgroundColorBuckets\": " << contract.m_min_non_background_color_buckets << "\n"
           << "  },\n"
           << "  \"sampledPixels\": " << pixels.m_sampled_pixels << ",\n"
           << "  \"scenePixels\": " << pixels.m_scene_pixels << ",\n"
           << "  \"backgroundPixels\": " << pixels.m_background_pixels << ",\n"
           << "  \"groundPixels\": " << pixels.m_ground_pixels << ",\n"
           << "  \"coloredPixels\": " << pixels.m_colored_pixels << ",\n"
           << "  \"lowerHalfSampledPixels\": " << pixels.m_lower_half_sampled_pixels << ",\n"
           << "  \"lowerHalfScenePixels\": " << pixels.m_lower_half_scene_pixels << ",\n"
           << "  \"sceneRatio\": " << pixels.m_scene_ratio << ",\n"
           << "  \"backgroundRatio\": " << pixels.m_background_ratio << ",\n"
           << "  \"groundRatio\": " << pixels.m_ground_ratio << ",\n"
           << "  \"coloredRatio\": " << pixels.m_colored_ratio << ",\n"
           << "  \"lowerHalfSceneRatio\": " << pixels.m_lower_half_scene_ratio << ",\n"
           << "  \"nonBackgroundColorBuckets\": " << pixels.m_non_background_color_buckets << ",\n"
           << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
           << "  \"failureReason\": " << string_or_null(pixels.m_failure_reason) << "\n"
           << "}\n";
}

// Reads the value following an option and reports a clear usage failure.
[[nodiscard]] std::string require_arg_value(int argc, char** argv, int& index, const std::string& name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for " + name + ".");
    }
    index += 1;
    return argv[index];
}

// Parses an unsigned 32-bit command-line value with overflow checking.
[[nodiscard]] std::uint32_t parse_u32(const std::string& value, const char* name) {
    const unsigned long parsed = std::stoul(value);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(name) + " is too large.");
    }
    return static_cast<std::uint32_t>(parsed);
}

// Parses a floating-point command-line value for threshold settings.
[[nodiscard]] double parse_double(const std::string& value) {
    return std::stod(value);
}

// Parses the comma-separated RGBA clear color passed by the Node smoke wrapper.
[[nodiscard]] std::vector<std::uint8_t> parse_clear_color(const std::string& value) {
    std::vector<std::uint8_t> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::uint32_t parsed = parse_u32(item, "clear color channel");
        if (parsed > 255U) {
            throw std::runtime_error("Clear color channel must be <= 255.");
        }
        result.push_back(static_cast<std::uint8_t>(parsed));
    }
    return result;
}

} // namespace

// Parses the smoke contract arguments forwarded by tools/smoke-render-cpp.mjs.
RenderSmokeOptions parse_render_smoke_args(int argc, char** argv) {
    RenderSmokeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--out") {
            options.m_out_dir = require_arg_value(argc, argv, index, arg);
        } else if (arg == "--width") {
            options.m_contract.m_width = parse_u32(require_arg_value(argc, argv, index, arg), "width");
        } else if (arg == "--height") {
            options.m_contract.m_height = parse_u32(require_arg_value(argc, argv, index, arg), "height");
        } else if (arg == "--clear-color-rgba8") {
            options.m_contract.m_clear_color_rgba8 = parse_clear_color(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--sample-step") {
            options.m_contract.m_sample_step = parse_u32(require_arg_value(argc, argv, index, arg), "sample step");
        } else if (arg == "--color-distance-tolerance") {
            options.m_contract.m_color_distance_tolerance = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--bucket-divisor") {
            options.m_contract.m_bucket_divisor =
                parse_u32(require_arg_value(argc, argv, index, arg), "bucket divisor");
        } else if (arg == "--min-scene-ratio") {
            options.m_contract.m_min_scene_ratio = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--min-background-ratio") {
            options.m_contract.m_min_background_ratio = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--min-ground-ratio") {
            options.m_contract.m_min_ground_ratio = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--min-colored-ratio") {
            options.m_contract.m_min_colored_ratio = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--min-lower-half-scene-ratio") {
            options.m_contract.m_min_lower_half_scene_ratio = parse_double(require_arg_value(argc, argv, index, arg));
        } else if (arg == "--min-non-background-color-buckets") {
            options.m_contract.m_min_non_background_color_buckets =
                parse_u32(require_arg_value(argc, argv, index, arg), "min non-background color buckets");
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    validate_contract(options.m_contract);
    return options;
}

// Runs the full native smoke: render, write artifacts, inspect pixels, and fail if needed.
void run_render_smoke(const RenderSmokeOptions& options) {
    std::filesystem::create_directories(options.m_out_dir);
    const std::filesystem::path png_path = options.m_out_dir / "opaque-demo.png";
    const std::filesystem::path report_path = options.m_out_dir / "report.json";

    GpuContext context = create_gpu_context();
    const std::vector<std::uint8_t> pixels = render_and_readback(context, options.m_contract);
    write_rgba_png(png_path, pixels, options.m_contract.m_width, options.m_contract.m_height);
    const PixelReport pixel_report = inspect_pixels(pixels, options.m_contract);
    write_report(png_path, report_path, options.m_contract, context, pixel_report);

    std::cout << "Native C++ render smoke PNG: " << png_path << "\n";
    std::cout << "Native C++ render smoke report: " << report_path << "\n";

    if (!pixel_report.m_failure_reason.empty()) {
        throw std::runtime_error(pixel_report.m_failure_reason);
    }
}

} // namespace ofg::native
