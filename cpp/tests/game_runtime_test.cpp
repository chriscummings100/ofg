// Doctest coverage for shared OFG Game runtime and render validation.
//
// These tests avoid real GPU execution. They validate shared lifecycle/status
// behavior and render-target checks that the browser and native frame drivers
// will use when they delegate to Game.
#include "doctest.h"

#include "ofg/game/game.hpp"
#include "ofg/game/game_runtime.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"

#include <cstdint>
#include <limits>
#include <string>

#include <webgpu/webgpu.h>

namespace {

// Produces a non-null opaque WebGPU handle for validation-only tests.
WGPUTextureView fake_texture_view() {
  return reinterpret_cast<WGPUTextureView>(static_cast<std::uintptr_t>(1));
}

} // namespace

// Verifies the shared runtime records resize and frame state.
TEST_CASE("GameRuntime records resize and tick state") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.5, error));
  REQUIRE(runtime.tick(16.0, error));
  REQUIRE(runtime.tick(33.0, error));

  const ofg::RuntimeDebugStatus& status = runtime.status();
  CHECK(status.canvas_width == 800);
  CHECK(status.canvas_height == 450);
  CHECK(status.device_pixel_ratio == doctest::Approx(1.5));
  CHECK(status.frame_count == 2);
  CHECK(status.last_error.has_value() == false);
}

// Verifies the shared runtime exposes the browser-facing debug JSON contract.
TEST_CASE("GameRuntime exposes debug status JSON") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(320, 200, 1.0, error));
  REQUIRE(runtime.tick(7.0, error));

  CHECK(runtime.debug_status_json().find("\"frameCount\":1") != std::string::npos);
}

// Verifies readiness stays false until the platform target is configured.
TEST_CASE("GameRuntime tracks GPU readiness and target configuration") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  REQUIRE(runtime.mark_gpu_ready("adapter", "Backend", "Rgba8Unorm", error));
  CHECK(runtime.status().initialized == false);

  REQUIRE(runtime.mark_surface_configured(error));
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);
  CHECK(runtime.status().adapter_name == "adapter");
  CHECK(runtime.status().backend == "Backend");
  CHECK(runtime.status().surface_format == "Rgba8Unorm");
}

// Verifies zero-size targets stay recoverable without counting configuration.
TEST_CASE("GameRuntime keeps zero-size targets recoverable") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(0, 450, 1.0, error));
  REQUIRE(runtime.mark_gpu_ready("adapter", "Backend", "Rgba8Unorm", error));
  REQUIRE(runtime.mark_surface_configured(error));

  CHECK(runtime.status().initialized == false);
  CHECK(runtime.status().surface_configure_count == 0);
  CHECK(runtime.status().last_error.has_value() == false);

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  REQUIRE(runtime.mark_surface_configured(error));
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);
}

// Verifies idempotent configuration does not inflate renderer diagnostics.
TEST_CASE("GameRuntime counts target configuration transitions once") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  REQUIRE(runtime.mark_gpu_ready("adapter", "Backend", "Rgba8Unorm", error));
  REQUIRE(runtime.mark_surface_configured(error));
  REQUIRE(runtime.mark_surface_configured(error));

  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);

  REQUIRE(runtime.resize(801, 450, 1.0, error));
  CHECK(runtime.status().initialized == false);
  REQUIRE(runtime.mark_surface_configured(error));
  CHECK(runtime.status().surface_configure_count == 2);
}

// Verifies durable renderer counters flow into debug status.
TEST_CASE("GameRuntime records durable renderer resource counters") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.mark_renderer_counters(1, 1, error));

  CHECK(runtime.status().pipeline_create_count == 1);
  CHECK(runtime.status().buffer_create_count == 1);
  CHECK(runtime.status().last_error.has_value() == false);
}

// Verifies recoverable errors do not force GPU/resource reinitialization.
TEST_CASE("GameRuntime recoverable errors preserve configured target state") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  REQUIRE(runtime.mark_gpu_ready("adapter", "Backend", "Rgba8Unorm", error));
  REQUIRE(runtime.mark_surface_configured(error));

  CHECK(runtime.mark_error("transient render validation") == false);
  CHECK(runtime.status().initialized == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "transient render validation");

  REQUIRE(runtime.mark_surface_configured(error));
  CHECK(runtime.status().initialized == true);
  CHECK(runtime.status().surface_configure_count == 1);
  CHECK(runtime.status().last_error.has_value() == false);
}

// Verifies GPU/device errors clear readiness until the platform recreates it.
TEST_CASE("GameRuntime GPU errors require readiness to be restored") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  REQUIRE(runtime.mark_gpu_ready("adapter", "Backend", "Rgba8Unorm", error));
  REQUIRE(runtime.mark_surface_configured(error));

  CHECK(runtime.mark_gpu_error("device lost") == false);
  CHECK(runtime.status().initialized == false);
  CHECK(runtime.mark_surface_configured(error) == false);
  CHECK(error == "Game GPU device is not ready.");
}

