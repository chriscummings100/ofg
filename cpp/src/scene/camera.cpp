// Camera scene component implementation.
#include "ofg/scene/camera.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/entity.hpp"

#include <cmath>

namespace ofg {
namespace {

constexpr float _pi = 3.14159265358979323846f;
constexpr float _default_vertical_fov_radians = 55.0f * _pi / 180.0f;

// Validates perspective projection settings shared by construction and mutation paths.
void validate_perspective(float vertical_fov_radians, float near_z, float far_z) {
    if (!std::isfinite(vertical_fov_radians) || !std::isfinite(near_z) || !std::isfinite(far_z)) {
        throw EngineError("Camera perspective values must be finite.");
    }
    if (vertical_fov_radians <= 0.0f || vertical_fov_radians >= _pi) {
        throw EngineError("Camera vertical field of view must be greater than 0 and less than pi radians.");
    }
    if (near_z <= 0.0f || far_z <= near_z) {
        throw EngineError("Camera perspective requires 0 < near_z < far_z.");
    }
}

// Builds a local-to-parent matrix from translation and rotation only.
math::Mat4 parent_from_camera_local(const LocalTransform& transform) noexcept {
    return math::mul(math::mat4_translation(transform.m_position), math::mat4_from_quat(transform.m_rotation));
}

// Resolves the camera entity transform into world space while ignoring scale.
math::Mat4 world_from_camera_entity(const Entity& entity) noexcept {
    math::Mat4 world_from_camera = math::mat4_identity();
    for (const Entity* current = &entity; current != nullptr; current = current->parent()) {
        world_from_camera = math::mul(parent_from_camera_local(current->local_transform()), world_from_camera);
    }
    return world_from_camera;
}

// Returns a Vec3 from a matrix column used as a direction vector.
math::Vec3 direction_from_column(math::Vec4 column) noexcept {
    return math::vec3(column.x, column.y, column.z);
}

} // namespace

// Binds this camera to one scene-owned entity.
Camera::Camera(Entity* entity) noexcept
    : Component(ComponentType::Camera, entity), m_vertical_fov_radians(_default_vertical_fov_radians) {}

// Returns the vertical perspective field of view in radians.
float Camera::vertical_fov_radians() const noexcept {
    return m_vertical_fov_radians;
}

// Returns the near clip distance.
float Camera::near_z() const noexcept {
    return m_near_z;
}

// Returns the far clip distance.
float Camera::far_z() const noexcept {
    return m_far_z;
}

// Replaces perspective projection settings after validating their range.
void Camera::set_perspective(float vertical_fov_radians, float near_z, float far_z) {
    validate_perspective(vertical_fov_radians, near_z, far_z);
    m_vertical_fov_radians = vertical_fov_radians;
    m_near_z = near_z;
    m_far_z = far_z;
}

// Resolves this camera and its owning entity into renderer-facing properties.
CameraProperties Camera::camera_properties(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0.0f) {
        throw EngineError("Camera properties require a positive finite aspect ratio.");
    }
    if (entity() == nullptr) {
        throw EngineError("Camera properties require an owning entity.");
    }

    const math::Mat4 world_from_camera = world_from_camera_entity(*entity());
    const math::Vec3 eye = direction_from_column(world_from_camera[3]);
    const math::Vec3 camera_up = direction_from_column(world_from_camera[1]);
    const math::Vec3 camera_back = direction_from_column(world_from_camera[2]);
    const math::Vec3 camera_forward = math::mul(camera_back, -1.0f);
    const math::Vec3 target = math::add(eye, camera_forward);

    CameraProperties properties =
        camera_properties_from_look_at(this, eye, target, camera_up, m_vertical_fov_radians, aspect, m_near_z, m_far_z);
    properties.world_from_camera = world_from_camera;
    return properties;
}

} // namespace ofg
