// Browser-facing C++ runtime facade and WebGPU lifecycle owner.
#include "ofg/web/browser_game.hpp"
#include "ofg/web/webgpu_utils.hpp"

#include "ofg/core/engine_error.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ofg {

#ifdef __EMSCRIPTEN__
struct BrowserGameWebGpuCallbackContext {
    std::weak_ptr<BrowserGame> m_game;
};

namespace {

// Formats numeric validation failures consistently for runtime status JSON.
std::string number_message(const char* label, double value) {
    std::ostringstream out;
    out << label << " must be a non-negative integer within uint32 range, got " << value << ".";
    return out.str();
}

// Converts JavaScript numeric dimensions into the uint32 WebGPU size domain.
std::optional<std::uint32_t> parse_dimension(const char* label, double value, std::string& error) {
    if (!std::isfinite(value) || value < 0.0 || std::trunc(value) != value) {
        error = number_message(label, value);
        return std::nullopt;
    }
    constexpr double max_dimension = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (value > max_dimension) {
        error = number_message(label, value);
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

// Clears transient status errors without erasing a durable model-loading failure.
void clear_status_last_error(RuntimeDebugStatus& status) noexcept {
    if (status.m_model_loading_state != "failed") {
        status.m_last_error.reset();
    }
}

// Converts a JavaScript control-input number into a finite float.
std::optional<float> parse_control_input_float(const char* label, double value, std::string& error) {
    if (!std::isfinite(value) || value < -static_cast<double>(std::numeric_limits<float>::max()) ||
        value > static_cast<double>(std::numeric_limits<float>::max())) {
        std::ostringstream out;
        out << label << " must be a finite float, got " << value << ".";
        error = out.str();
        return std::nullopt;
    }
    return static_cast<float>(value);
}

// Converts scalar Embind control input into the C++ snapshot type.
std::optional<ControlInput> parse_control_input(double move_x,
    double move_y,
    double move_z,
    double look_delta_x,
    double look_delta_y,
    bool look_active,
    bool fast,
    bool slow,
    bool cycle_camera_mode,
    std::string& error) {
    const std::optional<float> parsed_move_x = parse_control_input_float("Control input move_x", move_x, error);
    if (!parsed_move_x.has_value()) {
        return std::nullopt;
    }
    const std::optional<float> parsed_move_y = parse_control_input_float("Control input move_y", move_y, error);
    if (!parsed_move_y.has_value()) {
        return std::nullopt;
    }
    const std::optional<float> parsed_move_z = parse_control_input_float("Control input move_z", move_z, error);
    if (!parsed_move_z.has_value()) {
        return std::nullopt;
    }
    const std::optional<float> parsed_look_delta_x =
        parse_control_input_float("Control input look_delta_x", look_delta_x, error);
    if (!parsed_look_delta_x.has_value()) {
        return std::nullopt;
    }
    const std::optional<float> parsed_look_delta_y =
        parse_control_input_float("Control input look_delta_y", look_delta_y, error);
    if (!parsed_look_delta_y.has_value()) {
        return std::nullopt;
    }

    return ControlInput{
        *parsed_move_x,
        *parsed_move_y,
        *parsed_move_z,
        *parsed_look_delta_x,
        *parsed_look_delta_y,
        look_active,
        fast,
        slow,
        cycle_camera_mode,
    };
}

// Copies one JavaScript Uint8Array-like value into durable C++ bytes.
std::vector<std::byte> copy_uint8_array_bytes(emscripten::val value, const char* label) {
    if (value.isNull() || value.isUndefined()) {
        throw EngineError(std::string(label) + " must be a Uint8Array.");
    }
    const double byte_length_value = value["byteLength"].as<double>();
    if (!std::isfinite(byte_length_value) || byte_length_value < 0.0 ||
        std::trunc(byte_length_value) != byte_length_value ||
        byte_length_value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        throw EngineError(std::string(label) + " must expose a valid Uint8Array byteLength.");
    }
    const auto byte_length = static_cast<std::uint32_t>(byte_length_value);
    std::vector<std::byte> bytes(byte_length);
    if (!bytes.empty()) {
        emscripten::val memory_view =
            emscripten::val(emscripten::typed_memory_view(byte_length, reinterpret_cast<std::uint8_t*>(bytes.data())));
        memory_view.call<void>("set", value);
    }
    return bytes;
}

// Reclaims callback heap context exactly once when Emdawn invokes a callback.
std::unique_ptr<BrowserGameWebGpuCallbackContext> consume_context(void* context) {
    return std::unique_ptr<BrowserGameWebGpuCallbackContext>(static_cast<BrowserGameWebGpuCallbackContext*>(context));
}

// Ensures the canvas has an id and returns the selector required by Emdawn.
std::string ensure_canvas_selector(emscripten::val canvas) {
    static std::uint64_t _next_canvas_id = 0;

    std::string id = canvas["id"].as<std::string>();
    if (id.empty()) {
        _next_canvas_id += 1;
        id = "ofg-cpp-canvas-" + std::to_string(_next_canvas_id);
        canvas.set("id", id);
    }
    return "#" + id;
}

} // namespace

// Captures the canvas selector used to create the browser WebGPU surface.
BrowserGame::BrowserGame(emscripten::val canvas) : m_canvas_selector(ensure_canvas_selector(canvas)) {}

// Releases browser WebGPU handles if the TypeScript wrapper forgets dispose().
BrowserGame::~BrowserGame() {
    release_webgpu();
}

// Creates the Embind-facing runtime and starts asynchronous WebGPU setup.
std::shared_ptr<BrowserGame> BrowserGame::create(emscripten::val canvas) {
    std::shared_ptr<BrowserGame> game = std::make_shared<BrowserGame>(canvas);
    try {
        game->start_webgpu_initialization(game);
    } catch (const std::exception& error) {
        game->record_gpu_error(error.what());
    } catch (...) {
        game->record_gpu_error("BrowserGame::create failed with an unknown exception.");
    }
    return game;
}
#endif

// Receives physical canvas dimensions from the TypeScript host.
void BrowserGame::resize(double width, double height, double device_pixel_ratio) {
    try {
        if (m_disposed) {
            record_error("Browser game runtime has been disposed.");
            return;
        }

        std::string error;
        const std::optional<std::uint32_t> parsed_width = parse_dimension("Canvas width", width, error);
        if (!parsed_width.has_value()) {
            record_error(error);
            return;
        }
        const std::optional<std::uint32_t> parsed_height = parse_dimension("Canvas height", height, error);
        if (!parsed_height.has_value()) {
            record_error(error);
            return;
        }

        if (m_game_active) {
            Game::resize(*parsed_width, *parsed_height, device_pixel_ratio);
            m_pending_width = *parsed_width;
            m_pending_height = *parsed_height;
            m_pending_device_pixel_ratio = device_pixel_ratio;
            m_has_pending_size = true;
#ifdef __EMSCRIPTEN__
            configure_surface_if_ready();
#endif
            return;
        }

        resize_setup_status(*parsed_width, *parsed_height, device_pixel_ratio);
        m_pending_width = *parsed_width;
        m_pending_height = *parsed_height;
        m_pending_device_pixel_ratio = device_pixel_ratio;
        m_has_pending_size = true;
    } catch (const std::exception& error) {
        record_error(error.what());
    } catch (...) {
        record_error("BrowserGame::resize failed with an unknown exception.");
    }
}

// Advances runtime state and renders a frame when WebGPU is initialized.
void BrowserGame::frame(double time_ms) {
    try {
#ifdef __EMSCRIPTEN__
        if (m_instance != nullptr) {
            wgpuInstanceProcessEvents(m_instance);
        }
#endif
        if (m_disposed) {
            record_error("Browser game runtime has been disposed.");
            return;
        }

        if (m_game_active) {
            if (!Game::prepare()) {
                return;
            }
            drain_pending_player_model_to_game();
            Game::update(time_ms);
#ifdef __EMSCRIPTEN__
            render_frame_if_ready();
#endif
            return;
        }

        tick_setup_status(time_ms);
    } catch (const std::exception& error) {
        record_error(error.what());
    } catch (...) {
        record_error("BrowserGame::frame failed with an unknown exception.");
    }
}

// Accepts raw control input from the TypeScript host.
void BrowserGame::set_control_input(double move_x,
    double move_y,
    double move_z,
    double look_delta_x,
    double look_delta_y,
    bool look_active,
    bool fast,
    bool slow,
    bool cycle_camera_mode) {
    try {
        if (m_disposed) {
            record_error("Browser game runtime has been disposed.");
            return;
        }

        std::string error;
        const std::optional<ControlInput> input = parse_control_input(
            move_x, move_y, move_z, look_delta_x, look_delta_y, look_active, fast, slow, cycle_camera_mode, error);
        if (!input.has_value()) {
            record_error(error);
            return;
        }
        accept_control_input(*input);
    } catch (const std::exception& error) {
        record_error(error.what());
    } catch (...) {
        record_error("BrowserGame::set_control_input failed with an unknown exception.");
    }
}

#ifdef __EMSCRIPTEN__
// Receives fetched default player model bytes from the TypeScript host.
void BrowserGame::load_player_model(emscripten::val player_bytes, emscripten::val animation_bytes) {
    try {
        if (m_disposed) {
            record_error("Browser game runtime has been disposed.");
            return;
        }
        accept_player_model_bytes(copy_uint8_array_bytes(player_bytes, "Player model bytes"),
            copy_uint8_array_bytes(animation_bytes, "Player animation bytes"));
    } catch (const std::exception& error) {
        report_player_model_load_error(error.what());
    } catch (...) {
        report_player_model_load_error("BrowserGame::load_player_model failed with an unknown exception.");
    }
}
#endif

// Records a player model fetch/transport error from the TypeScript host.
void BrowserGame::report_player_model_load_error(std::string message) {
    try {
        if (m_game_active) {
            Game::record_player_model_load_failure(std::move(message));
            return;
        }
        record_setup_player_model_load_failure(std::move(message));
    } catch (...) {
        record_error("BrowserGame::report_player_model_load_error failed.");
    }
}

// Returns the browser-facing debug-status JSON payload.
std::string BrowserGame::debug_status_json() const {
    try {
        if (m_game_active) {
            return Game::debug_status_json();
        }
        return m_setup_status.to_json();
    } catch (const std::exception& error) {
        return RuntimeDebugStatus::uninitialized(error.what()).to_json();
    } catch (...) {
        return RuntimeDebugStatus::uninitialized("BrowserGame::debug_status_json failed.").to_json();
    }
}

// Releases WebGPU resources and makes later lifecycle calls fail clearly.
void BrowserGame::dispose() {
    try {
#ifdef __EMSCRIPTEN__
        release_webgpu();
#endif
        m_disposed = true;
        dispose_setup_status();
        m_setup_status.m_lifecycle_state = "released";
    } catch (const std::exception& error) {
        record_error(error.what());
    } catch (...) {
        record_error("BrowserGame::dispose failed with an unknown exception.");
    }
}

#ifdef __EMSCRIPTEN__
// Creates instance/surface state and starts requestAdapter.
void BrowserGame::start_webgpu_initialization(std::weak_ptr<BrowserGame> self) {
    WGPUInstanceDescriptor instance_descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    m_instance = wgpuCreateInstance(&instance_descriptor);
    if (m_instance == nullptr) {
        record_gpu_error("wgpuCreateInstance returned null.");
        return;
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas_source.selector = webgpu::string_view(m_canvas_selector);

    WGPUSurfaceDescriptor surface_descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    surface_descriptor.nextInChain = &canvas_source.chain;
    surface_descriptor.label = webgpu::cstring_view("OFG C++ WebGPU canvas surface");
    m_surface = wgpuInstanceCreateSurface(m_instance, &surface_descriptor);
    if (m_surface == nullptr) {
        record_gpu_error("wgpuInstanceCreateSurface returned null.");
        return;
    }

    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    options.compatibleSurface = m_surface;

    WGPURequestAdapterCallbackInfo callback_info = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback_info.mode = WGPUCallbackMode_AllowSpontaneous;
    callback_info.callback = &BrowserGame::handle_adapter_request;
    callback_info.userdata1 = new BrowserGameWebGpuCallbackContext{std::move(self)};

    (void)wgpuInstanceRequestAdapter(m_instance, &options, callback_info);
}

// Starts requestDevice after an adapter has been accepted.
void BrowserGame::request_device(std::weak_ptr<BrowserGame> self) {
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.label = webgpu::cstring_view("OFG C++ WebGPU device");
    descriptor.defaultQueue.label = webgpu::cstring_view("OFG C++ WebGPU queue");
    m_device_event_context = std::make_unique<BrowserGameWebGpuCallbackContext>(BrowserGameWebGpuCallbackContext{self});
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = &BrowserGame::handle_device_lost;
    descriptor.deviceLostCallbackInfo.userdata1 = m_device_event_context.get();
    descriptor.uncapturedErrorCallbackInfo.callback = &BrowserGame::handle_uncaptured_error;
    descriptor.uncapturedErrorCallbackInfo.userdata1 = m_device_event_context.get();

    WGPURequestDeviceCallbackInfo callback_info = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback_info.mode = WGPUCallbackMode_AllowSpontaneous;
    callback_info.callback = &BrowserGame::handle_device_request;
    callback_info.userdata1 = new BrowserGameWebGpuCallbackContext{std::move(self)};

    (void)wgpuAdapterRequestDevice(m_adapter, &descriptor, callback_info);
}

// Handles requestAdapter completion on the owning runtime instance.
void BrowserGame::on_adapter_request(
    WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, std::weak_ptr<BrowserGame> self) {
    if (m_disposed) {
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        return;
    }

    if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
        record_gpu_error(
            webgpu::failure_message("requestAdapter", webgpu::request_adapter_status_name(status), message));
        return;
    }

    m_adapter = adapter;
    request_device(std::move(self));
}

// Handles requestDevice completion and creates durable renderer resources.
void BrowserGame::on_device_request(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message) {
    if (m_disposed) {
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        return;
    }

    if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
        record_gpu_error(webgpu::failure_message("requestDevice", webgpu::request_device_status_name(status), message));
        return;
    }

    m_device = device;
    m_queue = wgpuDeviceGetQueue(m_device);
    if (m_queue == nullptr) {
        record_gpu_error("wgpuDeviceGetQueue returned null.");
        return;
    }

    // Choose the final surface format before creating renderer resources.
    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    const WGPUStatus capabilities_status = wgpuSurfaceGetCapabilities(m_surface, m_adapter, &capabilities);
    if (capabilities_status != WGPUStatus_Success || capabilities.formatCount == 0) {
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        record_gpu_error("wgpuSurfaceGetCapabilities returned no formats.");
        return;
    }

    m_surface_format = webgpu::choose_surface_format(capabilities);
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    if (m_surface_format == WGPUTextureFormat_Undefined) {
        record_gpu_error("No usable WebGPU surface format was found.");
        return;
    }

    // Create the shared Game singleton once after browser device and format selection.
    Game::create(
        GpuContext{m_device, m_queue, webgpu::adapter_name_from_info(m_adapter), "BrowserWebGpu"}, m_surface_format);
    m_game_active = true;
    if (m_has_pending_control_input) {
        Game::set_control_input(m_pending_control_input);
    }

    if (apply_pending_resize_to_game()) {
        configure_surface_if_ready();
    }
}

// Records device-loss callbacks into runtime debug status.
void BrowserGame::on_device_lost(WGPUDeviceLostReason reason, WGPUStringView message) {
    if (m_disposed) {
        return;
    }
    record_gpu_error(webgpu::failure_message("device lost", webgpu::device_lost_reason_name(reason), message));
}

// Records uncaptured WebGPU errors into runtime debug status.
void BrowserGame::on_uncaptured_error(WGPUErrorType type, WGPUStringView message) {
    if (m_disposed) {
        return;
    }
    record_gpu_error(webgpu::failure_message("uncaptured WebGPU error", webgpu::error_type_name(type), message));
}

// Configures or unconfigures the surface to match the current canvas size.
void BrowserGame::configure_surface_if_ready() {
    if (!m_game_active || m_surface == nullptr || m_device == nullptr ||
        m_surface_format == WGPUTextureFormat_Undefined || m_disposed) {
        return;
    }

    const RuntimeDebugStatus& status = Game::status();
    if (status.m_canvas_width == 0 || status.m_canvas_height == 0) {
        if (m_surface_configured) {
            wgpuSurfaceUnconfigure(m_surface);
            m_surface_configured = false;
            m_configured_width = 0;
            m_configured_height = 0;
        }
        return;
    }

    if (m_surface_configured && m_configured_width == status.m_canvas_width &&
        m_configured_height == status.m_canvas_height) {
        return;
    }

    // Emdawn presents from requestAnimationFrame; no explicit Present call is used.
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = m_device;
    config.format = m_surface_format;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = status.m_canvas_width;
    config.height = status.m_canvas_height;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    config.presentMode = WGPUPresentMode_Fifo;

    wgpuSurfaceConfigure(m_surface, &config);
    m_surface_configured = true;
    m_configured_width = status.m_canvas_width;
    m_configured_height = status.m_canvas_height;
}

// Acquires the current surface texture and submits one bootstrap draw.
void BrowserGame::render_frame_if_ready() {
    if (!m_game_active || m_surface == nullptr || m_device == nullptr || m_queue == nullptr || !m_surface_configured) {
        return;
    }

    const RuntimeDebugStatus& status = Game::status();
    if (status.m_canvas_width == 0 || status.m_canvas_height == 0) {
        return;
    }

    // Surface acquisition is the only browser-owned texture step per frame.
    WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(m_surface, &surface_texture);
    const bool texture_ready = surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                               surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    if (!texture_ready || surface_texture.texture == nullptr) {
        if (surface_texture.texture != nullptr) {
            wgpuTextureRelease(surface_texture.texture);
        }
        const std::string message = "wgpuSurfaceGetCurrentTexture failed with status " +
                                    webgpu::surface_texture_status_name(surface_texture.status) + ".";
        if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
            surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated) {
            record_error(message);
            if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated) {
                m_surface_configured = false;
                m_configured_width = 0;
                m_configured_height = 0;
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
    WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoder_descriptor.label = webgpu::cstring_view("OFG C++ bootstrap encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoder_descriptor);
    if (encoder == nullptr) {
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surface_texture.texture);
        record_gpu_error("wgpuDeviceCreateCommandEncoder returned null.");
        return;
    }

    try {
        Game::render(encoder, RenderTarget{view, m_surface_format, status.m_canvas_width, status.m_canvas_height});
    } catch (...) {
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surface_texture.texture);
        throw;
    }

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = webgpu::cstring_view("OFG C++ bootstrap commands");
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &command_descriptor);
    if (command == nullptr) {
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surface_texture.texture);
        record_gpu_error("wgpuCommandEncoderFinish returned null.");
        return;
    }

    wgpuQueueSubmit(m_queue, 1, &command);

    // Release every per-frame handle after submission; durable resources stay put.
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surface_texture.texture);
}

