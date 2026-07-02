// Doctest coverage for camera component controls.
//
// These tests validate camera behavior without browser input so TypeScript can
// remain a raw control bridge while C++ owns camera modes and transforms.
#include "doctest.h"

#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

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
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    return ofg::math::vec3(forward.x, forward.y, forward.z);
}

// Builds an update context for one camera and optional player.
ofg::SceneUpdateContext context_for(
    ofg::ControlInput& input, float delta_seconds, ofg::Player* player, ofg::Camera* camera) {
    return ofg::SceneUpdateContext{input, 1000.0, delta_seconds, player, camera};
}

} // namespace

// Verifies debug mode movement matches the old fly-camera behavior.
TEST_CASE("camera debug mode moves with raw controls and clamps diagonal speed") {
    ofg::Scene scene;
    ofg::Camera& camera = add_root_camera(scene);
    ofg::ControlInput input;
    input.m_move_z = 1.0f;

    ofg::SceneUpdateContext first_context = context_for(input, 0.0f, nullptr, &camera);
    camera.update(first_context);
    CHECK(camera_position(camera).z == doctest::Approx(0.0f));

    ofg::SceneUpdateContext second_context = context_for(input, 0.016f, nullptr, &camera);
    camera.update(second_context);
    CHECK(camera_position(camera).z == doctest::Approx(0.08f).epsilon(0.0001));

    input = ofg::ControlInput{};
    input.m_move_x = 1.0f;
    input.m_move_y = 1.0f;
    input.m_fast = true;
    ofg::SceneUpdateContext diagonal_context = context_for(input, 0.1f, nullptr, &camera);
    camera.update(diagonal_context);
    const ofg::math::Vec3 position = camera_position(camera);
    CHECK(position.x == doctest::Approx(1.4142135f).epsilon(0.0001));
    CHECK(position.y == doctest::Approx(1.4142135f).epsilon(0.0001));
}

// Verifies camera mode cycling follows the public debug-status order.
TEST_CASE("camera cycles through debug first-person and third-person modes") {
    ofg::Scene scene;
    ofg::Camera& camera = add_root_camera(scene);
    ofg::ControlInput input;
    input.m_cycle_camera_mode = true;
    ofg::SceneUpdateContext context = context_for(input, 0.0f, nullptr, &camera);

    CHECK(camera.control_mode() == ofg::CameraControlMode::Debug);
    camera.update(context);
    CHECK(camera.control_mode() == ofg::CameraControlMode::FirstPerson);
    CHECK(std::string(ofg::camera_control_mode_name(camera.control_mode())) == "first_person");
    camera.update(context);
    CHECK(camera.control_mode() == ofg::CameraControlMode::ThirdPerson);
    CHECK(std::string(ofg::camera_control_mode_name(camera.control_mode())) == "third_person");
    camera.update(context);
    CHECK(camera.control_mode() == ofg::CameraControlMode::Debug);
    CHECK(std::string(ofg::camera_control_mode_name(camera.control_mode())) == "debug");
}

// Verifies mouse look applies yaw/pitch and clamps vertical pitch before flipping.
TEST_CASE("camera controls apply mouse look and clamp pitch") {
    ofg::Scene scene;
    ofg::Camera& camera = add_root_camera(scene);
    ofg::ControlInput input;
    input.m_look_active = true;
    input.m_look_delta_x = 100.0f;
    input.m_look_delta_y = -10000.0f;
    ofg::SceneUpdateContext context = context_for(input, 0.0f, nullptr, &camera);
    camera.update(context);

    const ofg::math::Vec3 forward = camera_forward(camera);
    CHECK(forward.x > 0.0f);
    CHECK(forward.y == doctest::Approx(std::sin(89.0f * 3.14159265358979323846f / 180.0f)).epsilon(0.0001));
    CHECK(forward.z > 0.0f);
}

// Verifies first-person mode follows the already-updated player position.
TEST_CASE("first-person camera follows the player at eye height") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    player_entity->local_transform().m_position = ofg::math::vec3(2.0f, 0.9f, -3.0f);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);

    ofg::ControlInput input;
    ofg::SceneUpdateContext context = context_for(input, 0.0f, player, camera);
    camera->update(context);

    const ofg::math::Vec3 position = camera_position(*camera);
    CHECK(position.x == doctest::Approx(2.0f));
    CHECK(position.y == doctest::Approx(1.6f));
    CHECK(position.z == doctest::Approx(-2.72f));
}

// Verifies third-person mode follows behind and above the player.
TEST_CASE("third-person camera follows behind the player") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    player_entity->local_transform().m_position.y = 0.9f;
    camera->set_control_mode(ofg::CameraControlMode::ThirdPerson);

    ofg::ControlInput input;
    ofg::SceneUpdateContext context = context_for(input, 0.0f, player, camera);
    camera->update(context);

    const ofg::math::Vec3 position = camera_position(*camera);
    CHECK(position.x == doctest::Approx(0.0f));
    CHECK(position.y == doctest::Approx(1.45f));
    CHECK(position.z == doctest::Approx(-4.0f));
}

// Verifies validation rejects non-finite raw controls.
TEST_CASE("camera update rejects invalid controls and delta") {
    ofg::Scene scene;
    ofg::Camera& camera = add_root_camera(scene);
    ofg::ControlInput input;
    input.m_move_x = std::numeric_limits<float>::infinity();
    ofg::SceneUpdateContext context = context_for(input, 0.0f, nullptr, &camera);

    CHECK_THROWS_WITH_AS(camera.update(context), doctest::Contains("finite"), ofg::EngineError);
    input = ofg::ControlInput{};
    ofg::SceneUpdateContext bad_delta = context_for(input, -1.0f, nullptr, &camera);
    CHECK_THROWS_WITH_AS(camera.update(bad_delta), doctest::Contains("delta"), ofg::EngineError);
}
