// Native Dawn render-smoke contract and implementation.
//
// This file owns the browser-free renderer validation path for the C++/WASM
// migration. It creates a native Dawn instance/device, renders OFG's bootstrap
// triangle through the shared WebGPU renderer, reads the offscreen texture back
// into CPU memory, writes a PNG, and records the same threshold diagnostics as
// the browser smoke. The native backend is intentionally constrained to Vulkan
// for this Windows migration path so it cannot quietly pass through Dawn's null
// backend.
#include "ofg/native/render_smoke.hpp"

#include "ofg/native/png_writer.hpp"
#include "ofg/render/bootstrap_renderer.hpp"
#include "ofg/render/bootstrap_scene.hpp"
#include "ofg/render/webgpu_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
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

constexpr std::uint32_t kBytesPerPixel = 4;
constexpr std::uint64_t kWaitTimeoutNs = 15'000'000'000ULL;
constexpr WGPUTextureFormat kRenderFormat = WGPUTextureFormat_RGBA8Unorm;

struct PixelReport {
  std::uint64_t sampled_pixels{0};
  std::uint64_t triangle_pixels{0};
  std::uint64_t background_pixels{0};
  double triangle_ratio{0.0};
  double background_ratio{0.0};
  std::uint32_t non_background_color_buckets{0};
  std::string failure_reason;
};

// Owns Dawn handles in release order for the native smoke lifetime.
struct GpuContext {
  WGPUInstance instance{nullptr};
  WGPUAdapter adapter{nullptr};
  WGPUDevice device{nullptr};
  WGPUQueue queue{nullptr};
  std::string adapter_name{"Unavailable"};
  std::string backend{"Unknown"};

  GpuContext() = default;
  GpuContext(const GpuContext&) = delete;
  GpuContext& operator=(const GpuContext&) = delete;

  // Transfers ownership of all Dawn handles from another context.
  GpuContext(GpuContext&& other) noexcept
    : instance(std::exchange(other.instance, nullptr)),
      adapter(std::exchange(other.adapter, nullptr)),
      device(std::exchange(other.device, nullptr)),
      queue(std::exchange(other.queue, nullptr)),
      adapter_name(std::move(other.adapter_name)),
      backend(std::move(other.backend)) {
  }

  // Releases current handles, then takes ownership from another context.
  GpuContext& operator=(GpuContext&& other) noexcept {
    if (this != &other) {
      release();
      instance = std::exchange(other.instance, nullptr);
      adapter = std::exchange(other.adapter, nullptr);
      device = std::exchange(other.device, nullptr);
      queue = std::exchange(other.queue, nullptr);
      adapter_name = std::move(other.adapter_name);
      backend = std::move(other.backend);
    }
    return *this;
  }

  // Releases Dawn handles in queue/device/adapter/instance order.
  ~GpuContext() {
    release();
  }

  // Performs the actual idempotent handle release for destructor and move assignment.
  void release() {
    if (queue != nullptr) {
      wgpuQueueRelease(queue);
      queue = nullptr;
    }
    if (device != nullptr) {
      wgpuDeviceRelease(device);
      device = nullptr;
    }
    if (adapter != nullptr) {
      wgpuAdapterRelease(adapter);
      adapter = nullptr;
    }
    if (instance != nullptr) {
      wgpuInstanceRelease(instance);
      instance = nullptr;
    }
  }
};

// Releases an offscreen render texture created during smoke execution.
struct ScopedTexture {
  WGPUTexture value{nullptr};

  // Releases the temporary offscreen texture.
  ~ScopedTexture() {
    if (value != nullptr) {
      wgpuTextureRelease(value);
    }
  }
};

// Releases the texture view used as the render pass attachment.
struct ScopedTextureView {
  WGPUTextureView value{nullptr};

  // Releases the render-target view.
  ~ScopedTextureView() {
    if (value != nullptr) {
      wgpuTextureViewRelease(value);
    }
  }
};

// Releases the readback buffer after mapped pixels have been copied out.
struct ScopedBuffer {
  WGPUBuffer value{nullptr};

  // Releases the readback buffer after unmap/copy is complete.
  ~ScopedBuffer() {
    if (value != nullptr) {
      wgpuBufferRelease(value);
    }
  }
};

// Releases a command encoder unless ownership has been consumed by Finish.
struct ScopedCommandEncoder {
  WGPUCommandEncoder value{nullptr};

