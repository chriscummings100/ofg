// Doctest coverage for the C++ debug-status JSON contract.
//
// The TypeScript host parses exact field names from this payload, so these tests
// keep serialization and escaping stable through the migration.
#include "doctest.h"

#include "ofg/runtime/runtime_debug_status.hpp"

// Verifies the serialized JSON uses the TypeScript-facing field names.
TEST_CASE("RuntimeDebugStatus emits the browser debug contract") {
  ofg::RuntimeDebugStatus status;
  status.initialized = true;
  status.frame_count = 2;
  status.canvas_width = 800;
  status.canvas_height = 450;
  status.device_pixel_ratio = 1.25;
  status.surface_format = "Bgra8UnormSrgb";
  status.adapter_name = "test adapter";
  status.backend = "BrowserWebGpu";
  status.pipeline_create_count = 1;
  status.buffer_create_count = 1;
  status.surface_configure_count = 1;

  CHECK(
    status.to_json() ==
    "{\"initialized\":true,\"frameCount\":2,\"canvasWidth\":800,\"canvasHeight\":450,"
    "\"devicePixelRatio\":1.25,\"surfaceFormat\":\"Bgra8UnormSrgb\","
    "\"adapterName\":\"test adapter\",\"backend\":\"BrowserWebGpu\","
    "\"pipelineCreateCount\":1,\"bufferCreateCount\":1,\"surfaceConfigureCount\":1,"
    "\"lastError\":null}"
  );
}

// Verifies control characters and quotes are escaped for valid JSON output.
TEST_CASE("RuntimeDebugStatus escapes strings in JSON") {
  ofg::RuntimeDebugStatus status;
  status.surface_format = std::string("control ") + static_cast<char>(0x01);
  status.adapter_name = "quote \" slash \\ newline\n";
  status.backend = "return\r";
  status.last_error = "tab\t backspace\b formfeed\f";

  CHECK(status.to_json().find("\"surfaceFormat\":\"control \\u0001\"") != std::string::npos);
  CHECK(status.to_json().find("\"adapterName\":\"quote \\\" slash \\\\ newline\\n\"") != std::string::npos);
  CHECK(status.to_json().find("\"backend\":\"return\\r\"") != std::string::npos);
  CHECK(status.to_json().find("\"lastError\":\"tab\\t backspace\\b formfeed\\f\"") != std::string::npos);
}

// Verifies helper construction of an uninitialized status with an error reason.
TEST_CASE("RuntimeDebugStatus can describe an uninitialized runtime") {
  const ofg::RuntimeDebugStatus status =
    ofg::RuntimeDebugStatus::uninitialized("missing WebGPU");

  CHECK(status.initialized == false);
  REQUIRE(status.last_error.has_value());
  CHECK(*status.last_error == "missing WebGPU");
}
