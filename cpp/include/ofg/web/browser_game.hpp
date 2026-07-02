// Embind-facing browser game facade for the C++/WASM runtime.
#pragma once

#include "ofg/core/frame_state.hpp"
#include "ofg/game/game.hpp"
#include "ofg/runtime/runtime_debug_status.hpp"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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
    // Accepts raw control input from the TypeScript host.
    void set_control_input(double move_x,
        double move_y,
        double move_z,
        double look_delta_x,
        double look_delta_y,
        bool look_active,
        bool fast,
        bool slow,
        bool cycle_camera_mode);
    // Receives fetched default player model bytes from the TypeScript host.
#ifdef __EMSCRIPTEN__
    void load_player_model(emscripten::val player_bytes, emscripten::val animation_bytes);
#endif
    // Records a player model fetch/transport error from the TypeScript host.
    void report_player_model_load_error(std::string message);
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
        WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, std::weak_ptr<BrowserGame> self);
    // Handles requestDevice completion and creates durable renderer resources.
    void on_device_request(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message);
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
    // Accepts a setup-phase size before the Game singleton exists.
    void resize_setup_status(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    // Advances setup-phase frame diagnostics before the Game singleton exists.
    void tick_setup_status(double time_ms);
    // Stores or forwards the latest sanitized control input.
    void accept_control_input(ControlInput input);
    // Stores or forwards fetched player model bytes.
    void accept_player_model_bytes(std::vector<std::byte> player_bytes, std::vector<std::byte> animation_bytes);
    // Imports queued player model bytes once Game has prepared the player scene.
    void drain_pending_player_model_to_game();
    // Records a setup-phase recoverable error.
    void record_setup_error(std::string message) noexcept;
    // Records a setup-phase player model loading failure.
    void record_setup_player_model_load_failure(std::string message) noexcept;
    // Records a setup-phase GPU/device error.
    void record_setup_gpu_error(std::string message) noexcept;
    // Makes setup status inert after browser disposal.
    void dispose_setup_status() noexcept;
    // Stores a setup-phase failure reason.
    void fail_setup_status(std::string message) noexcept;
    // Releases all WebGPU handles in dependency order.
    void release_webgpu();

    // Bridges the C requestAdapter callback back to the BrowserGame instance.
    static void handle_adapter_request(
        WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2);
    // Bridges the C requestDevice callback back to the BrowserGame instance.
    static void handle_device_request(
        WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2);
    // Bridges the C device-lost callback back to the BrowserGame instance.
    static void handle_device_lost(WGPUDevice const* device,
        WGPUDeviceLostReason reason,
        WGPUStringView message,
        void* userdata1,
        void* userdata2);
    // Bridges uncaptured WebGPU errors back to the BrowserGame instance.
    static void handle_uncaptured_error(
        WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2);

    std::string m_canvas_selector;
    std::unique_ptr<BrowserGameWebGpuCallbackContext> m_device_event_context;
    WGPUInstance m_instance{nullptr};
    WGPUSurface m_surface{nullptr};
    WGPUAdapter m_adapter{nullptr};
    WGPUDevice m_device{nullptr};
    WGPUQueue m_queue{nullptr};
    WGPUTextureFormat m_surface_format{WGPUTextureFormat_Undefined};
    FrameState m_setup_frame_state;
    RuntimeDebugStatus m_setup_status;
    std::uint32_t m_pending_width{0};
    std::uint32_t m_pending_height{0};
    double m_pending_device_pixel_ratio{1.0};
    ControlInput m_pending_control_input;
    std::vector<std::byte> m_pending_player_model_bytes;
    std::vector<std::byte> m_pending_player_animation_bytes;
    std::uint32_t m_configured_width{0};
    std::uint32_t m_configured_height{0};
    bool m_has_pending_size{false};
    bool m_has_pending_control_input{false};
    bool m_has_pending_player_model{false};
    bool m_game_active{false};
    bool m_surface_configured{false};
    bool m_disposed{false};
    bool m_setup_disposed{false};
#endif
};

} // namespace ofg