  // Releases an unfinished encoder; finished encoders clear the handle first.
  ~ScopedCommandEncoder() {
    if (value != nullptr) {
      wgpuCommandEncoderRelease(value);
    }
  }
};

// Releases the finished command buffer after queue submission.
struct ScopedCommandBuffer {
  WGPUCommandBuffer value{nullptr};

  // Releases the submitted command buffer.
  ~ScopedCommandBuffer() {
    if (value != nullptr) {
      wgpuCommandBufferRelease(value);
    }
  }
};

// Carries the result of Dawn's asynchronous adapter request callback.
struct AdapterRequest {
  WGPURequestAdapterStatus status{WGPURequestAdapterStatus_Unavailable};
  WGPUAdapter adapter{nullptr};
  std::string message;
};

// Carries the result of Dawn's asynchronous device request callback.
struct DeviceRequest {
  WGPURequestDeviceStatus status{WGPURequestDeviceStatus_Error};
  WGPUDevice device{nullptr};
  std::string message;
};

// Carries the result of Dawn's asynchronous buffer map callback.
struct MapRequest {
  WGPUMapAsyncStatus status{WGPUMapAsyncStatus_Error};
  std::string message;
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
void wait_for_future(
  WGPUInstance instance,
  WGPUFuture future,
  const char* operation
) {
  WGPUFutureWaitInfo wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
  wait_info.future = future;
  const WGPUWaitStatus status =
    wgpuInstanceWaitAny(instance, 1, &wait_info, kWaitTimeoutNs);
  if (status != WGPUWaitStatus_Success || wait_info.completed != WGPU_TRUE) {
    throw std::runtime_error(
      std::string(operation) + " wait failed with status " +
      wait_status_name(status) + "."
    );
  }
}

// Stores the adapter returned by Dawn's requestAdapter callback.
void handle_adapter_request(
  WGPURequestAdapterStatus status,
  WGPUAdapter adapter,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)userdata2;
  auto* request = static_cast<AdapterRequest*>(userdata1);
  request->status = status;
  request->adapter = adapter;
  request->message = gpu::string_from_view(message);
}

// Stores the device returned by Dawn's requestDevice callback.
void handle_device_request(
  WGPURequestDeviceStatus status,
  WGPUDevice device,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)userdata2;
  auto* request = static_cast<DeviceRequest*>(userdata1);
  request->status = status;
  request->device = device;
  request->message = gpu::string_from_view(message);
}

// Stores the mapAsync result after Dawn finishes readback synchronization.
void handle_map_request(
  WGPUMapAsyncStatus status,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)userdata2;
  auto* request = static_cast<MapRequest*>(userdata1);
  request->status = status;
  request->message = gpu::string_from_view(message);
}

// Aligns row pitch to WebGPU's texture-to-buffer copy requirements.
[[nodiscard]] std::uint32_t align_to(
  std::uint32_t value,
  std::uint32_t alignment
) {
  return ((value + alignment - 1U) / alignment) * alignment;
}

// Computes RGB distance while ignoring alpha, matching the browser smoke.
[[nodiscard]] double color_distance(
  const std::array<std::uint8_t, 4>& left,
  const std::vector<std::uint8_t>& right
) {
  const double dr = static_cast<double>(left[0]) - right[0];
  const double dg = static_cast<double>(left[1]) - right[1];
  const double db = static_cast<double>(left[2]) - right[2];
  return std::sqrt(dr * dr + dg * dg + db * db);
}

