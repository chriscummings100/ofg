// Conservative CPU-side bounds used by renderer culling and future shadows.
//
// Bounds are ordinary finite axis-aligned boxes in local or world space.
// Helpers deliberately favor conservative inclusion so culling cannot remove
// objects that touch a plane or grow under non-uniform world transforms.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <span>

namespace ofg {

struct MeshVertex;

struct Bounds3 {
    math::Vec3 m_min;
    math::Vec3 m_max;
};

struct BoundingSphere {
    math::Vec3 m_center;
    float m_radius{0.0f};
};

// Returns whether the bounds has finite ordered corners.
[[nodiscard]] bool bounds_is_valid(Bounds3 bounds) noexcept;

// Computes local-space bounds from mesh CPU vertices.
[[nodiscard]] Bounds3 mesh_vertex_bounds(std::span<const MeshVertex> vertices);

// Transforms an axis-aligned box and returns the conservative world-space AABB.
[[nodiscard]] Bounds3 transform_bounds(Bounds3 bounds, math::Mat4 world_from_local);

// Builds a conservative sphere around a bounds value.
[[nodiscard]] BoundingSphere bounding_sphere_from_bounds(Bounds3 bounds);

} // namespace ofg