// Applies the latest accepted browser size to Game after async setup finishes.
bool BrowserGame::apply_pending_resize_to_game() {
    if (!m_game_active || !m_has_pending_size) {
        return m_game_active;
    }

    Game::resize(m_pending_width, m_pending_height, m_pending_device_pixel_ratio);
    return true;
}

// Records a recoverable platform error in the active status owner.
void BrowserGame::record_error(std::string message) {
    if (m_game_active) {
        Game::record_error(std::move(message));
        return;
    }
    record_setup_error(std::move(message));
}

// Records a GPU/device error in the active status owner.
void BrowserGame::record_gpu_error(std::string message) {
    if (m_game_active) {
        Game::record_gpu_error(std::move(message));
        return;
    }
    record_setup_gpu_error(std::move(message));
}

// Accepts a setup-phase size before the Game singleton exists.
void BrowserGame::resize_setup_status(std::uint32_t width, std::uint32_t height, double device_pixel_ratio) {
    if (m_setup_disposed) {
        const std::string message = "Browser game runtime has been disposed.";
        fail_setup_status(message);
        throw EngineError(message);
    }
    if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
        std::ostringstream out;
        out << "Device pixel ratio must be a positive finite number, got " << device_pixel_ratio << ".";
        const std::string message = out.str();
        fail_setup_status(message);
        throw EngineError(message);
    }

    m_setup_status.m_canvas_width = width;
    m_setup_status.m_canvas_height = height;
    m_setup_status.m_device_pixel_ratio = device_pixel_ratio;
    m_setup_status.m_initialized = false;
    clear_status_last_error(m_setup_status);
}