// Samples the rendered pixels and checks them against the shared smoke thresholds.
[[nodiscard]] PixelReport inspect_pixels(
  const std::vector<std::uint8_t>& pixels,
  const SmokeContract& contract
) {
  const std::size_t expected_size =
    static_cast<std::size_t>(contract.width) * contract.height * kBytesPerPixel;
  if (pixels.size() != expected_size) {
    throw std::runtime_error(
      "Expected " + std::to_string(expected_size) +
      " RGBA bytes for inspection, got " + std::to_string(pixels.size()) + "."
    );
  }

  // Bucket non-background colors coarsely to tolerate interpolation variance.
  PixelReport report{};
  std::set<std::string> buckets;
  for (std::uint32_t y = 0; y < contract.height; y += contract.sample_step) {
    for (std::uint32_t x = 0; x < contract.width; x += contract.sample_step) {
      const std::size_t index =
        (static_cast<std::size_t>(contract.width) * y + x) * kBytesPerPixel;
      const std::array<std::uint8_t, 4> pixel{
        pixels[index],
        pixels[index + 1U],
        pixels[index + 2U],
        pixels[index + 3U]
      };
      if (color_distance(pixel, contract.clear_color_rgba8) <=
          contract.color_distance_tolerance) {
        report.background_pixels += 1U;
      } else {
        report.triangle_pixels += 1U;
        buckets.insert(
          std::to_string(pixel[0] / contract.bucket_divisor) + ":" +
          std::to_string(pixel[1] / contract.bucket_divisor) + ":" +
          std::to_string(pixel[2] / contract.bucket_divisor)
        );
      }
    }
  }

  // Convert counts into ratios and make exactly one failure reason visible.
  report.sampled_pixels = report.background_pixels + report.triangle_pixels;
  report.triangle_ratio =
    static_cast<double>(report.triangle_pixels) / report.sampled_pixels;
  report.background_ratio =
    static_cast<double>(report.background_pixels) / report.sampled_pixels;
  report.non_background_color_buckets =
    static_cast<std::uint32_t>(buckets.size());

  if (report.triangle_ratio < contract.min_triangle_ratio) {
    report.failure_reason =
      "Triangle coverage too low: " + std::to_string(report.triangle_ratio);
  } else if (report.background_ratio < contract.min_background_ratio) {
    report.failure_reason =
      "Background coverage too low: " + std::to_string(report.background_ratio);
  } else if (
    report.non_background_color_buckets <
    contract.min_non_background_color_buckets
  ) {
    report.failure_reason =
      "Expected at least " +
      std::to_string(contract.min_non_background_color_buckets) +
      " non-background color buckets; got " +
      std::to_string(report.non_background_color_buckets) + ".";
  }

  return report;
}

// Validates contract values before the expensive Dawn path starts.
void validate_contract(const SmokeContract& contract) {
  if (contract.width == 0 || contract.height == 0) {
    throw std::runtime_error("Smoke contract dimensions must be non-zero.");
  }
  if (contract.clear_color_rgba8.size() != 4U) {
    throw std::runtime_error("Smoke contract clear color must have four channels.");
  }
  if (contract.sample_step == 0) {
    throw std::runtime_error("Smoke contract sample step must be non-zero.");
  }
  if (contract.bucket_divisor == 0) {
    throw std::runtime_error("Smoke contract bucket divisor must be non-zero.");
  }
}

// Creates the native Dawn instance, Vulkan adapter, device, and queue.
[[nodiscard]] GpuContext create_gpu_context() {
  GpuContext context;

  // Timed waits are required so request/map futures can fail with a timeout.
  const WGPUInstanceFeatureName instance_feature =
    WGPUInstanceFeatureName_TimedWaitAny;
  WGPUInstanceLimits instance_limits = WGPU_INSTANCE_LIMITS_INIT;
  instance_limits.timedWaitAnyMaxCount = 1;
  WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
  instance_descriptor.requiredFeatureCount = 1;
  instance_descriptor.requiredFeatures = &instance_feature;
  instance_descriptor.requiredLimits = &instance_limits;
  context.instance = wgpuCreateInstance(&instance_descriptor);
  if (context.instance == nullptr) {
    throw std::runtime_error("wgpuCreateInstance returned null.");
  }

  // Request a real Vulkan adapter; null backend is not acceptable for pixels.
  WGPURequestAdapterOptions adapter_options =
    WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
  adapter_options.backendType = WGPUBackendType_Vulkan;

  AdapterRequest adapter_request;
  WGPURequestAdapterCallbackInfo adapter_callback =
    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  adapter_callback.mode = WGPUCallbackMode_WaitAnyOnly;
  adapter_callback.callback = handle_adapter_request;
  adapter_callback.userdata1 = &adapter_request;
  wait_for_future(
    context.instance,
    wgpuInstanceRequestAdapter(
      context.instance,
      &adapter_options,
      adapter_callback
    ),
    "requestAdapter"
  );
  if (
    adapter_request.status != WGPURequestAdapterStatus_Success ||
    adapter_request.adapter == nullptr
  ) {
    throw std::runtime_error(
      "requestAdapter failed with status " +
      adapter_status_name(adapter_request.status) + ": " +
      adapter_request.message
    );
  }
  context.adapter = adapter_request.adapter;

  // Capture adapter diagnostics before creating the device.
  WGPUAdapterInfo adapter_info = WGPU_ADAPTER_INFO_INIT;
  if (wgpuAdapterGetInfo(context.adapter, &adapter_info) == WGPUStatus_Success) {
    context.adapter_name = gpu::string_from_view(adapter_info.device);
    if (context.adapter_name.empty()) {
      context.adapter_name = gpu::string_from_view(adapter_info.description);
    }
    if (context.adapter_name.empty()) {
      context.adapter_name = gpu::string_from_view(adapter_info.vendor);
    }
    context.backend = gpu::backend_type_name(adapter_info.backendType);
    wgpuAdapterInfoFreeMembers(adapter_info);
  }
  if (context.backend != "Vulkan") {
    throw std::runtime_error(
      "Native render smoke requires a real Vulkan Dawn backend; got " +
      context.backend + "."
    );
  }

  // Create the WebGPU device used by the shared bootstrap renderer.
  WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
  device_descriptor.label = gpu::cstring_view("OFG native render-smoke device");
  device_descriptor.defaultQueue.label =
    gpu::cstring_view("OFG native render-smoke queue");

  DeviceRequest device_request;
  WGPURequestDeviceCallbackInfo device_callback =
    WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  device_callback.mode = WGPUCallbackMode_WaitAnyOnly;
  device_callback.callback = handle_device_request;
  device_callback.userdata1 = &device_request;
  wait_for_future(
    context.instance,
    wgpuAdapterRequestDevice(context.adapter, &device_descriptor, device_callback),
    "requestDevice"
  );
  if (
    device_request.status != WGPURequestDeviceStatus_Success ||
    device_request.device == nullptr
  ) {
    throw std::runtime_error(
      "requestDevice failed with status " +
      device_status_name(device_request.status) + ": " +
      device_request.message
    );
  }

  context.device = device_request.device;
  context.queue = wgpuDeviceGetQueue(context.device);
  if (context.queue == nullptr) {
    throw std::runtime_error("wgpuDeviceGetQueue returned null.");
  }
  return context;
}

