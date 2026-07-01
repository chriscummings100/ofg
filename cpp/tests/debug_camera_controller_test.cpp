// Doctest coverage for the C++ debug fly camera controller.
//
// These tests validate camera movement without browser input so TypeScript can
// remain a raw input bridge while C++ owns camera behavior.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/game/debug_camera_controller.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/scene.hpp"

#include <cmath>
#include <limits>

namespace {

// Creates a scene with one root camera and returns the camera component.
ofg::Camera& add_root_camera(ofg::Scene& scene) {
    ofg::Component* component = scene.get_root()->create_component(ofg::ComponentType::Camera);
    REQUIRE(component != nullptr);
    REQUIRE(scene.get_root()->camera() != nullptr);
    return *scene.get_root()->camera();
}

// Returns the current camera world position from resolved properties.
ofg::math::Vec3 camera_position(const ofg::Camera& camera) {
    const ofg::CameraProperties properties = camera.camera_properties(1.0f);
    return ofg::math::vec3(
        properties.world_from_camera[3].x, properties.world_from_camera[3].y, properties.world_from_camera[3].z);
}

// Returns the current camera world forward direction from resolved properties.
ofg::math::Vec3 camera_forward(const ofg::Camera& camera) {
    const ofg::CameraProperties properties = camera.camera_properties(1.0f);
    const ofg::math::Vec4 forward =
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return ofg::math::vec3(forward.x, forward.y, forward.z);
}

} // namespace

// Verifies the first update captures timing without applying movement distance.
TEST_CASE("debug camera treats first movement update as zero delta") {
    ofg::Scene scene;
    const ofg::Camera& camera = add_root_camera(scene);
    ofg::DebugCameraController controller;

    ofg::DebugCameraInput input;
    input.move_z = 1.0f;
    controller.update(scene, input, 1000.0);
    CHECK(camera_position(camera).z == doctest::Approx(0.0f));

    controller.update(scene, input, 1016.0);
    CHECK(camera_position(camera).z == doctest::Approx(-0.08f).epsilon(0.0001));
}

// Verifies large frame gaps are clamped before movement is applied.
TEST_CASE("debug camera clamps large movement deltas") {
    ofg::Scene scene;
    const ofg::Camera& camera = add_root_camera(scene);
    ofg::DebugCameraController controller;

    ofg::DebugCameraInput input;
    input.move_z = 1.0f;
    controller.update(scene, input, 0.0);
    controller.update(scene, input, 10000.0);

    CHECK(camera_position(camera).z == doctest::Approx(-0.5f).epsilon(0.0001));
}

// Verifies diagonal movement is normalized and fast/slow modifiers are deterministic.
TEST_CASE("debug camera normalizes diagonal movement and applies speed modifiers") {
    ofg::Scene scene;
    const ofg::Camera& camera = add_root_camera(scene);
    ofg::DebugCameraController controller;

    ofg::DebugCameraInput input;
    input.move_x = 1.0f;
    input.move_z = 1.0f;
    input.fast = true;
    controller.update(scene, input, 0.0);
    controller.update(scene, input, 100.0);

    const ofg::math::Vec3 position = camera_position(camera);
    CHECK(position.x == doctest::Approx(1.4142135f).epsilon(0.0001));
    CHECK(position.y == doctest::Approx(0.0f));
    CHECK(position.z == doctest::Approx(-1.4142135f).epsilon(0.0001));

    input = ofg::DebugCameraInput{};
    input.move_x = 1.0f;
    input.slow = true;
    controller.reset();
    scene.get_root()->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 0.0f);
    controller.update(scene, input, 0.0);
    controller.update(scene, input, 100.0);
    CHECK(camera_position(camera).x == doctest::Approx(0.125f).epsilon(0.0001));
}

// Verifies mouse look applies yaw/pitch and clamps vertical pitch before flipping.
TEST_CASE("debug camera applies mouse look and clamps pitch") {
    ofg::Scene scene;
    const ofg::Camera& camera = add_root_camera(scene);
    ofg::DebugCameraController controller;

    ofg::DebugCameraInput input;
    input.look_active = true;
    input.look_delta_x = 100.0f;
    input.look_delta_y = -10000.0f;
    controller.update(scene, input, 0.0);

    const ofg::math::Vec3 forward = camera_forward(camera);
    CHECK(forward.x > 0.0f);
    CHECK(forward.y == doctest::Approx(std::sin(89.0f * 3.14159265358979323846f / 180.0f)).epsilon(0.0001));
    CHECK(forward.z < 0.0f);
}

// Verifies validation rejects non-finite raw input before controller state changes.
TEST_CASE("debug camera rejects non-finite input and update time") {
    ofg::DebugCameraInput input;
    input.move_x = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH_AS(ofg::validate_debug_camera_input(input), doctest::Contains("finite"), ofg::EngineError);

    ofg::Scene scene;
    add_root_camera(scene);
    ofg::DebugCameraController controller;
    CHECK_THROWS_WITH_AS(controller.update(scene, ofg::DebugCameraInput{}, std::numeric_limits<double>::infinity()),
        doctest::Contains("time"),
        ofg::EngineError);
}

// Verifies scenes without cameras are a recoverable no-op for the controller.
TEST_CASE("debug camera controller ignores scenes without cameras") {
    ofg::Scene scene;
    ofg::DebugCameraController controller;

    ofg::DebugCameraInput input;
    input.move_z = 1.0f;
    CHECK_NOTHROW(controller.update(scene, input, 0.0));
    CHECK_NOTHROW(controller.update(scene, input, 100.0));

    const ofg::Camera& camera = add_root_camera(scene);
    controller.update(scene, input, 200.0);
    CHECK(camera_position(camera).z == doctest::Approx(0.0f));

    controller.update(scene, input, 216.0);
    CHECK(camera_position(camera).z == doctest::Approx(-0.08f).epsilon(0.0001));
}
