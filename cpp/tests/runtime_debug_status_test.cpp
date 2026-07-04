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
    status.m_model_loading_state = "loaded";
    status.m_player_model_loaded = true;
    status.m_demo_scene =
        ofg::RuntimeDemoSceneStatus{"large-default-culling-shadow-validation", 184, 22, 79, 83, 24, 24, 16};
    status.m_render_culling = ofg::RuntimeRenderCullingStatus{186, 160, 26};
    status.m_shadow.m_enabled = true;
    status.m_shadow.m_cascade_count = 3;
    status.m_shadow.m_encoded_pass_count = 3;
    status.m_shadow.m_map_size = 1024;
    status.m_shadow.m_estimated_depth_bytes = 1024ULL * 1024ULL * 3ULL * 4ULL;
    status.m_shadow.m_pcf_mode = "five_tap";
    status.m_shadow.m_pcf_sample_count = 5;
    status.m_shadow.m_sun_elevation_radians = 0.75f;
    status.m_shadow.m_effective_intensity = 0.75f;
    status.m_shadow.m_cascades[0] = ofg::RuntimeShadowCascadeStatus{0, 186, 42, 144, 42, 42, 1512};
    status.m_shadow.m_cascades[1] = ofg::RuntimeShadowCascadeStatus{1, 186, 75, 111, 75, 75, 2700};
    status.m_shadow.m_cascades[2] = ofg::RuntimeShadowCascadeStatus{2, 186, 90, 96, 90, 90, 3240};
    status.m_shadow.m_total_tested_caster_count = 558;
    status.m_shadow.m_total_accepted_caster_count = 207;
    status.m_shadow.m_total_rejected_caster_count = 351;
    status.m_shadow.m_total_draw_count = 207;
    status.m_shadow.m_total_submesh_count = 207;
    status.m_shadow.m_total_index_count = 7452;
    status.m_debug_ui.m_visible = true;
    status.m_debug_ui.m_overlay_pass_count = 2;
    status.m_debug_ui.m_menu_tree_generation = 3;
    status.m_debug_ui.m_menu_tree_rebuild_count = 1;
    status.m_debug_ui.m_draw_list_count = 1;
    status.m_debug_ui.m_draw_command_count = 6;
    status.m_debug_ui.m_vertex_count = 400;
    status.m_debug_ui.m_index_count = 900;
    status.m_debug_ui.m_uploaded_vertex_bytes = 8000;
    status.m_debug_ui.m_uploaded_index_bytes = 1800;
    status.m_debug_ui.m_vertex_buffer_capacity = 5400;
    status.m_debug_ui.m_index_buffer_capacity = 10900;
    status.m_debug_ui.m_vertex_buffer_resize_count = 1;
    status.m_debug_ui.m_index_buffer_resize_count = 1;
    status.m_debug_ui.m_font_texture_create_count = 1;
    status.m_pipeline_create_count = 1;
    status.m_buffer_create_count = 1;
    status.m_surface_configure_count = 1;
    status.m_bloom_active_level_count = 4;
    status.m_bloom_encoded_pass_count = 7;
    status.m_bloom_draw_count = 7;
    status.m_bloom_estimated_read_bytes = 2048;
    status.m_bloom_estimated_write_bytes = 1024;
    status.m_bloom_skipped = false;
    status.m_temp_buffer_active_bytes = 0;
    status.m_temp_buffer_reusable_bytes = 512;
    status.m_temp_buffer_peak_bytes = 4096;
    status.m_temp_buffer_created_count = 3;
    status.m_temp_buffer_reused_count = 5;
    status.m_temp_buffer_discarded_count = 1;
    status.m_temp_buffer_active_count = 0;
    status.m_temp_buffer_reusable_count = 3;
    status.m_temp_buffer_early_release_count = 4;
    status.m_temp_buffer_end_frame_return_count = 1;

    CHECK(status.to_json() == "{\"initialized\":true,\"lifecycleState\":\"ready\",\"frameCount\":2,"
                              "\"canvasWidth\":800,\"canvasHeight\":450,"
                              "\"devicePixelRatio\":1.25,\"surfaceFormat\":\"Bgra8UnormSrgb\","
                              "\"adapterName\":\"test adapter\",\"backend\":\"BrowserWebGpu\","
                              "\"cameraMode\":\"third_person\",\"modelLoadingState\":\"loaded\","
                              "\"playerModelLoaded\":true,"
                              "\"demoScene\":{\"name\":\"large-default-culling-shadow-validation\","
                              "\"boxCount\":184,\"nearBoxCount\":22,\"midBoxCount\":79,\"farBoxCount\":83,"
                              "\"partlyBelowGroundCount\":24,\"overlapClusterBoxCount\":24,"
                              "\"offCameraCandidateCount\":16},"
                              "\"renderCulling\":{\"extractedObjectCount\":186,"
                              "\"cameraVisibleObjectCount\":160,\"cameraCulledObjectCount\":26},"
                              "\"shadow\":{\"enabled\":true,\"cascadeCount\":3,\"encodedPassCount\":3,"
                              "\"mapSize\":1024,\"estimatedDepthBytes\":12582912,\"pcfMode\":\"five_tap\","
                              "\"pcfSampleCount\":5,\"sunElevationRadians\":0.75,\"effectiveIntensity\":0.75,"
                              "\"lowSunClamped\":false,\"cascades\":["
                              "{\"index\":0,\"testedCasterCount\":186,\"acceptedCasterCount\":42,"
                              "\"rejectedCasterCount\":144,\"drawCount\":42,\"submeshCount\":42,"
                              "\"indexCount\":1512},"
                              "{\"index\":1,\"testedCasterCount\":186,\"acceptedCasterCount\":75,"
                              "\"rejectedCasterCount\":111,\"drawCount\":75,\"submeshCount\":75,"
                              "\"indexCount\":2700},"
                              "{\"index\":2,\"testedCasterCount\":186,\"acceptedCasterCount\":90,"
                              "\"rejectedCasterCount\":96,\"drawCount\":90,\"submeshCount\":90,"
                              "\"indexCount\":3240}],"
                              "\"totalTestedCasterCount\":558,\"totalAcceptedCasterCount\":207,"
                              "\"totalRejectedCasterCount\":351,\"totalDrawCount\":207,"
                              "\"totalSubmeshCount\":207,\"totalIndexCount\":7452},"
                              "\"debugUi\":{\"visible\":true,\"wantsCaptureMouse\":false,"
                              "\"wantsCaptureKeyboard\":false,\"overlayPassCount\":2,"
                              "\"menuTreeGeneration\":3,\"menuTreeRebuildCount\":1,"
                              "\"drawListCount\":1,\"drawCommandCount\":6,\"vertexCount\":400,"
                              "\"indexCount\":900,\"uploadedVertexBytes\":8000,"
                              "\"uploadedIndexBytes\":1800,\"vertexBufferCapacity\":5400,"
                              "\"indexBufferCapacity\":10900,\"vertexBufferResizeCount\":1,"
                              "\"indexBufferResizeCount\":1,\"fontTextureCreateCount\":1},"
                              "\"pipelineCreateCount\":1,\"bufferCreateCount\":1,"
                              "\"surfaceConfigureCount\":1,\"bloomActiveLevelCount\":4,"
                              "\"bloomEncodedPassCount\":7,\"bloomDrawCount\":7,"
                              "\"bloomEstimatedReadBytes\":2048,\"bloomEstimatedWriteBytes\":1024,"
                              "\"bloomSkipped\":false,\"tempBufferActiveBytes\":0,"
                              "\"tempBufferReusableBytes\":512,\"tempBufferPeakBytes\":4096,"
                              "\"tempBufferCreatedCount\":3,\"tempBufferReusedCount\":5,"
                              "\"tempBufferDiscardedCount\":1,\"tempBufferActiveCount\":0,"
                              "\"tempBufferReusableCount\":3,\"tempBufferEarlyReleaseCount\":4,"
                              "\"tempBufferEndFrameReturnCount\":1,\"lastError\":null}");
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
