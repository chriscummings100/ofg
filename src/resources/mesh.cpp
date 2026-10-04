// Geometry validation and bounds calculation, independent of any graphics device.
#include "resources/mesh.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ofg {
Mesh::Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices, std::vector<SubMesh> subMeshes, Bounds bounds)
    : m_vertices(std::move(vertices))
    , m_indices(std::move(indices))
    , m_subMeshes(std::move(subMeshes))
    , m_bounds(bounds)
{
}

std::shared_ptr<Mesh> Mesh::create(
    std::vector<Vertex> vertices,
    std::vector<uint32_t> indices,
    std::vector<SubMesh> subMeshes
)
{
    if (vertices.empty() || indices.empty() || subMeshes.empty() || indices.size() % 3 != 0 ||
        vertices.size() > UINT32_MAX || indices.size() > UINT32_MAX || subMeshes.size() > UINT32_MAX)
    {
        throw EngineError("Mesh requires nonempty uint32-addressable triangle geometry and submeshes.");
    }
    Bounds bounds{vertices[0].position, vertices[0].position};
    for (const Vertex& vertex : vertices)
    {
        for (float value :
             {vertex.position.x,
              vertex.position.y,
              vertex.position.z,
              vertex.normal.x,
              vertex.normal.y,
              vertex.normal.z,
              vertex.uv.x,
              vertex.uv.y,
              vertex.tangent.x,
              vertex.tangent.y,
              vertex.tangent.z,
              vertex.tangent.w,
              vertex.uv1.x,
              vertex.uv1.y,
              vertex.color.x,
              vertex.color.y,
              vertex.color.z,
              vertex.color.w})
        {
            if (!std::isfinite(value))
            {
                throw EngineError("Mesh vertex attributes must be finite.");
            }
        }
        bounds.minimum = {
            std::min(bounds.minimum.x, vertex.position.x),
            std::min(bounds.minimum.y, vertex.position.y),
            std::min(bounds.minimum.z, vertex.position.z)
        };
        bounds.maximum = {
            std::max(bounds.maximum.x, vertex.position.x),
            std::max(bounds.maximum.y, vertex.position.y),
            std::max(bounds.maximum.z, vertex.position.z)
        };
    }
    for (uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            throw EngineError("Mesh index is outside the vertex buffer.");
        }
    }
    for (const SubMesh& part : subMeshes)
    {
        if (!part.material || part.indexCount == 0 || part.firstIndex % 3 != 0 || part.indexCount % 3 != 0 ||
            part.firstIndex > indices.size() || part.indexCount > indices.size() - part.firstIndex)
        {
            throw EngineError("Submesh requires a material and a valid nonempty triangle index range.");
        }
    }
    return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), std::move(indices), std::move(subMeshes), bounds));
}
} // namespace ofg
