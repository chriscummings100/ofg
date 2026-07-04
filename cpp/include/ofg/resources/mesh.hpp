// Mutable indexed mesh resource for OFG renderer geometry.
//
// Meshes store CPU vertices, indices, and submesh ranges, and they eagerly
// prepare WebGPU vertex/index buffers when created with a ready GpuContext.
#pragma once

#include "ofg/core/object.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/resources/material.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

struct MeshVertex {
    std::array<float, 3> m_position{};
    std::array<float, 3> m_normal{};
    std::array<float, 4> m_tangent{1.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 2> m_uv{};

    constexpr MeshVertex() noexcept = default;

    constexpr MeshVertex(std::array<float, 3> position, std::array<float, 3> normal, std::array<float, 2> uv) noexcept
        : m_position(position), m_normal(normal), m_uv(uv) {}

    constexpr MeshVertex(std::array<float, 3> position,
        std::array<float, 3> normal,
        std::array<float, 4> tangent,
        std::array<float, 2> uv) noexcept
        : m_position(position), m_normal(normal), m_tangent(tangent), m_uv(uv) {}
};

struct SubMesh {
    std::string m_label;
    std::uint32_t m_index_start{0};
    std::uint32_t m_index_count{0};
    Ptr<Material> m_default_material;
};

class Mesh : public Object {
public:
    // Allocates a labeled mesh resource using the creating Resources context.
    Mesh(GpuContext gpu, std::string label);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) = delete;
    Mesh& operator=(Mesh&& other) = delete;
    ~Mesh() override;

    // Initializes this mesh and validates vertices, indices, and submesh ranges.
    void init(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes);
    // Initializes a fixed-size dynamic vertex mesh for per-frame vertex updates.
    void init_dynamic_vertices(
        std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes);

    // Replaces vertices when the existing indices remain valid.
    void replace_vertices(std::vector<MeshVertex> vertices);
    // Updates an already-initialized dynamic vertex mesh without recreating GPU buffers.
    void update_vertices_in_place(std::span<const MeshVertex> vertices);
    // Replaces indices and submeshes after validating ranges and materials.
    void replace_indices(std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes);
    // Returns the mesh label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns the borrowed GPU context used by this mesh, or an empty context for CPU-only meshes.
    [[nodiscard]] GpuContext gpu_context() const noexcept;
    // Returns immutable CPU vertices.
    [[nodiscard]] std::span<const MeshVertex> vertices() const noexcept;
    // Returns immutable CPU indices.
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    // Returns immutable submesh ranges.
    [[nodiscard]] std::span<const SubMesh> submeshes() const noexcept;
    // Returns local-space bounds computed from CPU vertex positions.
    [[nodiscard]] Bounds3 local_bounds() const noexcept;
    // Returns the WebGPU vertex buffer, null for CPU-only resources.
    [[nodiscard]] WGPUBuffer vertex_buffer() const noexcept;
    // Returns the WebGPU index buffer, null for CPU-only resources.
    [[nodiscard]] WGPUBuffer index_buffer() const noexcept;
    // Returns whether this mesh supports fixed-capacity in-place vertex updates.
    [[nodiscard]] bool is_dynamic_vertex_mesh() const noexcept;
    // Returns the mesh revision.
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // Reports how many GPU vertex buffers this mesh has created.
    [[nodiscard]] std::uint64_t vertex_buffer_create_count() const noexcept;
    // Reports bytes uploaded to this mesh's GPU vertex buffer.
    [[nodiscard]] std::uint64_t vertex_upload_bytes() const noexcept;

private:
    // Creates GPU vertex and index buffers from CPU mesh data.
    void prepare_gpu_state();
    // Releases all owned WebGPU mesh buffers.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    std::string m_label;
    std::vector<MeshVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    std::vector<SubMesh> m_submeshes;
    Bounds3 m_local_bounds{};
    WGPUBuffer m_vertex_buffer{nullptr};
    WGPUBuffer m_index_buffer{nullptr};
    bool m_dynamic_vertices{false};
    std::size_t m_dynamic_vertex_capacity{0};
    std::uint64_t m_revision{0};
    std::uint64_t m_vertex_buffer_create_count{0};
    std::uint64_t m_vertex_upload_bytes{0};
};

// Returns the byte stride of MeshVertex for WebGPU vertex buffers.
[[nodiscard]] constexpr std::size_t mesh_vertex_stride_bytes() noexcept {
    return sizeof(MeshVertex);
}

} // namespace ofg
