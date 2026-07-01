// Renderer-facing camera snapshot values.
#include "ofg/render/camera_properties.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"

#include <optional>
#include <string>

namespace ofg {
namespace {

// Builds the camera-to-world transform that corresponds to a right-handed look-at view.
math::Mat4 world_from_look_at(math::Vec3 eye, math::Vec3 target, math::Vec3 up, std::string& error) {
    std::optional<math::Vec3> forward = math::normalize(math::sub(target, eye), error);
    if (!forward.has_value()) {
        error = "Camera properties eye and target must be distinct.";
        return math::mat4_identity();
    }

    std::optional<math::Vec3> right = math::normalize(math::cross(*forward, up), error);
    if (!right.has_value()) {
        error = "Camera properties up vector must not be parallel to the view direction.";
        return math::mat4_identity();
    }

    const math::Vec3 camera_up = math::cross(*right, *forward);
    math::Mat4 matrix;
    matrix[0] = math::vec4(right->x, right->y, right->z, 0.0f);
    matrix[1] = math::vec4(camera_up.x, camera_up.y, camera_up.z, 0.0f);
    matrix[2] = math::vec4(-forward->x, -forward->y, -forward->z, 0.0f);
    matrix[3] = math::vec4(eye.x, eye.y, eye.z, 1.0f);
    error.clear();
    return matrix;
}

} // namespace

// Builds camera properties from explicit right-handed look-at inputs.
CameraProperties camera_properties_from_look_at(const Camera* camera,
    math::Vec3 eye,
    math::Vec3 target,
    math::Vec3 up,
    float vertical_fov_radians,
    float aspect,
    float near_z,
    float far_z) {
    std::string error;
    const math::Mat4 world_from_camera = world_from_look_at(eye, target, up, error);
    if (!error.empty()) {
        throw EngineError(error);
    }

    std::optional<math::Mat4> clip_from_camera =
        math::perspective_rh(vertical_fov_radians, aspect, near_z, far_z, error);
    if (!clip_from_camera.has_value()) {
        throw EngineError(error.empty() ? "Camera properties projection creation failed." : error);
    }

    CameraProperties properties;
    properties.camera = camera;
    properties.world_from_camera = world_from_camera;
    std::optional<math::Mat4> camera_from_world = math::look_at_rh(eye, target, up, error);
    if (!camera_from_world.has_value()) {
        throw EngineError(error.empty() ? "Camera properties view creation failed." : error);
    }
    properties.camera_from_world = *camera_from_world;
    properties.clip_from_camera = *clip_from_camera;
    properties.clip_from_world = math::mul(*clip_from_camera, *camera_from_world);
    properties.vertical_fov_radians = vertical_fov_radians;
    properties.aspect = aspect;
    properties.near_z = near_z;
    properties.far_z = far_z;
    return properties;
}

} // namespace ofg
