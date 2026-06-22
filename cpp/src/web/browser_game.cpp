// Browser-facing C++ runtime facade and WebGPU lifecycle owner.
#include "ofg/web/browser_game.hpp"
#include "ofg/web/webgpu_utils.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace ofg {

#ifdef __EMSCRIPTEN__
struct BrowserGameWebGpuCallbackContext {
  std::weak_ptr<BrowserGame> game;
};

namespace {

// Formats numeric validation failures consistently for runtime status JSON.
std::string number_message(const char* label, double value) {
  std::ostringstream out;
  out << label << " must be a non-negative integer within uint32 range, got " << value << ".";
  return out.str();
}

// Converts JavaScript numeric dimensions into the uint32 WebGPU size domain.
std::optional<std::uint32_t> parse_dimension(
  const char* label,
  double value,
  std::string& error
) {
  if (!std::isfinite(value) || value < 0.0 || std::trunc(value) != value) {
    error = number_message(label, value);
    return std::nullopt;
  }
  constexpr double max_dimension =
    static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  if (value > max_dimension) {
    error = number_message(label, value);
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

// Reclaims callback heap context exactly once when Emdawn invokes a callback.
std::unique_ptr<BrowserGameWebGpuCallbackContext> consume_context(void* context) {
  return std::unique_ptr<BrowserGameWebGpuCallbackContext>(
    static_cast<BrowserGameWebGpuCallbackContext*>(context)
  );
}

// Ensures the canvas has an id and returns the selector required by Emdawn.
std::string ensure_canvas_selector(emscripten::val canvas) {
  static std::uint64_t next_canvas_id = 0;

  std::string id = canvas["id"].as<std::string>();
  if (id.empty()) {
    next_canvas_id += 1;
    id = "ofg-cpp-canvas-" + std::to_string(next_canvas_id);
    canvas.set("id", id);
  }
  return "#" + id;
}

} // namespace

// Captures the canvas selector used to create the browser WebGPU surface.
BrowserGame::BrowserGame(emscripten::val canvas)
  : canvas_selector_(ensure_canvas_selector(canvas)) {
}

// Releases browser WebGPU handles if the TypeScript wrapper forgets dispose().
BrowserGame::~BrowserGame() {
  release_webgpu();
}

// Creates the Embind-facing runtime and starts asynchronous WebGPU setup.
std::shared_ptr<BrowserGame> BrowserGame::create(emscripten::val canvas) {
  std::shared_ptr<BrowserGame> game = std::make_shared<BrowserGame>(canvas);
  game->start_webgpu_initialization(game);
  return game;
}
#endif

// Receives physical canvas dimensions from the TypeScript host.
void BrowserGame::resize(
  double width,
  double height,
  double device_pixel_ratio
) {
  if (disposed_) {
    record_error("Browser game runtime has been disposed.");
    return;
  }

  std::string error;
  const std::optional<std::uint32_t> parsed_width =
    parse_dimension("Canvas width", width, error);
  if (!parsed_width.has_value()) {
    record_error(error);
    return;
  }
  const std::optional<std::uint32_t> parsed_height =
    parse_dimension("Canvas height", height, error);
  if (!parsed_height.has_value()) {
    record_error(error);
    return;
  }

  if (game_ != nullptr) {
    if (game_->resize(
          *parsed_width,
          *parsed_height,
          device_pixel_ratio,
          error
        )) {
      pending_width_ = *parsed_width;
      pending_height_ = *parsed_height;
      pending_device_pixel_ratio_ = device_pixel_ratio;
      has_pending_size_ = true;
#ifdef __EMSCRIPTEN__
      configure_surface_if_ready();
#endif
    }
    return;
  }

  if (setup_runtime_.resize(
        *parsed_width,
        *parsed_height,
        device_pixel_ratio,
        error
      )) {
    pending_width_ = *parsed_width;
    pending_height_ = *parsed_height;
    pending_device_pixel_ratio_ = device_pixel_ratio;
    has_pending_size_ = true;
  }
}

// Advances runtime state and renders a frame when WebGPU is initialized.
void BrowserGame::frame(double time_ms) {
#ifdef __EMSCRIPTEN__
  if (instance_ != nullptr) {
    wgpuInstanceProcessEvents(instance_);
  }
#endif
  if (disposed_) {
    record_error("Browser game runtime has been disposed.");
    return;
  }

  std::string error;
  if (game_ != nullptr) {
    if (game_->tick(time_ms, error)) {
#ifdef __EMSCRIPTEN__
      render_frame_if_ready();
#endif
    }
    return;
  }

  (void)setup_runtime_.tick(time_ms, error);
}

// Returns the browser-facing debug-status JSON payload.
std::string BrowserGame::debug_status_json() const {
  if (game_ != nullptr) {
    return game_->debug_status_json();
  }
  return setup_runtime_.debug_status_json();
}

// Releases WebGPU resources and makes later lifecycle calls fail clearly.
void BrowserGame::dispose() {
#ifdef __EMSCRIPTEN__
  release_webgpu();
#endif
  disposed_ = true;
  setup_runtime_.dispose();
}

#ifdef __EMSCRIPTEN__
// Creates instance/surface state and starts requestAdapter.
void BrowserGame::start_webgpu_initialization(std::weak_ptr<BrowserGame> self) {
  WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
  instance_ = wgpuCreateInstance(&instance_descriptor);
  if (instance_ == nullptr) {
    record_gpu_error("wgpuCreateInstance returned null.");
    return;
  }

  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source =
    WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
  canvas_source.selector = webgpu::string_view(canvas_selector_);

  WGPUSurfaceDescriptor surface_descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
  surface_descriptor.nextInChain = &canvas_source.chain;
  surface_descriptor.label = webgpu::cstring_view("OFG C++ WebGPU canvas surface");
  surface_ = wgpuInstanceCreateSurface(instance_, &surface_descriptor);
  if (surface_ == nullptr) {
    record_gpu_error("wgpuInstanceCreateSurface returned null.");
    return;
  }

  WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  options.powerPreference = WGPUPowerPreference_HighPerformance;
  options.compatibleSurface = surface_;

  WGPURequestAdapterCallbackInfo callback_info =
    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  callback_info.mode = WGPUCallbackMode_AllowSpontaneous;
  callback_info.callback = &BrowserGame::handle_adapter_request;
  callback_info.userdata1 =
    new BrowserGameWebGpuCallbackContext{std::move(self)};

  (void)wgpuInstanceRequestAdapter(instance_, &options, callback_info);
}

// Starts requestDevice after an adapter has been accepted.
void BrowserGame::request_device(std::weak_ptr<BrowserGame> self) {
  WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
  descriptor.label = webgpu::cstring_view("OFG C++ WebGPU device");
  descriptor.defaultQueue.label = webgpu::cstring_view("OFG C++ WebGPU queue");
  device_event_context_ =
    std::make_unique<BrowserGameWebGpuCallbackContext>(
      BrowserGameWebGpuCallbackContext{self}
    );
  descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  descriptor.deviceLostCallbackInfo.callback = &BrowserGame::handle_device_lost;
  descriptor.deviceLostCallbackInfo.userdata1 = device_event_context_.get();
  descriptor.uncapturedErrorCallbackInfo.callback =
    &BrowserGame::handle_uncaptured_error;
  descriptor.uncapturedErrorCallbackInfo.userdata1 = device_event_context_.get();

  WGPURequestDeviceCallbackInfo callback_info = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  callback_info.mode = WGPUCallbackMode_AllowSpontaneous;
  callback_info.callback = &BrowserGame::handle_device_request;
  callback_info.userdata1 =
    new BrowserGameWebGpuCallbackContext{std::move(self)};

  (void)wgpuAdapterRequestDevice(adapter_, &descriptor, callback_info);
}

// Handles requestAdapter completion on the owning runtime instance.
void BrowserGame::on_adapter_request(
  WGPURequestAdapterStatus status,
  WGPUAdapter adapter,
  WGPUStringView message,
  std::weak_ptr<BrowserGame> self
) {
  if (disposed_) {
    if (adapter != nullptr) {
      wgpuAdapterRelease(adapter);
    }
    return;
  }

  if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
    record_gpu_error(
      webgpu::failure_message(
        "requestAdapter",
        webgpu::request_adapter_status_name(status),
        message
      )
    );
    return;
  }

  adapter_ = adapter;
  request_device(std::move(self));
}

// Handles requestDevice completion and creates durable renderer resources.
void BrowserGame::on_device_request(
  WGPURequestDeviceStatus status,
  WGPUDevice device,
  WGPUStringView message
) {
  if (disposed_) {
    if (device != nullptr) {
      wgpuDeviceRelease(device);
    }
    return;
  }

  if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
    record_gpu_error(
      webgpu::failure_message(
        "requestDevice",
        webgpu::request_device_status_name(status),
        message
      )
    );
    return;
  }