// Advances setup-phase frame diagnostics before the Game singleton exists.
void BrowserGame::tick_setup_status(double time_ms) {
    if (m_setup_disposed) {
        const std::string message = "Browser game runtime has been disposed.";
        fail_setup_status(message);
        throw EngineError(message);
    }
    if (!std::isfinite(time_ms)) {
        std::ostringstream out;
        out << "Frame time must be finite, got " << time_ms << ".";
        const std::string message = out.str();
        fail_setup_status(message);
        throw EngineError(message);
    }

    m_setup_frame_state.tick(time_ms);
    m_setup_status.m_frame_count = m_setup_frame_state.frame_count();
    clear_status_last_error(m_setup_status);
}

// Stores or forwards the latest sanitized control input.
void BrowserGame::accept_control_input(ControlInput input) {
    m_pending_control_input = input;
    m_has_pending_control_input = true;
    if (m_game_active) {
        Game::set_control_input(input);
    }
}

// Stores or forwards fetched player model bytes.
void BrowserGame::accept_player_model_bytes(
    std::vector<std::byte> player_bytes, std::vector<std::byte> animation_bytes) {
    if (player_bytes.empty()) {
        throw EngineError("Player model bytes must not be empty.");
    }
    if (animation_bytes.empty()) {
        throw EngineError("Player animation bytes must not be empty.");
    }
    if (m_game_active && Game::state() == GameLifecycleState::Ready) {
        Game::load_player_model(player_bytes, animation_bytes);
        return;
    }

    m_pending_player_model_bytes = std::move(player_bytes);
    m_pending_player_animation_bytes = std::move(animation_bytes);
    m_has_pending_player_model = true;
    m_setup_status.m_model_loading_state = "queued";
    m_setup_status.m_player_model_loaded = false;
    m_setup_status.m_last_error.reset();
}

