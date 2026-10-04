// Immutable triangle geometry and CPU deformation data; GPU buffers are privately asset-owned.
#pragma once

#include "resources/material.h"
#include <array>
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
    math::Vec4 tangent{}; // W = +/-1 for authored tangents; zero requests a derivative UV0 frame.
    math::Vec2 uv1{};
    math::Vec4 color{1, 1, 1, 1};
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

struct VertexJointInfluences
{
    std::array<uint32_t, 4> joints{}; // Indices into a Skin's palette, not Model nodes or EntityIds.
    math::Vec4 weights{};
};

struct MorphTarget
{
    // Dense per-vertex deltas. Empty arrays are accepted at creation and expanded to zeros.
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    std::vector<math::Vec3> tangents;
};

struct MeshDeformationData
{
    // One vertex-aligned array per JOINTS_n / WEIGHTS_n pair; no influence pruning occurs.
    std::vector<std::vector<VertexJointInfluences>> influenceSets;
    std::vector<MorphTarget> morphTargets;
    std::vector<float> defaultMorphWeights; // Empty defaults become one zero per target.
};

class Mesh : public Resource
{
public:
    // Validates and takes immutable geometry and optional vertex-aligned deformation data.
    // Indices address vertices; submesh ranges address indices. Bounds describe undeformed geometry.
    static std::shared_ptr<Mesh> create(
        std::vector<Vertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<SubMesh> subMeshes,
        MeshDeformationData deformation = {}
    );
    // Returns the fixed-layout vertices.
    std::span<const Vertex> vertices() const noexcept { return m_vertices; }
    // Returns the shared triangle index buffer data.
    std::span<const uint32_t> indices() const noexcept { return m_indices; }
    // Returns submesh ranges and their default materials.
    std::span<const SubMesh> subMeshes() const noexcept { return m_subMeshes; }
    // Returns the local-space bounds computed from all vertices.
    const Bounds& bounds() const noexcept { return m_bounds; }
    // Returns vertex influences, morph deltas and defaults independently of any live scene binding.
    const MeshDeformationData& deformation() const noexcept { return m_deformation; }

private:
    friend class Graphics;
    // Stores validated immutable geometry and its computed bounds.
    Mesh(
        std::vector<Vertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<SubMesh> subMeshes,
        Bounds bounds,
        MeshDeformationData deformation
    );
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<SubMesh> m_subMeshes;
    Bounds m_bounds;
    MeshDeformationData m_deformation;
    std::shared_ptr<MeshGpuData> m_gpu;
};
} // namespace ofg
