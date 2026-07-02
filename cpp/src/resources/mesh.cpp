// Mutable indexed mesh resource for OFG renderer geometry.
#include "ofg/resources/mesh.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/gpu/common.hpp"

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
void validate_indices(const std::vector<std::uint32_t>& indices, std::size_t vertex_count) {
    if (indices.empty()) {
        throw EngineError("Mesh indices must not be empty.");
    }
    for (std::uint32_t index : indices) {
        if (index >= vertex_count) {
            throw EngineError("Mesh index references a missing vertex.");
        }
    }
}

// Validates submesh ranges and default materials.
void validate_submeshes(const std::vector<SubMesh>& submeshes, std::size_t index_count) {
    if (submeshes.empty()) {
        throw EngineError("Mesh requires at least one submesh.");
    }
    for (const SubMesh& submesh : submeshes) {
        if (submesh.m_label.empty()) {
            throw EngineError("Submesh label must not be empty.");
        }
        if (!submesh.m_default_material) {
            throw EngineError("Submesh default material must not be null.");
        }
        const std::uint64_t range_end =
            static_cast<std::uint64_t>(submesh.m_index_start) + static_cast<std::uint64_t>(submesh.m_index_count);
        if (submesh.m_index_count == 0 || range_end > index_count) {
            throw EngineError("Submesh index range is outside the mesh index buffer.");
        }
    }
}

// Validates a complete mesh CPU data set.
void validate_mesh_data(const std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    const std::vector<SubMesh>& submeshes) {
    if (vertices.empty()) {
        throw EngineError("Mesh vertices must not be empty.");
    }
    validate_indices(indices, vertices.size());
    validate_submeshes(submeshes, indices.size());
}

// Creates a GPU buffer and uploads immutable CPU data into it.
WGPUBuffer create_gpu_buffer(
    const GpuContext& gpu, const std::string& label, const void* data, std::size_t byte_count, WGPUBufferUsage usage) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::string_view(label);
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    descriptor.size = byte_count;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu.m_device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError("wgpuDeviceCreateBuffer returned null for mesh buffer '" + label + "'.");
    }

    wgpuQueueWriteBuffer(gpu.m_queue, buffer, 0, data, byte_count);
    return buffer;
}

} // namespace

// Allocates a labeled mesh resource using the creating Resources context.
Mesh::Mesh(GpuContext gpu, std::string label) : m_gpu(std::move(gpu)), m_label(std::move(label)) {
    if (m_label.empty()) {
        throw EngineError("Mesh label must not be empty.");
    }
}

// Releases owned GPU mesh buffers.
Mesh::~Mesh() {
    release_gpu_state();
}

// Initializes this mesh and validates vertices, indices, and submesh ranges.
void Mesh::init(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes) {
    validate_mesh_data(vertices, indices, submeshes);
    release_gpu_state();
    m_dynamic_vertices = false;
    m_dynamic_vertex_capacity = 0;
    m_vertices = std::move(vertices);
    m_indices = std::move(indices);
    m_submeshes = std::move(submeshes);
    prepare_gpu_state();
    m_revision += 1;
}

// Initializes a fixed-size dynamic vertex mesh for per-frame vertex updates.
void Mesh::init_dynamic_vertices(
    std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes) {
    validate_mesh_data(vertices, indices, submeshes);
    release_gpu_state();
    m_dynamic_vertices = true;
    m_dynamic_vertex_capacity = vertices.size();
    m_vertices = std::move(vertices);
    m_indices = std::move(indices);
    m_submeshes = std::move(submeshes);
    prepare_gpu_state();
    m_revision += 1;
}

// Replaces vertices when the existing indices remain valid.
void Mesh::replace_vertices(std::vector<MeshVertex> vertices) {
    if (vertices.empty()) {
        throw EngineError("Mesh vertices must not be empty.");
    }
    validate_indices(m_indices, vertices.size());
    WGPUBuffer next_vertex_buffer = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            throw EngineError("Mesh GPU preparation requires a WebGPU device and queue.");
        }
        const std::size_t vertex_bytes = sizeof(MeshVertex) * vertices.size();
        next_vertex_buffer =
            create_gpu_buffer(m_gpu, m_label + " vertex buffer", vertices.data(), vertex_bytes, WGPUBufferUsage_Vertex);
        m_vertex_buffer_create_count += 1;
        m_vertex_upload_bytes += vertex_bytes;
    }
    if (m_vertex_buffer != nullptr) {
        wgpuBufferRelease(m_vertex_buffer);
    }
    m_vertex_buffer = next_vertex_buffer;
    m_vertices = std::move(vertices);
    m_revision += 1;
}

