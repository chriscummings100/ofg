// Owns the Dear ImGui debug overlay that is rendered by the C++ renderer.
//
// This layer intentionally stays renderer-facing: it creates and destroys the ImGui context and WebGPU backend,
// turns the DebugMenu registry into immediate-mode controls, and records lightweight diagnostics for the browser
// debug status endpoint. Browser code forwards raw input, but C++ owns the ImGui state and capture decisions.
#pragma once

#include "ofg/debug/debug_ui_input.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <webgpu/webgpu.h>

struct ImGuiContext;

namespace ofg {

struct DebugUiFrameInfo {
    float m_delta_seconds{1.0F / 60.0F};
    float m_device_pixel_ratio{1.0F};
    DebugUiInput m_input;
};

struct DebugUiStatus {
    bool m_visible{false};
    bool m_wants_capture_mouse{false};
    bool m_wants_capture_keyboard{false};
    std::uint64_t m_overlay_pass_count{0};
    std::uint64_t m_menu_tree_generation{0};
    std::uint64_t m_menu_tree_rebuild_count{0};
    std::uint32_t m_draw_list_count{0};
    std::uint32_t m_draw_command_count{0};
    std::uint32_t m_vertex_count{0};
    std::uint32_t m_index_count{0};
    std::uint64_t m_uploaded_vertex_bytes{0};
    std::uint64_t m_uploaded_index_bytes{0};
    std::uint32_t m_vertex_buffer_capacity{0};
    std::uint32_t m_index_buffer_capacity{0};
    std::uint64_t m_vertex_buffer_resize_count{0};
    std::uint64_t m_index_buffer_resize_count{0};
    std::uint64_t m_font_texture_create_count{0};
};

class DebugUi {
public:
    static std::unique_ptr<DebugUi> create(const GpuContext& gpu, WGPUTextureFormat target_format);

    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;
    DebugUi(DebugUi&&) = delete;
    DebugUi& operator=(DebugUi&&) = delete;

    void render(WGPUCommandEncoder encoder, const RenderTarget& target, const DebugUiFrameInfo& frame_info);

    void set_visible(bool visible) noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] DebugUiStatus status() const noexcept;

private:
    DebugUi(const GpuContext& gpu, WGPUTextureFormat target_format);

    void initialize();
    void reset_frame_status() noexcept;
    void update_draw_status() noexcept;
    void update_buffer_status() noexcept;

    GpuContext m_gpu{};
    WGPUTextureFormat m_target_format{WGPUTextureFormat_Undefined};
    ImGuiContext* m_context{nullptr};
    std::string m_current_menu_path;
    bool m_backend_initialized{false};
    bool m_font_texture_seen{false};
    DebugUiStatus m_status{};
};

[[nodiscard]] DebugUiStatus default_debug_ui_status() noexcept;

} // namespace ofg
