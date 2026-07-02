// Public debug-status snapshot for the C++ browser runtime.
//
// The TypeScript host, browser smoke, and native tests all read this shape
// through JSON. Keep field names and semantics aligned with
// src/app/wasmRuntime.ts while the implementation evolves in C++.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ofg {

struct RuntimeDebugStatus {
    bool m_initialized{false};
    std::string m_lifecycle_state{"uninitialized"};
    std::uint64_t m_frame_count{0};
    std::uint32_t m_canvas_width{0};
    std::uint32_t m_canvas_height{0};
    double m_device_pixel_ratio{1.0};
    std::string m_surface_format{"Unavailable"};
    std::string m_adapter_name{"Unavailable"};
    std::string m_backend{"CppWasm"};
    std::string m_camera_mode{"debug"};
    std::uint32_t m_pipeline_create_count{0};
    std::uint32_t m_buffer_create_count{0};
    std::uint32_t m_surface_configure_count{0};
    std::optional<std::string> m_last_error;

    // Serializes the status using the browser-facing debug-status field names.
    [[nodiscard]] std::string to_json() const;

    // Creates a non-initialized status with a human-readable failure reason.
    [[nodiscard]] static RuntimeDebugStatus uninitialized(std::string message);
};

} // namespace ofg
