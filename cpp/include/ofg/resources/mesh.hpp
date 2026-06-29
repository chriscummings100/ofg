// Mutable indexed mesh resource for OFG renderer geometry.
//
// Meshes store CPU vertices, indices, and submesh ranges, and they eagerly
// prepare WebGPU vertex/index buffers when created with a ready GpuContext.
#pragma once

#include "ofg/game/gpu_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

class Material;

struct MeshVertex {
    std::array<float, 3> m_position{};
    std::array<float, 3> m_normal{};
    std::array<float, 2> m_uv{};
};

struct SubMesh {
    std::string m_label;
    std::uint32_t m_index_start{0};
    std::uint32_t m_index_count{0};
    Material* m_default_material{nullptr};
};

class Mesh {
public:
    // Allocates a labeled mesh resource using the creating Resources context.
    Mesh(GpuContext gpu, std::string label);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();

    // Initializes this mesh and validates vertices, indices, and submesh ranges.
    void init(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes);

    // Replaces vertices when the existing indices remain valid.
    void replace_vertices(std::vector<MeshVertex> vertices);
    // Replaces indices and submeshes after validating ranges and materials.
    void replace_indices(std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes);
    // Returns the mesh label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns immutable CPU vertices.
    [[nodiscard]] std::span<const MeshVertex> vertices() const noexcept;
    // Returns immutable CPU indices.
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    // Returns immutable submesh ranges.
    [[nodiscard]] std::span<const SubMesh> submeshes() const noexcept;
    // Returns the WebGPU vertex buffer, null for CPU-only resources.
    [[nodiscard]] WGPUBuffer vertex_buffer() const noexcept;
    // Returns the WebGPU index buffer, null for CPU-only resources.
    [[nodiscard]] WGPUBuffer index_buffer() const noexcept;
    // Returns the mesh revision.
    [[nodiscard]] std::uint64_t revision() const noexcept;

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
    WGPUBuffer m_vertex_buffer{nullptr};
    WGPUBuffer m_index_buffer{nullptr};
    std::uint64_t m_revision{0};
};

// Returns the byte stride of MeshVertex for WebGPU vertex buffers.
[[nodiscard]] constexpr std::size_t mesh_vertex_stride_bytes() noexcept {
    return sizeof(MeshVertex);
}

} // namespace ofg
