// Latitude/longitude sphere with analytic normals and tangents; degenerate pole triangles are omitted.
#include "lab/sphere.h"
#include "core/engine-error.h"
#include <cmath>

namespace ofg {
std::shared_ptr<Mesh> createSphereMesh(std::shared_ptr<Material> material, uint32_t slices, uint32_t stacks)
{
    if (slices < 3 || stacks < 2 || slices > 256 || stacks > 256)
    {
        throw EngineError("Sphere tessellation requires 3..256 slices and 2..256 stacks.");
    }
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (uint32_t y = 0; y <= stacks; ++y)
    {
        const float v = float(y) / stacks;
        const float latitude = v * 3.141592654f;
        for (uint32_t x = 0; x <= slices; ++x)
        {
            const float u = float(x) / slices;
            const float longitude = u * 6.283185307f;
            const float radius = y == 0 || y == stacks ? 0.f : std::sin(latitude);
            const math::Vec3 normal{radius * std::sin(longitude), std::cos(latitude), radius * std::cos(longitude)};
            Vertex vertex{normal, normal, {u, v}};
            vertex.tangent = {std::cos(longitude), 0, -std::sin(longitude), -1};
            vertex.uv1 = {u, v};
            vertices.push_back(vertex);
        }
    }
    for (uint32_t y = 0; y < stacks; ++y)
    {
        for (uint32_t x = 0; x < slices; ++x)
        {
            const uint32_t a = y * (slices + 1) + x;
            const uint32_t b = a + slices + 1;
            if (y != 0)
            {
                indices.insert(indices.end(), {a, b, a + 1});
            }
            if (y + 1 != stacks)
            {
                indices.insert(indices.end(), {a + 1, b, b + 1});
            }
        }
    }
    const uint32_t count = uint32_t(indices.size());
    return Mesh::create(std::move(vertices), std::move(indices), {{0, count, std::move(material)}});
}
} // namespace ofg
