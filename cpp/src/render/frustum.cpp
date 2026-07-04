// Plane-set culling primitives for camera and future shadow passes.
#include "ofg/render/frustum.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <array>
#include <cstddef>
#include <cmath>
#include <span>

namespace ofg {
namespace {

// Returns one row from a column-major matrix.
math::Vec4 matrix_row(math::Mat4 matrix, std::size_t row) noexcept {
    return math::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
}

// Builds a plane from clip-space row coefficients.
CullingPlane plane_from_row(math::Vec4 row) {
    return make_culling_plane(math::vec3(row.x, row.y, row.z), row.w);
}

} // namespace

// Stores the six inward-facing frustum planes in left/right/bottom/top/near/far order.
ViewFrustum::ViewFrustum(std::array<CullingPlane, 6> planes) noexcept : m_planes(planes) {}

// Returns the frustum planes as a non-owning plane set.
CullingPlaneSet ViewFrustum::plane_set() const noexcept {
    return CullingPlaneSet{m_planes};
}

// Returns immutable frustum plane storage.
std::span<const CullingPlane> ViewFrustum::planes() const noexcept {
    return m_planes;
}

// Builds a normalized inward-facing culling plane.
CullingPlane make_culling_plane(math::Vec3 normal, float distance) {
    const float length = math::length(normal);
    if (!std::isfinite(length) || length <= 0.0f || !std::isfinite(distance)) {
        throw EngineError("Culling plane requires a finite nonzero normal and finite distance.");
    }
    const float inverse_length = 1.0f / length;
    return CullingPlane{math::mul(normal, inverse_length), distance * inverse_length};
}

// Returns signed distance to a culling plane.
float signed_distance_to_plane(CullingPlane plane, math::Vec3 point) noexcept {
    return math::dot(plane.m_normal, point) + plane.m_distance;
}

// Tests whether an AABB intersects every accepted half-space.
bool intersects_culling_planes(Bounds3 world_bounds, CullingPlaneSet planes) {
    if (!bounds_is_valid(world_bounds)) {
        throw EngineError("Culling requires valid world bounds.");
    }

    for (const CullingPlane& plane : planes.m_planes) {
        const math::Vec3 support = math::vec3(plane.m_normal.x >= 0.0f ? world_bounds.m_max.x : world_bounds.m_min.x,
            plane.m_normal.y >= 0.0f ? world_bounds.m_max.y : world_bounds.m_min.y,
            plane.m_normal.z >= 0.0f ? world_bounds.m_max.z : world_bounds.m_min.z);
        if (signed_distance_to_plane(plane, support) < 0.0f) {
            return false;
        }
    }
    return true;
}

// Tests whether a bounding sphere intersects every accepted half-space.
bool intersects_culling_planes(BoundingSphere sphere, CullingPlaneSet planes) {
    if (!std::isfinite(sphere.m_radius) || sphere.m_radius < 0.0f) {
        throw EngineError("Culling requires a finite non-negative bounding sphere radius.");
    }
    for (const CullingPlane& plane : planes.m_planes) {
        if (signed_distance_to_plane(plane, sphere.m_center) < -sphere.m_radius) {
            return false;
        }
    }
    return true;
}

// Extracts camera frustum planes from the resolved clip-from-world matrix.
ViewFrustum view_frustum_from_camera(const CameraProperties& camera) {
    return view_frustum_from_clip_from_world(camera.clip_from_world);
}

// Extracts frustum planes from any clip-from-world matrix.
ViewFrustum view_frustum_from_clip_from_world(math::Mat4 clip_from_world) {
    const math::Vec4 row0 = matrix_row(clip_from_world, 0);
    const math::Vec4 row1 = matrix_row(clip_from_world, 1);
    const math::Vec4 row2 = matrix_row(clip_from_world, 2);
    const math::Vec4 row3 = matrix_row(clip_from_world, 3);

    return ViewFrustum{std::array<CullingPlane, 6>{
        plane_from_row(math::vec4(row3.x + row0.x, row3.y + row0.y, row3.z + row0.z, row3.w + row0.w)),
        plane_from_row(math::vec4(row3.x - row0.x, row3.y - row0.y, row3.z - row0.z, row3.w - row0.w)),
        plane_from_row(math::vec4(row3.x + row1.x, row3.y + row1.y, row3.z + row1.z, row3.w + row1.w)),
        plane_from_row(math::vec4(row3.x - row1.x, row3.y - row1.y, row3.z - row1.z, row3.w - row1.w)),
        plane_from_row(row2),
        plane_from_row(math::vec4(row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w)),
    }};
}

} // namespace ofg