// Imports queued player model bytes once Game has prepared the player scene.
void BrowserGame::drain_pending_player_model_to_game() {
    if (!m_game_active || !m_has_pending_player_model || Game::state() != GameLifecycleState::Ready) {
        return;
    }

    try {
        Game::load_player_model(m_pending_player_model_bytes, m_pending_player_animation_bytes);
    } catch (const std::exception& error) {
        Game::record_player_model_load_failure(error.what());
    } catch (...) {
        Game::record_player_model_load_failure("Queued player model import failed with an unknown exception.");
    }
    m_pending_player_model_bytes.clear();
    m_pending_player_model_bytes.shrink_to_fit();
    m_pending_player_animation_bytes.clear();
    m_pending_player_animation_bytes.shrink_to_fit();
    m_has_pending_player_model = false;
}

// Records a setup-phase recoverable error.
void BrowserGame::record_setup_error(std::string message) noexcept {
    if (m_setup_disposed) {
        fail_setup_status("Browser game runtime has been disposed.");
        return;
    }
    fail_setup_status(std::move(message));
}

// Records a setup-phase player model loading failure.
void BrowserGame::record_setup_player_model_load_failure(std::string message) noexcept {
    if (m_setup_disposed) {
        fail_setup_status("Browser game runtime has been disposed.");
        return;
    }
    if (message.empty()) {
        message = "Unknown player model loading error.";
    }
    m_setup_status.m_model_loading_state = "failed";
    m_setup_status.m_player_model_loaded = false;
    m_setup_status.m_last_error = std::move(message);
    m_has_pending_player_model = false;
    m_pending_player_model_bytes.clear();
    m_pending_player_animation_bytes.clear();
}