  device_ = device;
  queue_ = wgpuDeviceGetQueue(device_);
  if (queue_ == nullptr) {
    record_gpu_error("wgpuDeviceGetQueue returned null.");
    return;
  }

  // Choose the final surface format before creating renderer resources.
  WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
  const WGPUStatus capabilities_status =
    wgpuSurfaceGetCapabilities(surface_, adapter_, &capabilities);
  if (capabilities_status != WGPUStatus_Success || capabilities.formatCount == 0) {
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    record_gpu_error("wgpuSurfaceGetCapabilities returned no formats.");
    return;
  }

  surface_format_ = webgpu::choose_surface_format(capabilities);
  wgpuSurfaceCapabilitiesFreeMembers(capabilities);
  if (surface_format_ == WGPUTextureFormat_Undefined) {
    record_gpu_error("No usable WebGPU surface format was found.");
    return;
  }

  // Create the shared Game once after browser device and format selection.
  std::string game_error;
  game_ = Game::create(
    GpuContext{
      device_,
      queue_,
      webgpu::adapter_name_from_info(adapter_),
      "BrowserWebGpu"
    },
    surface_format_,
    game_error
  );
  if (!game_) {
    record_gpu_error(std::move(game_error));
    return;
  }

  if (apply_pending_resize_to_game()) {
    configure_surface_if_ready();
  }
}

