// Doctest coverage for the portable C++ browser-runtime contract.
//
// BrowserRuntime is intentionally WebGPU-free, so these tests can validate
// resize, frame, lifecycle, error, and debug-status behavior without Emscripten.
#include "doctest.h"

#include "ofg/runtime/browser_runtime.hpp"

#include <limits>

// Verifies resize updates public canvas fields before WebGPU is ready.
TEST_CASE("BrowserRuntime records resize state without claiming WebGPU initialization") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(800.0, 450.0, 1.25));

  const ofg::RuntimeDebugStatus& status = runtime.status();
  CHECK(status.initialized == false);
  CHECK(status.canvas_width == 800);
  CHECK(status.canvas_height == 450);
  CHECK(status.device_pixel_ratio == doctest::Approx(1.25));
  CHECK(status.last_error.has_value() == false);
}

// Verifies zero-size canvas axes remain recoverable rather than fatal.
TEST_CASE("BrowserRuntime accepts zero-size canvas axes as recoverable") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(0.0, 450.0, 2.0));

  const ofg::RuntimeDebugStatus& status = runtime.status();
  CHECK(status.initialized == false);
  CHECK(status.canvas_width == 0);
  CHECK(status.canvas_height == 450);
  CHECK(status.device_pixel_ratio == doctest::Approx(2.0));
  CHECK(status.last_error.has_value() == false);
}

// Verifies accepted frame timestamps update public frame count.
TEST_CASE("BrowserRuntime advances frame state") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.frame(16.5));
  REQUIRE(runtime.frame(33.0));

  CHECK(runtime.status().frame_count == 2);
}

// Verifies initialization only becomes true after WebGPU and surface readiness.
TEST_CASE("BrowserRuntime marks initialized after WebGPU and surface configuration") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(800.0, 450.0, 1.0));
  REQUIRE(runtime.mark_webgpu_ready("test adapter", "BrowserWebGpu", "BGRA8Unorm"));
  CHECK(runtime.status().initialized == false);

  REQUIRE(runtime.mark_surface_configured());
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().adapter_name == "test adapter");
  CHECK(runtime.status().backend == "BrowserWebGpu");
  CHECK(runtime.status().surface_format == "BGRA8Unorm");
  CHECK(runtime.status().surface_configure_count == 1);

  REQUIRE(runtime.resize(800.0, 450.0, 2.0));
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);

  REQUIRE(runtime.resize(801.0, 450.0, 2.0));
  CHECK(runtime.status().initialized == false);
  REQUIRE(runtime.mark_surface_configured());
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 2);
}

// Verifies zero-sized surfaces can recover after a later nonzero resize.
TEST_CASE("BrowserRuntime keeps zero-size WebGPU surfaces recoverable") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(0.0, 450.0, 1.0));
  REQUIRE(runtime.mark_webgpu_ready("test adapter", "BrowserWebGpu", "BGRA8Unorm"));
  REQUIRE(runtime.mark_surface_configured());

  CHECK(runtime.status().initialized == false);
  CHECK(runtime.status().surface_configure_count == 0);

  REQUIRE(runtime.resize(800.0, 450.0, 1.0));
  REQUIRE(runtime.mark_surface_configured());
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);
}

// Verifies WebGPU setup errors are reflected in the debug status contract.
TEST_CASE("BrowserRuntime records WebGPU initialization errors") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(800.0, 450.0, 1.0));
  CHECK(runtime.mark_surface_configured() == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser WebGPU device is not ready.");

  CHECK(runtime.mark_webgpu_error("requestAdapter failed") == false);
  CHECK(runtime.status().initialized == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "requestAdapter failed");
}

// Verifies durable renderer counters flow into debug status.
TEST_CASE("BrowserRuntime records durable renderer resource counters") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.mark_renderer_counters(1, 1));

  CHECK(runtime.status().pipeline_create_count == 1);
  CHECK(runtime.status().buffer_create_count == 1);
  CHECK(runtime.status().last_error.has_value() == false);
}

// Verifies invalid resize inputs fail without corrupting the last valid size.
TEST_CASE("BrowserRuntime rejects invalid resize inputs and preserves last good size") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(800.0, 450.0, 1.0));
  CHECK(runtime.resize(-1.0, 450.0, 1.0) == false);
  CHECK(runtime.status().canvas_width == 800);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(runtime.status().last_error->find("Canvas width") != std::string::npos);

  CHECK(runtime.resize(801.0, 450.5, 1.0) == false);
  CHECK(runtime.status().canvas_width == 800);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(runtime.status().last_error->find("Canvas height") != std::string::npos);

  CHECK(
    runtime.resize(
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()) + 1.0,
      450.0,
      1.0
    ) == false
  );
  CHECK(runtime.status().canvas_width == 800);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(runtime.status().last_error->find("Canvas width") != std::string::npos);

  CHECK(runtime.resize(801.0, 450.0, 0.0) == false);
  CHECK(runtime.status().canvas_width == 800);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(runtime.status().last_error->find("Device pixel ratio") != std::string::npos);
}

// Verifies non-finite frame timestamps are rejected before frame count changes.
TEST_CASE("BrowserRuntime rejects non-finite frame times") {
  ofg::BrowserRuntime runtime;

  CHECK(runtime.frame(std::numeric_limits<double>::infinity()) == false);
  CHECK(runtime.status().frame_count == 0);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(runtime.status().last_error->find("Frame time") != std::string::npos);
}

// Verifies dispose preserves diagnostics and blocks future runtime mutation.
TEST_CASE("BrowserRuntime dispose is idempotent and blocks later mutations") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.frame(16.0));
  runtime.dispose();
  runtime.dispose();

  CHECK(runtime.disposed());
  CHECK(runtime.status().initialized == false);
  CHECK(runtime.status().frame_count == 1);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser game runtime has been disposed.");

  CHECK(runtime.frame(32.0) == false);
  CHECK(runtime.resize(320.0, 200.0, 1.0) == false);
  CHECK(runtime.status().frame_count == 1);
}

// Verifies late asynchronous WebGPU callbacks cannot revive a disposed runtime.
TEST_CASE("BrowserRuntime blocks late WebGPU mutations after dispose") {
  ofg::BrowserRuntime runtime;

  runtime.dispose();

  CHECK(
    runtime.mark_webgpu_ready("late adapter", "BrowserWebGpu", "Bgra8Unorm") ==
    false
  );
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser game runtime has been disposed.");

  CHECK(runtime.mark_surface_configured() == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser game runtime has been disposed.");

  CHECK(runtime.mark_webgpu_error("late callback") == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser game runtime has been disposed.");

  CHECK(runtime.mark_renderer_counters(1, 1) == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Browser game runtime has been disposed.");
}

// Verifies the JSON facade remains available for TypeScript callers.
TEST_CASE("BrowserRuntime exposes debug status JSON") {
  ofg::BrowserRuntime runtime;

  REQUIRE(runtime.resize(320.0, 200.0, 1.0));
  REQUIRE(runtime.frame(7.0));

  CHECK(runtime.debug_status_json().find("\"frameCount\":1") != std::string::npos);
}
