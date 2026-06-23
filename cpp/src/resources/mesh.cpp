// Mutable indexed mesh resource for OFG renderer geometry.
#include "ofg/resources/mesh.hpp"

#include "ofg/resources/material.hpp"
#include "ofg/render/webgpu_common.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Validates index values against a vertex count.
bool validate_indices(const std::vector<std::uint32_t>& indices, std::size_t vertex_count, std::string& error) {
    if (indices.empty()) {
        error = "Mesh indices must not be empty.";
        return false;
    }
    for (std::uint32_t index : indices) {
        if (index >= vertex_count) {
            error = "Mesh index references a missing vertex.";
            return false;
        }
    }
    return true;
}

// Validates submesh ranges and default materials.
bool validate_submeshes(const std::vector<SubMesh>& submeshes, std::size_t index_count, std::string& error) {
    if (submeshes.empty()) {
        error = "Mesh requires at least one submesh.";
        return false;
    }
    for (const SubMesh& submesh : submeshes) {
        if (submesh.m_label.empty()) {
            error = "Submesh label must not be empty.";
            return false;
        }
        if (submesh.m_default_material == nullptr) {
            error = "Submesh default material must not be null.";
            return false;
        }
        const std::uint64_t range_end =
            static_cast<std::uint64_t>(submesh.m_index_start) + static_cast<std::uint64_t>(submesh.m_index_count);
        if (submesh.m_index_count == 0 || range_end > index_count) {
            error = "Submesh index range is outside the mesh index buffer.";
            return false;
        }
    }
    return true;
}

// Validates a complete mesh CPU data set.
bool validate_mesh_data(const std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    const std::vector<SubMesh>& submeshes,
    std::string& error) {
    if (vertices.empty()) {
        error = "Mesh vertices must not be empty.";
        return false;
    }
    return validate_indices(indices, vertices.size(), error) && validate_submeshes(submeshes, indices.size(), error);
}

// Creates a GPU buffer and uploads immutable CPU data into it.
WGPUBuffer create_gpu_buffer(const GpuContext& gpu,
    const std::string& label,
    const void* data,
    std::size_t byte_count,
    WGPUBufferUsage usage,
    std::string& error) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::string_view(label);
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    descriptor.size = byte_count;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu.m_device, &descriptor);
    if (buffer == nullptr) {
        error = "wgpuDeviceCreateBuffer returned null for mesh buffer '" + label + "'.";
        return nullptr;
    }

    wgpuQueueWriteBuffer(gpu.m_queue, buffer, 0, data, byte_count);
    return buffer;
}

} // namespace

// Stores validated mesh CPU data; use create() for validation.
Mesh::Mesh(GpuContext gpu,
    std::string label,
    std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<SubMesh> submeshes)
    : m_gpu(std::move(gpu)), m_label(std::move(label)), m_vertices(std::move(vertices)), m_indices(std::move(indices)),
      m_submeshes(std::move(submeshes)) {}

// Moves mesh CPU and GPU handles without duplicating ownership.
Mesh::Mesh(Mesh&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_label(std::move(other.m_label)), m_vertices(std::move(other.m_vertices)),
      m_indices(std::move(other.m_indices)), m_submeshes(std::move(other.m_submeshes)),
      m_vertex_buffer(other.m_vertex_buffer), m_index_buffer(other.m_index_buffer), m_revision(other.m_revision) {
    other.m_vertex_buffer = nullptr;
    other.m_index_buffer = nullptr;
}

// Moves mesh CPU and GPU handles without duplicating ownership.
Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release_gpu_state();
        m_gpu = std::move(other.m_gpu);
        m_label = std::move(other.m_label);
        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_submeshes = std::move(other.m_submeshes);
        m_vertex_buffer = other.m_vertex_buffer;
        m_index_buffer = other.m_index_buffer;
        m_revision = other.m_revision;
        other.m_vertex_buffer = nullptr;
        other.m_index_buffer = nullptr;
    }
    return *this;
}

// Releases owned GPU mesh buffers.
Mesh::~Mesh() {
    release_gpu_state();
}

// Creates a mesh and validates vertices, indices, and submesh ranges.
std::optional<Mesh> Mesh::create(GpuContext gpu,
    std::string label,
    std::vector<MeshVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<SubMesh> submeshes,
    std::string& error) {
    if (label.empty()) {
        error = "Mesh label must not be empty.";
        return std::nullopt;
    }
    if (!validate_mesh_data(vertices, indices, submeshes, error)) {
        return std::nullopt;
    }
    Mesh mesh(std::move(gpu), std::move(label), std::move(vertices), std::move(indices), std::move(submeshes));
    if (!mesh.prepare_gpu_state(error)) {
        return std::nullopt;
    }
    error.clear();
    return mesh;
}