// Records device-loss callbacks into runtime debug status.
void BrowserGame::on_device_lost(
  WGPUDeviceLostReason reason,
  WGPUStringView message
) {
  if (disposed_) {
    return;
  }
  record_gpu_error(
    webgpu::failure_message(
      "device lost",
      webgpu::device_lost_reason_name(reason),
      message
    )
  );
}

// Records uncaptured WebGPU errors into runtime debug status.
void BrowserGame::on_uncaptured_error(WGPUErrorType type, WGPUStringView message) {
  if (disposed_) {
    return;
  }
  record_gpu_error(
    webgpu::failure_message(
      "uncaptured WebGPU error",
      webgpu::error_type_name(type),
      message
    )
  );
}

// Configures or unconfigures the surface to match the current canvas size.
void BrowserGame::configure_surface_if_ready() {
  if (
    game_ == nullptr ||
    surface_ == nullptr ||
    device_ == nullptr ||
    surface_format_ == WGPUTextureFormat_Undefined ||
    disposed_
  ) {
    return;
  }

  const RuntimeDebugStatus& status = game_->status();
  if (status.canvas_width == 0 || status.canvas_height == 0) {
    if (surface_configured_) {
      wgpuSurfaceUnconfigure(surface_);
      surface_configured_ = false;
      configured_width_ = 0;
      configured_height_ = 0;
    }
    return;
  }

  if (
    surface_configured_ &&
    configured_width_ == status.canvas_width &&
    configured_height_ == status.canvas_height
  ) {
    return;
  }

  // Emdawn presents from requestAnimationFrame; no explicit Present call is used.
  WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
  config.device = device_;
  config.format = surface_format_;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = status.canvas_width;
  config.height = status.canvas_height;
  config.alphaMode = WGPUCompositeAlphaMode_Opaque;
  config.presentMode = WGPUPresentMode_Fifo;

  wgpuSurfaceConfigure(surface_, &config);
  surface_configured_ = true;
  configured_width_ = status.canvas_width;
  configured_height_ = status.canvas_height;
}

