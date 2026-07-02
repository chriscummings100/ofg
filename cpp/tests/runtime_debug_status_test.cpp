// Doctest coverage for the C++ debug-status JSON contract.
//
// The TypeScript host parses exact field names from this payload, so these tests
// keep serialization and escaping stable through the migration.
#include "doctest.h"

#include "ofg/runtime/runtime_debug_status.hpp"

// Verifies the serialized JSON uses the TypeScript-facing field names.
TEST_CASE("RuntimeDebugStatus emits the browser debug contract") {
    ofg::RuntimeDebugStatus status;
    status.m_initialized = true;
    status.m_lifecycle_state = "ready";
    status.m_frame_count = 2;
    status.m_canvas_width = 800;
    status.m_canvas_height = 450;
    status.m_device_pixel_ratio = 1.25;
    status.m_surface_format = "Bgra8UnormSrgb";
    status.m_adapter_name = "test adapter";
    status.m_backend = "BrowserWebGpu";
    status.m_camera_mode = "third_person";
    status.m_pipeline_create_count = 1;
    status.m_buffer_create_count = 1;
    status.m_surface_configure_count = 1;

    CHECK(status.to_json() == "{\"initialized\":true,\"lifecycleState\":\"ready\",\"frameCount\":2,"
                              "\"canvasWidth\":800,\"canvasHeight\":450,"
                              "\"devicePixelRatio\":1.25,\"surfaceFormat\":\"Bgra8UnormSrgb\","
                              "\"adapterName\":\"test adapter\",\"backend\":\"BrowserWebGpu\","
                              "\"cameraMode\":\"third_person\",\"pipelineCreateCount\":1,\"bufferCreateCount\":1,"
                              "\"surfaceConfigureCount\":1,\"lastError\":null}");
}

// Verifies control characters and quotes are escaped for valid JSON output.
TEST_CASE("RuntimeDebugStatus escapes strings in JSON") {
    ofg::RuntimeDebugStatus status;
    status.m_surface_format = std::string("control ") + static_cast<char>(0x01);
    status.m_lifecycle_state = "failed \" state";
    status.m_adapter_name = "quote \" slash \\ newline\n";
    status.m_backend = "return\r";
    status.m_last_error = "tab\t backspace\b formfeed\f";

    CHECK(status.to_json().find("\"surfaceFormat\":\"control \\u0001\"") != std::string::npos);
    CHECK(status.to_json().find("\"lifecycleState\":\"failed \\\" state\"") != std::string::npos);
    CHECK(status.to_json().find("\"adapterName\":\"quote \\\" slash \\\\ newline\\n\"") != std::string::npos);
    CHECK(status.to_json().find("\"backend\":\"return\\r\"") != std::string::npos);
    CHECK(status.to_json().find("\"lastError\":\"tab\\t backspace\\b formfeed\\f\"") != std::string::npos);
}

// Verifies helper construction of an uninitialized status with an error reason.
TEST_CASE("RuntimeDebugStatus can describe an uninitialized runtime") {
    const ofg::RuntimeDebugStatus status = ofg::RuntimeDebugStatus::uninitialized("missing WebGPU");

    CHECK(status.m_initialized == false);
    CHECK(status.m_lifecycle_state == "uninitialized");
    REQUIRE(status.m_last_error.has_value());
    CHECK(*status.m_last_error == "missing WebGPU");
}