// Records a setup-phase GPU/device error.
void BrowserGame::record_setup_gpu_error(std::string message) noexcept {
    if (m_setup_disposed) {
        fail_setup_status("Browser game runtime has been disposed.");
        return;
    }
    fail_setup_status(std::move(message));
}

// Makes setup status inert after browser disposal.
void BrowserGame::dispose_setup_status() noexcept {
    m_setup_disposed = true;
    const std::uint64_t frame_count = m_setup_status.m_frame_count;
    m_setup_status = RuntimeDebugStatus::uninitialized("Browser game runtime has been disposed.");
    m_setup_status.m_frame_count = frame_count;
}

// Stores a setup-phase failure reason.
void BrowserGame::fail_setup_status(std::string message) noexcept {
    if (message.empty()) {
        message = "Unknown browser runtime error.";
    }
    m_setup_status.m_initialized = false;
    m_setup_status.m_last_error = std::move(message);
}

// Releases all WebGPU handles in dependency order.
void BrowserGame::release_webgpu() {
    if (m_game_active) {
        try {
            while (!Game::release()) {}
        } catch (const std::exception& error) {
            Game::record_error(error.what());
        } catch (...) {
            Game::record_error("BrowserGame::release_webgpu observed an unknown Game release exception.");
        }
        Game::destroy();
        m_game_active = false;
    }

    if (m_surface != nullptr && m_surface_configured) {
        wgpuSurfaceUnconfigure(m_surface);
    }
    m_surface_configured = false;
    m_configured_width = 0;
    m_configured_height = 0;

    if (m_queue != nullptr) {
        wgpuQueueRelease(m_queue);
        m_queue = nullptr;
    }
    if (m_device != nullptr) {
        wgpuDeviceRelease(m_device);
        m_device = nullptr;
    }
    m_device_event_context.reset();
    if (m_adapter != nullptr) {
        wgpuAdapterRelease(m_adapter);
        m_adapter = nullptr;
    }
    if (m_surface != nullptr) {
        wgpuSurfaceRelease(m_surface);
        m_surface = nullptr;
    }
    if (m_instance != nullptr) {
        wgpuInstanceRelease(m_instance);
        m_instance = nullptr;
    }
    m_surface_format = WGPUTextureFormat_Undefined;
}

