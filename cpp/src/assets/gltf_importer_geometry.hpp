// Internal geometry helpers for glTF primitive import.
//
// These helpers keep derived vertex-data generation out of the structural
// glTF-to-ModelResource importer.
#pragma once

#include "ofg/resources/mesh.hpp"

#include <cstdint>
#include <vector>

namespace ofg::gltf_importer_detail {

// Generates smooth vertex normals for a supported triangle primitive.
void generate_normals(std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    std::uint32_t index_start,
    std::uint32_t index_count,
    std::uint32_t vertex_base,
    std::uint32_t vertex_count);

// Generates tangent vectors for a triangle primitive, tolerating degenerate UV triangles.
void generate_tangents(std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    std::uint32_t index_start,
    std::uint32_t index_count,
    std::uint32_t vertex_base,
    std::uint32_t vertex_count);

} // namespace ofg::gltf_importer_detail