// Acquires the current surface texture and submits one bootstrap draw.
void BrowserGame::render_frame_if_ready() {
  if (
    game_ == nullptr ||
    surface_ == nullptr ||
    device_ == nullptr ||
    queue_ == nullptr ||
    !surface_configured_
  ) {
    return;
  }

  const RuntimeDebugStatus& status = game_->status();
  if (status.canvas_width == 0 || status.canvas_height == 0) {
    return;
  }

  // Surface acquisition is the only browser-owned texture step per frame.
  WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(surface_, &surface_texture);
  const bool texture_ready =
    surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
    surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
  if (!texture_ready || surface_texture.texture == nullptr) {
    if (surface_texture.texture != nullptr) {
      wgpuTextureRelease(surface_texture.texture);
    }
    const std::string message =
      "wgpuSurfaceGetCurrentTexture failed with status " +
      webgpu::surface_texture_status_name(surface_texture.status) + ".";
    if (
      surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
      surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated
    ) {
      record_error(message);
      if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated) {
        surface_configured_ = false;
        configured_width_ = 0;
        configured_height_ = 0;
        configure_surface_if_ready();
      }
    } else {
      record_gpu_error(message);
    }
    return;
  }

  WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, nullptr);
  if (view == nullptr) {
    wgpuTextureRelease(surface_texture.texture);
    record_gpu_error("wgpuTextureCreateView returned null.");
    return;
  }

  // Encode exactly one clear+triangle pass and submit it to the device queue.
  WGPUCommandEncoderDescriptor encoder_descriptor =
    WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encoder_descriptor.label = webgpu::cstring_view("OFG C++ bootstrap encoder");
  WGPUCommandEncoder encoder =
    wgpuDeviceCreateCommandEncoder(device_, &encoder_descriptor);
  if (encoder == nullptr) {
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surface_texture.texture);
    record_gpu_error("wgpuDeviceCreateCommandEncoder returned null.");
    return;
  }

  std::string render_error;
  if (!game_->render(
        encoder,
        RenderTarget{view, surface_format_, status.canvas_width, status.canvas_height},
        render_error
      )) {
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surface_texture.texture);
    return;
  }

  WGPUCommandBufferDescriptor command_descriptor =
    WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  command_descriptor.label = webgpu::cstring_view("OFG C++ bootstrap commands");
  WGPUCommandBuffer command =
    wgpuCommandEncoderFinish(encoder, &command_descriptor);
  if (command == nullptr) {
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surface_texture.texture);
    record_gpu_error("wgpuCommandEncoderFinish returned null.");
    return;
  }

  wgpuQueueSubmit(queue_, 1, &command);

  // Release every per-frame handle after submission; durable resources stay put.
  wgpuCommandBufferRelease(command);
  wgpuCommandEncoderRelease(encoder);
  wgpuTextureViewRelease(view);
  wgpuTextureRelease(surface_texture.texture);
}

// Applies the latest accepted browser size to Game after async setup finishes.
bool BrowserGame::apply_pending_resize_to_game() {
  if (game_ == nullptr || !has_pending_size_) {
    return game_ != nullptr;
  }

  std::string error;
  return game_->resize(
    pending_width_,
    pending_height_,
    pending_device_pixel_ratio_,
    error
  );
}

