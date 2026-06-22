// Embind-facing browser game facade for the C++/WASM runtime.
#pragma once

#include "ofg/game/game.hpp"
#include "ofg/game/game_runtime.hpp"

#include <cstdint>
#include <memory>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#include <webgpu/webgpu.h>
#endif

namespace ofg {

#ifdef __EMSCRIPTEN__
struct BrowserGameWebGpuCallbackContext;
#endif

class BrowserGame {
public:
#ifdef __EMSCRIPTEN__
  // Captures the canvas selector used to create the browser WebGPU surface.
  explicit BrowserGame(emscripten::val canvas);
  // Releases browser WebGPU handles if the TypeScript wrapper forgets dispose().
  ~BrowserGame();

  // Creates the Embind-facing runtime and starts asynchronous WebGPU setup.
  [[nodiscard]] static std::shared_ptr<BrowserGame> create(emscripten::val canvas);
#endif

  // Receives physical canvas dimensions from the TypeScript host.
  void resize(double width, double height, double device_pixel_ratio);
  // Advances runtime state and renders a frame when WebGPU is initialized.
  void frame(double time_ms);
  // Returns the browser-facing debug-status JSON payload.
  [[nodiscard]] std::string debug_status_json() const;
  // Releases WebGPU resources and makes later lifecycle calls fail clearly.
  void dispose();

private:
#ifdef __EMSCRIPTEN__
  // Creates instance/surface state and starts requestAdapter.
  void start_webgpu_initialization(std::weak_ptr<BrowserGame> self);
  // Starts requestDevice after an adapter has been accepted.
  void request_device(std::weak_ptr<BrowserGame> self);
  // Handles requestAdapter completion on the owning runtime instance.
  void on_adapter_request(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    std::weak_ptr<BrowserGame> self
  );
  // Handles requestDevice completion and creates durable renderer resources.
  void on_device_request(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message
  );
  // Records device-loss callbacks into runtime debug status.
  void on_device_lost(WGPUDeviceLostReason reason, WGPUStringView message);
  // Records uncaptured WebGPU errors into runtime debug status.
  void on_uncaptured_error(WGPUErrorType type, WGPUStringView message);
  // Configures or unconfigures the surface to match the current canvas size.
  void configure_surface_if_ready();
  // Acquires the current surface texture and submits one bootstrap draw.
  void render_frame_if_ready();
  // Applies the latest accepted browser size to Game after async setup finishes.
  bool apply_pending_resize_to_game();
  // Records a recoverable platform error in the active status owner.
  void record_error(std::string message);
  // Records a GPU/device error in the active status owner.
  void record_gpu_error(std::string message);
  // Releases all WebGPU handles in dependency order.
  void release_webgpu();

  // Bridges the C requestAdapter callback back to the BrowserGame instance.
  static void handle_adapter_request(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
  );
  // Bridges the C requestDevice callback back to the BrowserGame instance.
  static void handle_device_request(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
  );
  // Bridges the C device-lost callback back to the BrowserGame instance.
  static void handle_device_lost(
    WGPUDevice const* device,
    WGPUDeviceLostReason reason,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
  );
  // Bridges uncaptured WebGPU errors back to the BrowserGame instance.
  static void handle_uncaptured_error(
    WGPUDevice const* device,
    WGPUErrorType type,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
  );

  std::string canvas_selector_;
  std::unique_ptr<BrowserGameWebGpuCallbackContext> device_event_context_;
  WGPUInstance instance_{nullptr};
  WGPUSurface surface_{nullptr};
  WGPUAdapter adapter_{nullptr};
  WGPUDevice device_{nullptr};
  WGPUQueue queue_{nullptr};
  WGPUTextureFormat surface_format_{WGPUTextureFormat_Undefined};
  std::unique_ptr<Game> game_;
  GameRuntime setup_runtime_{
    "Browser game runtime has been disposed.",
    "Browser WebGPU device is not ready."
  };
  std::uint32_t pending_width_{0};
  std::uint32_t pending_height_{0};
  double pending_device_pixel_ratio_{1.0};
  std::uint32_t configured_width_{0};
  std::uint32_t configured_height_{0};
  bool has_pending_size_{false};
  bool surface_configured_{false};
  bool disposed_{false};
#endif
};

} // namespace ofg