// Updates an already-initialized dynamic vertex mesh without recreating GPU buffers.
void Mesh::update_vertices_in_place(std::span<const MeshVertex> vertices) {
    if (!m_dynamic_vertices) {
        throw EngineError("Mesh in-place vertex updates require a dynamic vertex mesh.");
    }
    if (vertices.size() != m_vertices.size() || vertices.size() > m_dynamic_vertex_capacity) {
        throw EngineError("Mesh in-place vertex update must match the dynamic vertex capacity.");
    }
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu) || m_vertex_buffer == nullptr) {
            throw EngineError("Mesh in-place vertex update requires a prepared WebGPU vertex buffer.");
        }
    }

    std::copy(vertices.begin(), vertices.end(), m_vertices.begin());
    if (!gpu_context_is_empty(m_gpu)) {
        const std::size_t vertex_bytes = sizeof(MeshVertex) * vertices.size();
        wgpuQueueWriteBuffer(m_gpu.m_queue, m_vertex_buffer, 0, vertices.data(), vertex_bytes);
        m_vertex_upload_bytes += vertex_bytes;
    }
    m_revision += 1;
}

// Replaces indices and submeshes after validating ranges and materials.
void Mesh::replace_indices(std::vector<std::uint32_t> indices, std::vector<SubMesh> submeshes) {
    validate_mesh_data(m_vertices, indices, submeshes);
    WGPUBuffer next_index_buffer = nullptr;
    if (!gpu_context_is_empty(m_gpu)) {
        if (!gpu_context_is_ready(m_gpu)) {
            throw EngineError("Mesh GPU preparation requires a WebGPU device and queue.");
        }
        const std::size_t index_bytes = sizeof(std::uint32_t) * indices.size();
        next_index_buffer =
            create_gpu_buffer(m_gpu, m_label + " index buffer", indices.data(), index_bytes, WGPUBufferUsage_Index);
    }
    if (m_index_buffer != nullptr) {
        wgpuBufferRelease(m_index_buffer);
    }
    m_index_buffer = next_index_buffer;
    m_indices = std::move(indices);
    m_submeshes = std::move(submeshes);
    m_revision += 1;
}

// Returns the mesh label.
const std::string& Mesh::label() const noexcept {
    return m_label;
}

// Returns the borrowed GPU context used by this mesh, or an empty context for CPU-only meshes.
GpuContext Mesh::gpu_context() const noexcept {
    return m_gpu;
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

// Returns whether this mesh supports fixed-capacity in-place vertex updates.
bool Mesh::is_dynamic_vertex_mesh() const noexcept {
    return m_dynamic_vertices;
}

// Returns the mesh revision.
std::uint64_t Mesh::revision() const noexcept {
    return m_revision;
}

// Reports how many GPU vertex buffers this mesh has created.
std::uint64_t Mesh::vertex_buffer_create_count() const noexcept {
    return m_vertex_buffer_create_count;
}

// Reports bytes uploaded to this mesh's GPU vertex buffer.
std::uint64_t Mesh::vertex_upload_bytes() const noexcept {
    return m_vertex_upload_bytes;
}

// Creates GPU vertex and index buffers from CPU mesh data.
void Mesh::prepare_gpu_state() {
    if (gpu_context_is_empty(m_gpu)) {
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Mesh GPU preparation requires a WebGPU device and queue.");
    }

    const std::size_t vertex_bytes = sizeof(MeshVertex) * m_vertices.size();
    WGPUBuffer next_vertex_buffer =
        create_gpu_buffer(m_gpu, m_label + " vertex buffer", m_vertices.data(), vertex_bytes, WGPUBufferUsage_Vertex);
    m_vertex_buffer_create_count += 1;
    m_vertex_upload_bytes += vertex_bytes;

    const std::size_t index_bytes = sizeof(std::uint32_t) * m_indices.size();
    WGPUBuffer next_index_buffer = nullptr;
    try {
        next_index_buffer =
            create_gpu_buffer(m_gpu, m_label + " index buffer", m_indices.data(), index_bytes, WGPUBufferUsage_Index);
    } catch (...) {
        wgpuBufferRelease(next_vertex_buffer);
        throw;
    }

    release_gpu_state();
    m_vertex_buffer = next_vertex_buffer;
    m_index_buffer = next_index_buffer;
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
