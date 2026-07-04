// JSON serialization for the C++ runtime debug-status contract.
#include "ofg/runtime/runtime_debug_status.hpp"

#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <utility>

namespace ofg {
namespace {

// Writes one JSON string literal with the escaping needed by status messages.
void write_json_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec << std::setfill(' ');
            } else {
                out << ch;
            }
            break;
        }
    }
    out << '"';
}

// Writes one shadow cascade diagnostics object.
void write_shadow_cascade_status(std::ostream& out, const RuntimeShadowCascadeStatus& cascade) {
    out << '{';
    out << "\"index\":" << cascade.m_index;
    out << ",\"testedCasterCount\":" << cascade.m_tested_caster_count;
    out << ",\"acceptedCasterCount\":" << cascade.m_accepted_caster_count;
    out << ",\"rejectedCasterCount\":" << cascade.m_rejected_caster_count;
    out << ",\"drawCount\":" << cascade.m_draw_count;
    out << ",\"submeshCount\":" << cascade.m_submesh_count;
    out << ",\"indexCount\":" << cascade.m_index_count;
    out << '}';
}

// Writes the runtime-visible shadow diagnostics object.
void write_shadow_status(std::ostream& out, const RuntimeShadowStatus& shadow) {
    out << '{';
    out << "\"enabled\":" << shadow.m_enabled;
    out << ",\"cascadeCount\":" << shadow.m_cascade_count;
    out << ",\"encodedPassCount\":" << shadow.m_encoded_pass_count;
    out << ",\"mapSize\":" << shadow.m_map_size;
    out << ",\"estimatedDepthBytes\":" << shadow.m_estimated_depth_bytes;
    out << ",\"pcfMode\":";
    write_json_string(out, shadow.m_pcf_mode);
    out << ",\"pcfSampleCount\":" << shadow.m_pcf_sample_count;
    out << ",\"sunElevationRadians\":" << shadow.m_sun_elevation_radians;
    out << ",\"effectiveIntensity\":" << shadow.m_effective_intensity;
    out << ",\"lowSunClamped\":" << shadow.m_low_sun_clamped;
    out << ",\"cascades\":[";
    for (std::size_t index = 0; index < shadow.m_cascades.size(); ++index) {
        if (index > 0U) {
            out << ',';
        }
        write_shadow_cascade_status(out, shadow.m_cascades[index]);
    }
    out << ']';
    out << ",\"totalTestedCasterCount\":" << shadow.m_total_tested_caster_count;
    out << ",\"totalAcceptedCasterCount\":" << shadow.m_total_accepted_caster_count;
    out << ",\"totalRejectedCasterCount\":" << shadow.m_total_rejected_caster_count;
    out << ",\"totalDrawCount\":" << shadow.m_total_draw_count;
    out << ",\"totalSubmeshCount\":" << shadow.m_total_submesh_count;
    out << ",\"totalIndexCount\":" << shadow.m_total_index_count;
    out << '}';
}

// Writes renderer-owned ImGui overlay diagnostics.
void write_debug_ui_status(std::ostream& out, const RuntimeDebugUiStatus& debug_ui) {
    out << '{';
    out << "\"visible\":" << debug_ui.m_visible;
    out << ",\"wantsCaptureMouse\":" << debug_ui.m_wants_capture_mouse;
    out << ",\"wantsCaptureKeyboard\":" << debug_ui.m_wants_capture_keyboard;
    out << ",\"overlayPassCount\":" << debug_ui.m_overlay_pass_count;
    out << ",\"menuTreeGeneration\":" << debug_ui.m_menu_tree_generation;
    out << ",\"menuTreeRebuildCount\":" << debug_ui.m_menu_tree_rebuild_count;
    out << ",\"drawListCount\":" << debug_ui.m_draw_list_count;
    out << ",\"drawCommandCount\":" << debug_ui.m_draw_command_count;
    out << ",\"vertexCount\":" << debug_ui.m_vertex_count;
    out << ",\"indexCount\":" << debug_ui.m_index_count;
    out << ",\"uploadedVertexBytes\":" << debug_ui.m_uploaded_vertex_bytes;
    out << ",\"uploadedIndexBytes\":" << debug_ui.m_uploaded_index_bytes;
    out << ",\"vertexBufferCapacity\":" << debug_ui.m_vertex_buffer_capacity;
    out << ",\"indexBufferCapacity\":" << debug_ui.m_index_buffer_capacity;
    out << ",\"vertexBufferResizeCount\":" << debug_ui.m_vertex_buffer_resize_count;
    out << ",\"indexBufferResizeCount\":" << debug_ui.m_index_buffer_resize_count;
    out << ",\"fontTextureCreateCount\":" << debug_ui.m_font_texture_create_count;
    out << '}';
}

} // namespace

