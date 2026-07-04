// Conservative CPU-side bounds used by renderer culling and future shadows.
#include "ofg/render/bounds.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/resources/mesh.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <span>

namespace ofg {
namespace {

// Returns whether a vector has only finite components.
bool vec3_is_finite(math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Returns a MeshVertex position as a Vec3.
math::Vec3 vertex_position(const MeshVertex& vertex) noexcept {
    return math::vec3(vertex.m_position[0], vertex.m_position[1], vertex.m_position[2]);
}

// Returns the component-wise minimum.
math::Vec3 min_vec3(math::Vec3 left, math::Vec3 right) noexcept {
    return math::vec3(std::min(left.x, right.x), std::min(left.y, right.y), std::min(left.z, right.z));
}

// Returns the component-wise maximum.
math::Vec3 max_vec3(math::Vec3 left, math::Vec3 right) noexcept {
    return math::vec3(std::max(left.x, right.x), std::max(left.y, right.y), std::max(left.z, right.z));
}

// Throws when callers pass an invalid bounds value into a culling helper.
void validate_bounds(Bounds3 bounds) {
    if (!bounds_is_valid(bounds)) {
        throw EngineError("Bounds must be finite and ordered.");
    }
}

} // namespace

// Returns whether the bounds has finite ordered corners.
bool bounds_is_valid(Bounds3 bounds) noexcept {
    return vec3_is_finite(bounds.m_min) && vec3_is_finite(bounds.m_max) && bounds.m_min.x <= bounds.m_max.x &&
           bounds.m_min.y <= bounds.m_max.y && bounds.m_min.z <= bounds.m_max.z;
}

// Computes local-space bounds from mesh CPU vertices.
Bounds3 mesh_vertex_bounds(std::span<const MeshVertex> vertices) {
    if (vertices.empty()) {
        throw EngineError("Mesh bounds require at least one vertex.");
    }

    math::Vec3 minimum = vertex_position(vertices.front());
    if (!vec3_is_finite(minimum)) {
        throw EngineError("Mesh vertex positions must be finite for bounds.");
    }
    math::Vec3 maximum = minimum;
    for (const MeshVertex& vertex : vertices.subspan(1)) {
        const math::Vec3 position = vertex_position(vertex);
        if (!vec3_is_finite(position)) {
            throw EngineError("Mesh vertex positions must be finite for bounds.");
        }
        minimum = min_vec3(minimum, position);
        maximum = max_vec3(maximum, position);
    }
    return Bounds3{minimum, maximum};
}

// Transforms an axis-aligned box and returns the conservative world-space AABB.
Bounds3 transform_bounds(Bounds3 bounds, math::Mat4 world_from_local) {
    validate_bounds(bounds);

    const math::Vec3 min = bounds.m_min;
    const math::Vec3 max = bounds.m_max;
    const math::Vec3 corners[8]{
        math::vec3(min.x, min.y, min.z),
        math::vec3(max.x, min.y, min.z),
        math::vec3(min.x, max.y, min.z),
        math::vec3(max.x, max.y, min.z),
        math::vec3(min.x, min.y, max.z),
        math::vec3(max.x, min.y, max.z),
        math::vec3(min.x, max.y, max.z),
        math::vec3(max.x, max.y, max.z),
    };

    math::Vec3 minimum = math::transform_point(world_from_local, corners[0]);
    if (!vec3_is_finite(minimum)) {
        throw EngineError("Transformed bounds must be finite.");
    }
    math::Vec3 maximum = minimum;
    for (std::size_t index = 1; index < 8; ++index) {
        const math::Vec3 point = math::transform_point(world_from_local, corners[index]);
        if (!vec3_is_finite(point)) {
            throw EngineError("Transformed bounds must be finite.");
        }
        minimum = min_vec3(minimum, point);
        maximum = max_vec3(maximum, point);
    }
    return Bounds3{minimum, maximum};
}

// Builds a conservative sphere around a bounds value.
BoundingSphere bounding_sphere_from_bounds(Bounds3 bounds) {
    validate_bounds(bounds);
    const math::Vec3 center = math::mul(math::add(bounds.m_min, bounds.m_max), 0.5f);
    return BoundingSphere{center, math::length(math::sub(bounds.m_max, center))};
}

} // namespace ofg
