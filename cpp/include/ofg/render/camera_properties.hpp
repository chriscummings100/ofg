// Renderer-facing camera snapshot values.
//
// CameraProperties is CPU-side frame data resolved from a scene Camera
// component. It keeps the source camera pointer plus matrices and projection
// facts together so renderer passes can share one coherent camera view without
// owning scene camera state. A look-at helper remains for focused comparisons
// and tests that need explicit camera inputs.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

namespace ofg {

class Camera;

struct CameraProperties {
    const Camera* camera{nullptr};
    math::Mat4 world_from_camera;
    math::Mat4 camera_from_world;
    math::Mat4 clip_from_camera;
    math::Mat4 clip_from_world;
    float vertical_fov_radians{0.0f};
    float aspect{1.0f};
    float near_z{0.1f};
    float far_z{80.0f};
};

// Builds camera properties from explicit right-handed look-at inputs.
[[nodiscard]] CameraProperties camera_properties_from_look_at(const Camera* camera,
    math::Vec3 eye,
    math::Vec3 target,
    math::Vec3 up,
    float vertical_fov_radians,
    float aspect,
    float near_z,
    float far_z);

} // namespace ofg
