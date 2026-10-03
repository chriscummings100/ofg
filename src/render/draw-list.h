// GPU-independent visible draw extraction; entries retain assets and snapshot transforms, never scene pointers.
#pragma once

#include "resources/mesh.h"

namespace ofg {
class Scene;
class Camera;
struct DrawItem
{
    std::shared_ptr<Mesh> mesh;
    uint32_t subMeshIndex;
    std::shared_ptr<Material> material;
    math::Mat4 worldFromLocal;
};

struct DrawList
{
    math::Mat4 clipFromWorld{math::mat4Identity()};
    std::vector<DrawItem> items;
};

// Conservatively tests a local AABB against the six [0,1]-depth homogeneous clip planes.
bool boundsVisible(const Bounds& bounds, const math::Mat4& clipFromLocal) noexcept;
// Extracts visible submeshes in creation order; foreign/singular cameras and failed assets throw.
DrawList buildDrawList(const Scene& scene, const Camera& camera, float aspectRatio);
} // namespace ofg
