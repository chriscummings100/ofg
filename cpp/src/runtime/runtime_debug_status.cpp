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
            << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
            << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
  }
  out << '"';
}

} // namespace

// Serializes the status using the browser-facing debug-status field names.
std::string RuntimeDebugStatus::to_json() const {
  std::ostringstream out;
  out << std::boolalpha << std::setprecision(17);
  out << '{';
  out << "\"initialized\":" << initialized;
  out << ",\"frameCount\":" << frame_count;
  out << ",\"canvasWidth\":" << canvas_width;
  out << ",\"canvasHeight\":" << canvas_height;
  out << ",\"devicePixelRatio\":" << device_pixel_ratio;
  out << ",\"surfaceFormat\":";
  write_json_string(out, surface_format);
  out << ",\"adapterName\":";
  write_json_string(out, adapter_name);
  out << ",\"backend\":";
  write_json_string(out, backend);
  out << ",\"pipelineCreateCount\":" << pipeline_create_count;
  out << ",\"bufferCreateCount\":" << buffer_create_count;
  out << ",\"surfaceConfigureCount\":" << surface_configure_count;
  out << ",\"lastError\":";
  if (last_error.has_value()) {
    write_json_string(out, *last_error);
  } else {
    out << "null";
  }
  out << '}';
  return out.str();
}

// Creates a non-initialized status with a human-readable failure reason.
RuntimeDebugStatus RuntimeDebugStatus::uninitialized(std::string message) {
  RuntimeDebugStatus status;
  status.initialized = false;
  status.last_error = std::move(message);
  return status;
}

} // namespace ofg
