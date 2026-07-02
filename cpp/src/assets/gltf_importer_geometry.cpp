// Internal geometry helpers for glTF primitive import.
#include "gltf_importer_geometry.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ofg::gltf_importer_detail {
namespace {

constexpr float _generated_vector_epsilon = 0.000001f;

// Returns a vertex position as Vec3.
math::Vec3 vertex_position(const MeshVertex& vertex) noexcept {
    return math::vec3(vertex.m_position[0], vertex.m_position[1], vertex.m_position[2]);
}

// Returns a vertex normal as Vec3.
math::Vec3 vertex_normal(const MeshVertex& vertex) noexcept {
    return math::vec3(vertex.m_normal[0], vertex.m_normal[1], vertex.m_normal[2]);
}

// Returns a vertex UV as Vec2.
math::Vec2 vertex_uv(const MeshVertex& vertex) noexcept {
    return math::vec2(vertex.m_uv[0], vertex.m_uv[1]);
}

// Adds one vector into an accumulator.
void add_to(math::Vec3& target, math::Vec3 value) noexcept {
    target = math::add(target, value);
}

// Builds a stable tangent when UVs cannot provide one for a vertex.
math::Vec3 fallback_tangent_for_normal(math::Vec3 normal) {
    const math::Vec3 reference =
        std::abs(normal.y) < 0.9f ? math::vec3(0.0f, 1.0f, 0.0f) : math::vec3(1.0f, 0.0f, 0.0f);
    std::string error;
    const std::optional<math::Vec3> tangent = math::normalize(math::cross(reference, normal), error);
    if (!tangent.has_value()) {
        throw EngineError("glTF primitive cannot create a fallback tangent for an invalid normal.");
    }
    return *tangent;
}

} // namespace

// Generates smooth vertex normals for a supported triangle primitive.
void generate_normals(std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    std::uint32_t index_start,
    std::uint32_t index_count,
    std::uint32_t vertex_base,
    std::uint32_t vertex_count) {
    if (index_count % 3U != 0U) {
        throw EngineError("glTF triangle-list primitive index count must be divisible by three.");
    }

    std::vector<math::Vec3> normals(vertex_count, math::vec3(0.0f, 0.0f, 0.0f));
    for (std::uint32_t index = 0; index < index_count; index += 3U) {
        const std::uint32_t i0 = indices[index_start + index] - vertex_base;
        const std::uint32_t i1 = indices[index_start + index + 1U] - vertex_base;
        const std::uint32_t i2 = indices[index_start + index + 2U] - vertex_base;
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            throw EngineError("glTF primitive index references a vertex outside its primitive.");
        }

        const math::Vec3 p0 = vertex_position(vertices[vertex_base + i0]);
        const math::Vec3 p1 = vertex_position(vertices[vertex_base + i1]);
        const math::Vec3 p2 = vertex_position(vertices[vertex_base + i2]);
        const math::Vec3 face_normal = math::cross(math::sub(p1, p0), math::sub(p2, p0));
        if (math::length_squared(face_normal) <= _generated_vector_epsilon) {
            throw EngineError("glTF primitive cannot generate normals from a degenerate triangle.");
        }
        add_to(normals[i0], face_normal);
        add_to(normals[i1], face_normal);
        add_to(normals[i2], face_normal);
    }

    for (std::uint32_t local_index = 0; local_index < vertex_count; ++local_index) {
        std::string error;
        const std::optional<math::Vec3> normal = math::normalize(normals[local_index], error);
        if (!normal.has_value()) {
            throw EngineError("glTF primitive cannot generate a valid vertex normal.");
        }
        vertices[vertex_base + local_index].m_normal = {normal->x, normal->y, normal->z};
    }
}

// Generates tangent vectors for a triangle primitive, skipping degenerate UV triangles.
void generate_tangents(std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    std::uint32_t index_start,
    std::uint32_t index_count,
    std::uint32_t vertex_base,
    std::uint32_t vertex_count) {
    if (index_count % 3U != 0U) {
        throw EngineError("glTF triangle-list primitive index count must be divisible by three.");
    }

    std::vector<math::Vec3> tangents(vertex_count, math::vec3(0.0f, 0.0f, 0.0f));
    std::vector<math::Vec3> bitangents(vertex_count, math::vec3(0.0f, 0.0f, 0.0f));
    for (std::uint32_t index = 0; index < index_count; index += 3U) {
        const std::uint32_t i0 = indices[index_start + index] - vertex_base;
        const std::uint32_t i1 = indices[index_start + index + 1U] - vertex_base;
        const std::uint32_t i2 = indices[index_start + index + 2U] - vertex_base;
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            throw EngineError("glTF primitive index references a vertex outside its primitive.");
        }
        const MeshVertex& v0 = vertices[vertex_base + i0];
        const MeshVertex& v1 = vertices[vertex_base + i1];
        const MeshVertex& v2 = vertices[vertex_base + i2];

        const math::Vec3 p0 = vertex_position(v0);
        const math::Vec3 p1 = vertex_position(v1);
        const math::Vec3 p2 = vertex_position(v2);
        const math::Vec2 uv0 = vertex_uv(v0);
        const math::Vec2 uv1 = vertex_uv(v1);
        const math::Vec2 uv2 = vertex_uv(v2);
        const math::Vec3 edge1 = math::sub(p1, p0);
        const math::Vec3 edge2 = math::sub(p2, p0);
        const math::Vec2 delta_uv1 = math::vec2(uv1.x - uv0.x, uv1.y - uv0.y);
        const math::Vec2 delta_uv2 = math::vec2(uv2.x - uv0.x, uv2.y - uv0.y);
        const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::abs(determinant) <= _generated_vector_epsilon) {
            continue;
        }
        const float scale = 1.0f / determinant;
        const math::Vec3 tangent =
            math::mul(math::sub(math::mul(edge1, delta_uv2.y), math::mul(edge2, delta_uv1.y)), scale);
        const math::Vec3 bitangent =
            math::mul(math::sub(math::mul(edge2, delta_uv1.x), math::mul(edge1, delta_uv2.x)), scale);
        add_to(tangents[i0], tangent);
        add_to(tangents[i1], tangent);
        add_to(tangents[i2], tangent);
        add_to(bitangents[i0], bitangent);
        add_to(bitangents[i1], bitangent);
        add_to(bitangents[i2], bitangent);
    }

    for (std::uint32_t local_index = 0; local_index < vertex_count; ++local_index) {
        MeshVertex& vertex = vertices[vertex_base + local_index];
        const math::Vec3 normal = vertex_normal(vertex);
        const math::Vec3 tangent_source =
            math::sub(tangents[local_index], math::mul(normal, math::dot(normal, tangents[local_index])));
        std::string error;
        const std::optional<math::Vec3> generated_tangent = math::normalize(tangent_source, error);
        const math::Vec3 tangent =
            generated_tangent.has_value() ? *generated_tangent : fallback_tangent_for_normal(normal);
        const float sign =
            math::length_squared(bitangents[local_index]) <= _generated_vector_epsilon
                ? 1.0f
                : (math::dot(math::cross(normal, tangent), bitangents[local_index]) < 0.0f ? -1.0f : 1.0f);
        vertex.m_tangent = {tangent.x, tangent.y, tangent.z, sign};
    }
}

} // namespace ofg::gltf_importer_detail