// Renders the bootstrap triangle offscreen and returns tightly packed RGBA pixels.
[[nodiscard]] std::vector<std::uint8_t> render_and_readback(
  GpuContext& context,
  const SmokeContract& contract
) {
  // Build the same durable renderer resources used by the browser path.
  std::string renderer_error;
  std::unique_ptr<BootstrapRenderer> renderer =
    BootstrapRenderer::create(
      context.device,
      context.queue,
      kRenderFormat,
      renderer_error
    );
  if (!renderer) {
    throw std::runtime_error(renderer_error);
  }

  // Render into a copyable offscreen texture.
  WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  texture_descriptor.label =
    gpu::cstring_view("OFG native render-smoke texture");
  texture_descriptor.usage =
    WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
  texture_descriptor.dimension = WGPUTextureDimension_2D;
  texture_descriptor.size =
    WGPUExtent3D{contract.width, contract.height, 1};
  texture_descriptor.format = kRenderFormat;
  ScopedTexture texture{
    wgpuDeviceCreateTexture(context.device, &texture_descriptor)
  };
  if (texture.value == nullptr) {
    throw std::runtime_error("wgpuDeviceCreateTexture returned null.");
  }

  ScopedTextureView view{wgpuTextureCreateView(texture.value, nullptr)};
  if (view.value == nullptr) {
    throw std::runtime_error("wgpuTextureCreateView returned null.");
  }

  // WebGPU readback rows must be 256-byte aligned.
  const std::uint32_t unpadded_bytes_per_row =
    contract.width * kBytesPerPixel;
  const std::uint32_t padded_bytes_per_row =
    align_to(unpadded_bytes_per_row, 256);
  const std::uint64_t readback_size =
    static_cast<std::uint64_t>(padded_bytes_per_row) * contract.height;

  WGPUBufferDescriptor buffer_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
  buffer_descriptor.label =
    gpu::cstring_view("OFG native render-smoke readback buffer");
  buffer_descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
  buffer_descriptor.size = readback_size;
  buffer_descriptor.mappedAtCreation = WGPU_FALSE;
  ScopedBuffer readback{
    wgpuDeviceCreateBuffer(context.device, &buffer_descriptor)
  };
  if (readback.value == nullptr) {
    throw std::runtime_error("wgpuDeviceCreateBuffer returned null.");
  }

  // Encode the render pass and a texture-to-buffer copy in one command buffer.
  WGPUCommandEncoderDescriptor encoder_descriptor =
    WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encoder_descriptor.label =
    gpu::cstring_view("OFG native render-smoke encoder");
  ScopedCommandEncoder encoder{
    wgpuDeviceCreateCommandEncoder(context.device, &encoder_descriptor)
  };
  if (encoder.value == nullptr) {
    throw std::runtime_error("wgpuDeviceCreateCommandEncoder returned null.");
  }

  if (!renderer->render_to_view(encoder.value, view.value, renderer_error)) {
    throw std::runtime_error(renderer_error);
  }

  WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  source.texture = texture.value;
  source.mipLevel = 0;
  source.origin = WGPUOrigin3D{0, 0, 0};
  source.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
  destination.buffer = readback.value;
  destination.layout.offset = 0;
  destination.layout.bytesPerRow = padded_bytes_per_row;
  destination.layout.rowsPerImage = contract.height;

  const WGPUExtent3D copy_size{contract.width, contract.height, 1};
  wgpuCommandEncoderCopyTextureToBuffer(
    encoder.value,
    &source,
    &destination,
    &copy_size
  );

  WGPUCommandBufferDescriptor command_descriptor =
    WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  command_descriptor.label =
    gpu::cstring_view("OFG native render-smoke commands");
  ScopedCommandBuffer command{
    wgpuCommandEncoderFinish(encoder.value, &command_descriptor)
  };
  if (command.value == nullptr) {
    throw std::runtime_error("wgpuCommandEncoderFinish returned null.");
  }

  wgpuQueueSubmit(context.queue, 1, &command.value);

  // Map the readback buffer after submission and fail with a finite timeout.
  MapRequest map_request;
  WGPUBufferMapCallbackInfo map_callback =
    WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
  map_callback.mode = WGPUCallbackMode_WaitAnyOnly;
  map_callback.callback = handle_map_request;
  map_callback.userdata1 = &map_request;
  wait_for_future(
    context.instance,
    wgpuBufferMapAsync(
      readback.value,
      WGPUMapMode_Read,
      0,
      static_cast<std::size_t>(readback_size),
      map_callback
    ),
    "mapAsync"
  );
  if (map_request.status != WGPUMapAsyncStatus_Success) {
    throw std::runtime_error(
      "mapAsync failed with status " + map_status_name(map_request.status) +
      ": " + map_request.message
    );
  }

  const auto* mapped = static_cast<const std::uint8_t*>(
    wgpuBufferGetConstMappedRange(
      readback.value,
      0,
      static_cast<std::size_t>(readback_size)
    )
  );
  if (mapped == nullptr) {
    throw std::runtime_error("wgpuBufferGetConstMappedRange returned null.");
  }

  // Remove GPU row padding so the PNG writer receives tightly packed RGBA rows.
  std::vector<std::uint8_t> pixels(
    static_cast<std::size_t>(unpadded_bytes_per_row) * contract.height
  );
  for (std::uint32_t row = 0; row < contract.height; ++row) {
    const std::size_t src_start =
      static_cast<std::size_t>(row) * padded_bytes_per_row;
    const std::size_t dst_start =
      static_cast<std::size_t>(row) * unpadded_bytes_per_row;
    std::copy_n(
      mapped + src_start,
      unpadded_bytes_per_row,
      pixels.data() + dst_start
    );
  }
  wgpuBufferUnmap(readback.value);

  return pixels;
}

