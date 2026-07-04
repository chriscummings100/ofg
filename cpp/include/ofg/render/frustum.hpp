// Plane-set culling primitives for camera and future shadow passes.
//
// Plane normals point inward and use the equation dot(normal, point) +
// distance >= 0 for the accepted half-space. Touching a plane is visible.
#pragma once

#include "ofg/math/vec.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/camera_properties.hpp"

#include <array>
#include <span>

namespace ofg {

struct CullingPlane {
    math::Vec3 m_normal;
    float m_distance{0.0f};
};

struct CullingPlaneSet {
    std::span<const CullingPlane> m_planes;
};

class ViewFrustum {
public:
    // Stores the six inward-facing frustum planes in left/right/bottom/top/near/far order.
    explicit ViewFrustum(std::array<CullingPlane, 6> planes) noexcept;
    // Returns the frustum planes as a non-owning plane set.
    [[nodiscard]] CullingPlaneSet plane_set() const noexcept;
    // Returns immutable frustum plane storage.
    [[nodiscard]] std::span<const CullingPlane> planes() const noexcept;

private:
    std::array<CullingPlane, 6> m_planes;
};

// Builds a normalized inward-facing culling plane.
[[nodiscard]] CullingPlane make_culling_plane(math::Vec3 normal, float distance);

// Returns signed distance to a culling plane.
[[nodiscard]] float signed_distance_to_plane(CullingPlane plane, math::Vec3 point) noexcept;

// Tests whether an AABB intersects every accepted half-space.
[[nodiscard]] bool intersects_culling_planes(Bounds3 world_bounds, CullingPlaneSet planes);

// Tests whether a bounding sphere intersects every accepted half-space.
[[nodiscard]] bool intersects_culling_planes(BoundingSphere sphere, CullingPlaneSet planes);

// Extracts camera frustum planes from the resolved clip-from-world matrix.
[[nodiscard]] ViewFrustum view_frustum_from_camera(const CameraProperties& camera);

// Extracts frustum planes from any clip-from-world matrix.
[[nodiscard]] ViewFrustum view_frustum_from_clip_from_world(math::Mat4 clip_from_world);

} // namespace ofg