// Replaces vertices when the existing indices remain valid.
bool Mesh::replace_vertices(std::vector<MeshVertex> vertices, std::string& error) {
    if (vertices.empty()) {
        error = "Mesh vertices must not be empty.";
        return false;
    }
    if (!validate_indices(m_indices, vertices.size(), error)) {
        return false;
    }
    WGPUBuffer next_vertex_buffer = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            error = "Mesh GPU preparation requires a WebGPU device and queue.";
            return false;
        }
        const std::size_t vertex_bytes = sizeof(MeshVertex) * vertices.size();
        next_vertex_buffer = create_gpu_buffer(
            m_gpu, m_label + " vertex buffer", vertices.data(), vertex_bytes, WGPUBufferUsage_Vertex, error);
        if (next_vertex_buffer == nullptr) {
            return false;
        }
    }
    if (m_vertex_buffer != nullptr) {
        wgpuBufferRelease(m_vertex_buffer);
    }
    m_vertex_buffer = next_vertex_buffer;
    m_vertices = std::move(vertices);
    m_revision += 1;
    error.clear();
    return true;
}

// Replaces indices and submeshes after validating ranges and materials.
bool Mesh::replace_indices(std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes, std::string& error) {
    if (!validate_mesh_data(m_vertices, indices, submeshes, error)) {
        return false;
    }
    WGPUBuffer next_index_buffer = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            error = "Mesh GPU preparation requires a WebGPU device and queue.";
            return false;
        }
        const std::size_t index_bytes = sizeof(std::uint32_t) * indices.size();
        next_index_buffer = create_gpu_buffer(
            m_gpu, m_label + " index buffer", indices.data(), index_bytes, WGPUBufferUsage_Index, error);
        if (next_index_buffer == nullptr) {
            return false;
        }
    }
    if (m_index_buffer != nullptr) {
        wgpuBufferRelease(m_index_buffer);
    }
    m_index_buffer = next_index_buffer;
    m_indices = std::move(indices);
    m_submeshes = std::move(submeshes);
    m_revision += 1;
    error.clear();
    return true;
}

// Returns the mesh label.
const std::string& Mesh::label() const noexcept {
    return m_label;
}

// Returns immutable CPU vertices.
std::span<const MeshVertex> Mesh::vertices() const noexcept {
    return m_vertices;
}

// Returns immutable CPU indices.
std::span<const std::uint32_t> Mesh::indices() const noexcept {
    return m_indices;
}

// Returns immutable submesh ranges.
std::span<const SubMesh> Mesh::submeshes() const noexcept {
    return m_submeshes;
}

// Returns the WebGPU vertex buffer, null for CPU-only resources.
WGPUBuffer Mesh::vertex_buffer() const noexcept {
    return m_vertex_buffer;
}

// Returns the WebGPU index buffer, null for CPU-only resources.
WGPUBuffer Mesh::index_buffer() const noexcept {
    return m_index_buffer;
}

// Returns the mesh revision.
std::uint64_t Mesh::revision() const noexcept {
    return m_revision;
}

// Creates GPU vertex and index buffers from CPU mesh data.
bool Mesh::prepare_gpu_state(std::string& error) {
    if (gpu_context_is_empty(m_gpu)) {
        error.clear();
        return true;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        error = "Mesh GPU preparation requires a WebGPU device and queue.";
        return false;
    }

    const std::size_t vertex_bytes = sizeof(MeshVertex) * m_vertices.size();
    WGPUBuffer next_vertex_buffer = create_gpu_buffer(
        m_gpu, m_label + " vertex buffer", m_vertices.data(), vertex_bytes, WGPUBufferUsage_Vertex, error);
    if (next_vertex_buffer == nullptr) {
        return false;
    }

    const std::size_t index_bytes = sizeof(std::uint32_t) * m_indices.size();
    WGPUBuffer next_index_buffer = create_gpu_buffer(
        m_gpu, m_label + " index buffer", m_indices.data(), index_bytes, WGPUBufferUsage_Index, error);
    if (next_index_buffer == nullptr) {
        wgpuBufferRelease(next_vertex_buffer);
        return false;
    }

    release_gpu_state();
    m_vertex_buffer = next_vertex_buffer;
    m_index_buffer = next_index_buffer;
    error.clear();
    return true;
}

// Releases all owned WebGPU mesh buffers.
void Mesh::release_gpu_state() noexcept {
    if (m_index_buffer != nullptr) {
        wgpuBufferRelease(m_index_buffer);
        m_index_buffer = nullptr;
    }
    if (m_vertex_buffer != nullptr) {
        wgpuBufferRelease(m_vertex_buffer);
        m_vertex_buffer = nullptr;
    }
}

} // namespace ofg