// Writes the JSON report expected by the native render-smoke contract.
void write_report(
  const std::filesystem::path& png_path,
  const std::filesystem::path& report_path,
  const SmokeContract& contract,
  const GpuContext& context,
  const PixelReport& pixels
) {
  // Keep field names compatible with the original native smoke report.
  const bool passed = pixels.failure_reason.empty();
  std::ofstream report(report_path);
  if (!report) {
    throw std::runtime_error(
      "Could not open native render-smoke report path: " +
      report_path.string()
    );
  }

  report
    << "{\n"
    << "  \"pngPath\": \"" << json_escape(png_path.generic_string()) << "\",\n"
    << "  \"reportPath\": \"" << json_escape(report_path.generic_string()) << "\",\n"
    << "  \"width\": " << contract.width << ",\n"
    << "  \"height\": " << contract.height << ",\n"
    << "  \"textureFormat\": \"" << gpu::texture_format_name(kRenderFormat) << "\",\n"
    << "  \"adapterName\": \"" << json_escape(context.adapter_name) << "\",\n"
    << "  \"backend\": \"" << json_escape(context.backend) << "\",\n"
    << "  \"clearColor\": [\n"
    << "    " << static_cast<int>(contract.clear_color_rgba8[0]) << ",\n"
    << "    " << static_cast<int>(contract.clear_color_rgba8[1]) << ",\n"
    << "    " << static_cast<int>(contract.clear_color_rgba8[2]) << ",\n"
    << "    " << static_cast<int>(contract.clear_color_rgba8[3]) << "\n"
    << "  ],\n"
    << "  \"thresholds\": {\n"
    << "    \"sampleStep\": " << contract.sample_step << ",\n"
    << "    \"colorDistanceTolerance\": "
    << contract.color_distance_tolerance << ",\n"
    << "    \"bucketDivisor\": " << contract.bucket_divisor << ",\n"
    << "    \"minTriangleRatio\": " << contract.min_triangle_ratio << ",\n"
    << "    \"minBackgroundRatio\": " << contract.min_background_ratio << ",\n"
    << "    \"minNonBackgroundColorBuckets\": "
    << contract.min_non_background_color_buckets << "\n"
    << "  },\n"
    << "  \"sampledPixels\": " << pixels.sampled_pixels << ",\n"
    << "  \"trianglePixels\": " << pixels.triangle_pixels << ",\n"
    << "  \"backgroundPixels\": " << pixels.background_pixels << ",\n"
    << "  \"triangleRatio\": " << pixels.triangle_ratio << ",\n"
    << "  \"backgroundRatio\": " << pixels.background_ratio << ",\n"
    << "  \"nonBackgroundColorBuckets\": "
    << pixels.non_background_color_buckets << ",\n"
    << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
    << "  \"failureReason\": " << string_or_null(pixels.failure_reason) << "\n"
    << "}\n";
}

