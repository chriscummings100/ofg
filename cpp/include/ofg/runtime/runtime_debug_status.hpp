// Public debug-status snapshot for the C++ browser runtime.
//
// The TypeScript host, browser smoke, and native tests all read this shape
// through JSON. Keep field names and semantics aligned with
// src/app/wasmRuntime.ts while the implementation evolves in C++.
#pragma once

#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace ofg {

struct RuntimeDemoSceneStatus {
    std::string m_name{"unavailable"};
    std::uint32_t m_box_count{0};
    std::uint32_t m_near_box_count{0};
    std::uint32_t m_mid_box_count{0};
    std::uint32_t m_far_box_count{0};
    std::uint32_t m_partly_below_ground_count{0};
    std::uint32_t m_overlap_cluster_box_count{0};
    std::uint32_t m_off_camera_candidate_count{0};
};

struct RuntimeRenderCullingStatus {
    std::uint32_t m_extracted_object_count{0};
    std::uint32_t m_camera_visible_object_count{0};
    std::uint32_t m_camera_culled_object_count{0};
};

struct RuntimeShadowCascadeStatus {
    std::uint32_t m_index{0};
    std::uint32_t m_tested_caster_count{0};
    std::uint32_t m_accepted_caster_count{0};
    std::uint32_t m_rejected_caster_count{0};
    std::uint32_t m_draw_count{0};
    std::uint32_t m_submesh_count{0};
    std::uint32_t m_index_count{0};
};

struct RuntimeShadowStatus {
    bool m_enabled{false};
    std::uint32_t m_cascade_count{0};
    std::uint32_t m_encoded_pass_count{0};
    std::uint32_t m_map_size{0};
    std::uint64_t m_estimated_depth_bytes{0};
    std::string m_pcf_mode{"hard"};
    std::uint32_t m_pcf_sample_count{1};
    float m_sun_elevation_radians{0.0f};
    float m_effective_intensity{0.0f};
    bool m_low_sun_clamped{false};
    std::array<RuntimeShadowCascadeStatus, shadow_cascade_count()> m_cascades{};
    std::uint32_t m_total_tested_caster_count{0};
    std::uint32_t m_total_accepted_caster_count{0};
    std::uint32_t m_total_rejected_caster_count{0};
    std::uint32_t m_total_draw_count{0};
    std::uint32_t m_total_submesh_count{0};
    std::uint32_t m_total_index_count{0};
};

struct RuntimeDebugUiStatus {
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
    std::string m_model_loading_state{"not_requested"};
    bool m_player_model_loaded{false};
    RuntimeDemoSceneStatus m_demo_scene;
    RuntimeRenderCullingStatus m_render_culling;
    RuntimeShadowStatus m_shadow;
    RuntimeDebugUiStatus m_debug_ui;
    std::uint32_t m_pipeline_create_count{0};
    std::uint32_t m_buffer_create_count{0};
    std::uint32_t m_surface_configure_count{0};
    std::uint32_t m_bloom_active_level_count{0};
    std::uint32_t m_bloom_encoded_pass_count{0};
    std::uint32_t m_bloom_draw_count{0};
    std::uint64_t m_bloom_estimated_read_bytes{0};
    std::uint64_t m_bloom_estimated_write_bytes{0};
    bool m_bloom_skipped{false};
    std::uint64_t m_temp_buffer_active_bytes{0};
    std::uint64_t m_temp_buffer_reusable_bytes{0};
    std::uint64_t m_temp_buffer_peak_bytes{0};
    std::uint64_t m_temp_buffer_created_count{0};
    std::uint64_t m_temp_buffer_reused_count{0};
    std::uint64_t m_temp_buffer_discarded_count{0};
    std::uint64_t m_temp_buffer_active_count{0};
    std::uint64_t m_temp_buffer_reusable_count{0};
    std::uint64_t m_temp_buffer_early_release_count{0};
    std::uint64_t m_temp_buffer_end_frame_return_count{0};
    std::optional<std::string> m_last_error;

    // Clears recoverable runtime errors while preserving durable subsystem failures.
    void clear_transient_error() noexcept;
    // Serializes the status using the browser-facing debug-status field names.
    [[nodiscard]] std::string to_json() const;

    // Creates a non-initialized status with a human-readable failure reason.
    [[nodiscard]] static RuntimeDebugStatus uninitialized(std::string message);
};

} // namespace ofg