// Bridges the C requestAdapter callback back to the BrowserGame instance.
void BrowserGame::handle_adapter_request(
    WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    std::unique_ptr<BrowserGameWebGpuCallbackContext> context = consume_context(userdata1);
    std::weak_ptr<BrowserGame> self = context->m_game;
    std::shared_ptr<BrowserGame> game = self.lock();
    if (!game) {
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        return;
    }
    try {
        game->on_adapter_request(status, adapter, message, std::move(self));
    } catch (const std::exception& error) {
        game->record_gpu_error(error.what());
    } catch (...) {
        game->record_gpu_error("BrowserGame requestAdapter callback failed with an unknown exception.");
    }
}

// Bridges the C requestDevice callback back to the BrowserGame instance.
void BrowserGame::handle_device_request(
    WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    std::unique_ptr<BrowserGameWebGpuCallbackContext> context = consume_context(userdata1);
    std::shared_ptr<BrowserGame> game = context->m_game.lock();
    if (!game) {
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        return;
    }
    try {
        game->on_device_request(status, device, message);
    } catch (const std::exception& error) {
        game->record_gpu_error(error.what());
    } catch (...) {
        game->record_gpu_error("BrowserGame requestDevice callback failed with an unknown exception.");
    }
}

// Bridges the C device-lost callback back to the BrowserGame instance.
void BrowserGame::handle_device_lost(
    WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)device;
    (void)userdata2;
    auto* context = static_cast<BrowserGameWebGpuCallbackContext*>(userdata1);
    if (context == nullptr) {
        return;
    }
    std::shared_ptr<BrowserGame> game = context->m_game.lock();
    if (!game) {
        return;
    }
    try {
        game->on_device_lost(reason, message);
    } catch (const std::exception& error) {
        game->record_gpu_error(error.what());
    } catch (...) {
        game->record_gpu_error("BrowserGame device-lost callback failed with an unknown exception.");
    }
}

// Bridges uncaptured WebGPU errors back to the BrowserGame instance.
void BrowserGame::handle_uncaptured_error(
    WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)device;
    (void)userdata2;
    auto* context = static_cast<BrowserGameWebGpuCallbackContext*>(userdata1);
    if (context == nullptr) {
        return;
    }
    std::shared_ptr<BrowserGame> game = context->m_game.lock();
    if (!game) {
        return;
    }
    try {
        game->on_uncaptured_error(type, message);
    } catch (const std::exception& error) {
        game->record_gpu_error(error.what());
    } catch (...) {
        game->record_gpu_error("BrowserGame uncaptured-error callback failed with an unknown exception.");
    }
}
#endif

} // namespace ofg
