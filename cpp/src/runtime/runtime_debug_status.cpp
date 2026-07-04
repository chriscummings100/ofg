// JSON serialization for the C++ runtime debug-status contract.
#include "ofg/runtime/runtime_debug_status.hpp"

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
