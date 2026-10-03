// Immutable indexed triangle geometry, material slots and local bounds; GPU buffers are privately asset-owned.
#pragma once

#include "resources/material.h"
#include <span>
#include <vector>

namespace ofg {
struct MeshGpuData;
class Graphics;

struct Vertex
{
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
};

struct SubMesh
{
    uint32_t firstIndex;
    uint32_t indexCount;
    std::shared_ptr<Material> material;
};

struct Bounds
{
    math::Vec3 minimum;
    math::Vec3 maximum;
};

class Mesh : public Resource
{
public:
    // Validates and takes immutable geometry; indices address vertices and submesh ranges address indices.
    static std::shared_ptr<Mesh> create(
        std::vector<Vertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<SubMesh> subMeshes
    );
    // Returns the fixed-layout vertices.
    std::span<const Vertex> vertices() const noexcept { return m_vertices; }
    // Returns the shared triangle index buffer data.
    std::span<const uint32_t> indices() const noexcept { return m_indices; }
    // Returns submesh ranges and their default materials.
    std::span<const SubMesh> subMeshes() const noexcept { return m_subMeshes; }
    // Returns the local-space bounds computed from all vertices.
    const Bounds& bounds() const noexcept { return m_bounds; }

private:
    friend class Graphics;
    // Stores validated immutable geometry and its computed bounds.
    Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices, std::vector<SubMesh> subMeshes, Bounds bounds);
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<SubMesh> m_subMeshes;
    Bounds m_bounds;
    std::shared_ptr<MeshGpuData> m_gpu;
};
} // namespace ofg