// Verifies invalid shared runtime inputs are recoverable status errors.
TEST_CASE("GameRuntime rejects invalid tick and resize inputs") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));
  CHECK(runtime.resize(320, 200, 0.0, error) == false);
  CHECK(error.find("Device pixel ratio") != std::string::npos);
  CHECK(runtime.status().canvas_width == 800);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == error);

  CHECK(runtime.tick(std::numeric_limits<double>::infinity(), error) == false);
  CHECK(error.find("Frame time") != std::string::npos);
  CHECK(runtime.status().frame_count == 0);
}

// Verifies target configuration fails clearly before GPU setup finishes.
TEST_CASE("GameRuntime rejects target configuration before GPU readiness") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.resize(800, 450, 1.0, error));

  CHECK(runtime.mark_surface_configured(error) == false);
  CHECK(error == "Game GPU device is not ready.");
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == error);
}

// Verifies disposal blocks late mutation while preserving frame diagnostics.
TEST_CASE("GameRuntime dispose blocks future mutation") {
  ofg::GameRuntime runtime;
  std::string error;

  REQUIRE(runtime.tick(1.0, error));
  runtime.dispose();

  CHECK(runtime.disposed());
  CHECK(runtime.status().frame_count == 1);
  CHECK(runtime.resize(1, 1, 1.0, error) == false);
  CHECK(error == "Game runtime has been disposed.");
  CHECK(runtime.tick(2.0, error) == false);
  CHECK(error == "Game runtime has been disposed.");
  CHECK(runtime.mark_gpu_ready("late", "Backend", "Format", error) == false);
  CHECK(error == "Game runtime has been disposed.");
  CHECK(runtime.mark_renderer_counters(1, 1, error) == false);
  CHECK(error == "Game runtime has been disposed.");
  CHECK(runtime.mark_surface_configured(error) == false);
  CHECK(error == "Game runtime has been disposed.");
  CHECK(runtime.mark_error("late error") == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Game runtime has been disposed.");
  CHECK(runtime.mark_gpu_error("late gpu error") == false);
  REQUIRE(runtime.status().last_error.has_value());
  CHECK(*runtime.status().last_error == "Game runtime has been disposed.");
}

// Verifies render target validation catches null and mismatched targets.
TEST_CASE("RenderTarget validation rejects invalid frame targets") {
  std::string error;
  const WGPUTextureFormat expected_format = WGPUTextureFormat_RGBA8Unorm;

  CHECK(
    ofg::validate_render_target(
      ofg::RenderTarget{},
      expected_format,
      800,
      450,
      error
    ) == false
  );
  CHECK(error.find("texture view") != std::string::npos);

  CHECK(
    ofg::validate_render_target(
      ofg::RenderTarget{
        fake_texture_view(),
        WGPUTextureFormat_BGRA8Unorm,
        800,
        450
      },
      expected_format,
      800,
      450,
      error
    ) == false
  );
  CHECK(error.find("does not match renderer format") != std::string::npos);

  CHECK(
    ofg::validate_render_target(
      ofg::RenderTarget{fake_texture_view(), expected_format, 801, 450},
      expected_format,
      800,
      450,
      error
    ) == false
  );
  CHECK(error.find("does not match latest resize") != std::string::npos);

  CHECK(
    ofg::validate_render_target(
      ofg::RenderTarget{fake_texture_view(), expected_format, 0, 450},
      expected_format,
      800,
      450,
      error
    ) == false
  );
  CHECK(error.find("dimensions must be nonzero") != std::string::npos);
}

// Verifies valid render targets pass without touching GPU resources.
TEST_CASE("RenderTarget validation accepts matching nonzero target") {
  std::string error = "stale";

  CHECK(
    ofg::validate_render_target(
      ofg::RenderTarget{
        fake_texture_view(),
        WGPUTextureFormat_RGBA8Unorm,
        800,
        450
      },
      WGPUTextureFormat_RGBA8Unorm,
      800,
      450,
      error
    )
  );
  CHECK(error.empty());
}

// Verifies Game creation rejects invalid setup before calling WebGPU.
TEST_CASE("Game create validates color format and GPU handles") {
  std::string error;

  CHECK(
    ofg::Game::create(
      ofg::GpuContext{},
      WGPUTextureFormat_RGBA8Unorm,
      error
    ) == nullptr
  );
  CHECK(error.find("device and queue") != std::string::npos);

  CHECK(
    ofg::Game::create(
      ofg::GpuContext{},
      WGPUTextureFormat_Undefined,
      error
    ) == nullptr
  );
  CHECK(error.find("defined color format") != std::string::npos);
}