// Reads the value following an option and reports a clear usage failure.
[[nodiscard]] std::string require_arg_value(
  int argc,
  char** argv,
  int& index,
  const std::string& name
) {
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
[[nodiscard]] std::vector<std::uint8_t> parse_clear_color(
  const std::string& value
) {
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
      options.out_dir = require_arg_value(argc, argv, index, arg);
    } else if (arg == "--width") {
      options.contract.width =
        parse_u32(require_arg_value(argc, argv, index, arg), "width");
    } else if (arg == "--height") {
      options.contract.height =
        parse_u32(require_arg_value(argc, argv, index, arg), "height");
    } else if (arg == "--clear-color-rgba8") {
      options.contract.clear_color_rgba8 =
        parse_clear_color(require_arg_value(argc, argv, index, arg));
    } else if (arg == "--sample-step") {
      options.contract.sample_step =
        parse_u32(require_arg_value(argc, argv, index, arg), "sample step");
    } else if (arg == "--color-distance-tolerance") {
      options.contract.color_distance_tolerance =
        parse_double(require_arg_value(argc, argv, index, arg));
    } else if (arg == "--bucket-divisor") {
      options.contract.bucket_divisor =
        parse_u32(require_arg_value(argc, argv, index, arg), "bucket divisor");
    } else if (arg == "--min-triangle-ratio") {
      options.contract.min_triangle_ratio =
        parse_double(require_arg_value(argc, argv, index, arg));
    } else if (arg == "--min-background-ratio") {
      options.contract.min_background_ratio =
        parse_double(require_arg_value(argc, argv, index, arg));
    } else if (arg == "--min-non-background-color-buckets") {
      options.contract.min_non_background_color_buckets = parse_u32(
        require_arg_value(argc, argv, index, arg),
        "min non-background color buckets"
      );
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }
  validate_contract(options.contract);
  return options;
}

// Runs the full native smoke: render, write artifacts, inspect pixels, and fail if needed.
void run_render_smoke(const RenderSmokeOptions& options) {
  std::filesystem::create_directories(options.out_dir);
  const std::filesystem::path png_path = options.out_dir / "bootstrap.png";
  const std::filesystem::path report_path = options.out_dir / "report.json";

  GpuContext context = create_gpu_context();
  const std::vector<std::uint8_t> pixels =
    render_and_readback(context, options.contract);
  write_rgba_png(
    png_path,
    pixels,
    options.contract.width,
    options.contract.height
  );
  const PixelReport pixel_report = inspect_pixels(pixels, options.contract);
  write_report(png_path, report_path, options.contract, context, pixel_report);

  std::cout << "Native C++ render smoke PNG: " << png_path << "\n";
  std::cout << "Native C++ render smoke report: " << report_path << "\n";

  if (!pixel_report.failure_reason.empty()) {
    throw std::runtime_error(pixel_report.failure_reason);
  }
}

} // namespace ofg::native