// Clears recoverable runtime errors while preserving durable subsystem failures.
void RuntimeDebugStatus::clear_transient_error() noexcept {
    if (m_model_loading_state != "failed") {
        m_last_error.reset();
    }
}

// Serializes the status using the browser-facing debug-status field names.
std::string RuntimeDebugStatus::to_json() const {
    std::ostringstream out;
    out << std::boolalpha << std::setprecision(17);
    out << '{';
    out << "\"initialized\":" << m_initialized;
    out << ",\"lifecycleState\":";
    write_json_string(out, m_lifecycle_state);
    out << ",\"frameCount\":" << m_frame_count;
    out << ",\"canvasWidth\":" << m_canvas_width;
    out << ",\"canvasHeight\":" << m_canvas_height;
    out << ",\"devicePixelRatio\":" << m_device_pixel_ratio;
    out << ",\"surfaceFormat\":";
    write_json_string(out, m_surface_format);
    out << ",\"adapterName\":";
    write_json_string(out, m_adapter_name);
    out << ",\"backend\":";
    write_json_string(out, m_backend);
    out << ",\"cameraMode\":";
    write_json_string(out, m_camera_mode);
    out << ",\"modelLoadingState\":";
    write_json_string(out, m_model_loading_state);
    out << ",\"playerModelLoaded\":" << m_player_model_loaded;
    out << ",\"demoScene\":{";
    out << "\"name\":";
    write_json_string(out, m_demo_scene.m_name);
    out << ",\"boxCount\":" << m_demo_scene.m_box_count;
    out << ",\"nearBoxCount\":" << m_demo_scene.m_near_box_count;
    out << ",\"midBoxCount\":" << m_demo_scene.m_mid_box_count;
    out << ",\"farBoxCount\":" << m_demo_scene.m_far_box_count;
    out << ",\"partlyBelowGroundCount\":" << m_demo_scene.m_partly_below_ground_count;
    out << ",\"overlapClusterBoxCount\":" << m_demo_scene.m_overlap_cluster_box_count;
    out << ",\"offCameraCandidateCount\":" << m_demo_scene.m_off_camera_candidate_count;
    out << '}';
    out << ",\"renderCulling\":{";
    out << "\"extractedObjectCount\":" << m_render_culling.m_extracted_object_count;
    out << ",\"cameraVisibleObjectCount\":" << m_render_culling.m_camera_visible_object_count;
    out << ",\"cameraCulledObjectCount\":" << m_render_culling.m_camera_culled_object_count;
    out << '}';
    out << ",\"shadow\":";
    write_shadow_status(out, m_shadow);
    out << ",\"debugUi\":";
    write_debug_ui_status(out, m_debug_ui);
    out << ",\"pipelineCreateCount\":" << m_pipeline_create_count;
    out << ",\"bufferCreateCount\":" << m_buffer_create_count;
    out << ",\"surfaceConfigureCount\":" << m_surface_configure_count;
    out << ",\"bloomActiveLevelCount\":" << m_bloom_active_level_count;
    out << ",\"bloomEncodedPassCount\":" << m_bloom_encoded_pass_count;
    out << ",\"bloomDrawCount\":" << m_bloom_draw_count;
    out << ",\"bloomEstimatedReadBytes\":" << m_bloom_estimated_read_bytes;
    out << ",\"bloomEstimatedWriteBytes\":" << m_bloom_estimated_write_bytes;
    out << ",\"bloomSkipped\":" << m_bloom_skipped;
    out << ",\"tempBufferActiveBytes\":" << m_temp_buffer_active_bytes;
    out << ",\"tempBufferReusableBytes\":" << m_temp_buffer_reusable_bytes;
    out << ",\"tempBufferPeakBytes\":" << m_temp_buffer_peak_bytes;
    out << ",\"tempBufferCreatedCount\":" << m_temp_buffer_created_count;
    out << ",\"tempBufferReusedCount\":" << m_temp_buffer_reused_count;
    out << ",\"tempBufferDiscardedCount\":" << m_temp_buffer_discarded_count;
    out << ",\"tempBufferActiveCount\":" << m_temp_buffer_active_count;
    out << ",\"tempBufferReusableCount\":" << m_temp_buffer_reusable_count;
    out << ",\"tempBufferEarlyReleaseCount\":" << m_temp_buffer_early_release_count;
    out << ",\"tempBufferEndFrameReturnCount\":" << m_temp_buffer_end_frame_return_count;
    out << ",\"lastError\":";
    if (m_last_error.has_value()) {
        write_json_string(out, *m_last_error);
    } else {
        out << "null";
    }
    out << '}';
    return out.str();
}

// Creates a non-initialized status with a human-readable failure reason.
RuntimeDebugStatus RuntimeDebugStatus::uninitialized(std::string message) {
    RuntimeDebugStatus status;
    status.m_initialized = false;
    status.m_lifecycle_state = "uninitialized";
    status.m_last_error = std::move(message);
    return status;
}

} // namespace ofg