// Records a recoverable platform error in the active status owner.
void BrowserGame::record_error(std::string message) {
  if (game_ != nullptr) {
    (void)game_->record_error(std::move(message));
    return;
  }
  (void)setup_runtime_.mark_error(std::move(message));
}

// Records a GPU/device error in the active status owner.
void BrowserGame::record_gpu_error(std::string message) {
  if (game_ != nullptr) {
    (void)game_->record_gpu_error(std::move(message));
    return;
  }
  (void)setup_runtime_.mark_gpu_error(std::move(message));
}

// Releases all WebGPU handles in dependency order.
void BrowserGame::release_webgpu() {
  if (surface_ != nullptr && surface_configured_) {
    wgpuSurfaceUnconfigure(surface_);
  }
  surface_configured_ = false;
  configured_width_ = 0;
  configured_height_ = 0;
  if (game_ != nullptr) {
    game_->dispose();
    game_.reset();
  }

  if (queue_ != nullptr) {
    wgpuQueueRelease(queue_);
    queue_ = nullptr;
  }
  if (device_ != nullptr) {
    wgpuDeviceRelease(device_);
    device_ = nullptr;
  }
  device_event_context_.reset();
  if (adapter_ != nullptr) {
    wgpuAdapterRelease(adapter_);
    adapter_ = nullptr;
  }
  if (surface_ != nullptr) {
    wgpuSurfaceRelease(surface_);
    surface_ = nullptr;
  }
  if (instance_ != nullptr) {
    wgpuInstanceRelease(instance_);
    instance_ = nullptr;
  }
  surface_format_ = WGPUTextureFormat_Undefined;
}

// Bridges the C requestAdapter callback back to the BrowserGame instance.
void BrowserGame::handle_adapter_request(
  WGPURequestAdapterStatus status,
  WGPUAdapter adapter,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)userdata2;
  std::unique_ptr<BrowserGameWebGpuCallbackContext> context =
    consume_context(userdata1);
  std::weak_ptr<BrowserGame> self = context->game;
  std::shared_ptr<BrowserGame> game = self.lock();
  if (!game) {
    if (adapter != nullptr) {
      wgpuAdapterRelease(adapter);
    }
    return;
  }
  game->on_adapter_request(status, adapter, message, std::move(self));
}

// Bridges the C requestDevice callback back to the BrowserGame instance.
void BrowserGame::handle_device_request(
  WGPURequestDeviceStatus status,
  WGPUDevice device,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)userdata2;
  std::unique_ptr<BrowserGameWebGpuCallbackContext> context =
    consume_context(userdata1);
  std::shared_ptr<BrowserGame> game = context->game.lock();
  if (!game) {
    if (device != nullptr) {
      wgpuDeviceRelease(device);
    }
    return;
  }
  game->on_device_request(status, device, message);
}

// Bridges the C device-lost callback back to the BrowserGame instance.
void BrowserGame::handle_device_lost(
  WGPUDevice const* device,
  WGPUDeviceLostReason reason,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)device;
  (void)userdata2;
  auto* context = static_cast<BrowserGameWebGpuCallbackContext*>(userdata1);
  if (context == nullptr) {
    return;
  }
  std::shared_ptr<BrowserGame> game = context->game.lock();
  if (!game) {
    return;
  }
  game->on_device_lost(reason, message);
}

// Bridges uncaptured WebGPU errors back to the BrowserGame instance.
void BrowserGame::handle_uncaptured_error(
  WGPUDevice const* device,
  WGPUErrorType type,
  WGPUStringView message,
  void* userdata1,
  void* userdata2
) {
  (void)device;
  (void)userdata2;
  auto* context = static_cast<BrowserGameWebGpuCallbackContext*>(userdata1);
  if (context == nullptr) {
    return;
  }
  std::shared_ptr<BrowserGame> game = context->game.lock();
  if (!game) {
    return;
  }
  game->on_uncaptured_error(type, message);
}
#endif

} // namespace ofg
