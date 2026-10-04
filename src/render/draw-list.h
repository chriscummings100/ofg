// GPU-independent visible/caster extraction; entries retain assets and snapshot transforms, never scene pointers.
#pragma once

#include "resources/mesh.h"
#include "scene/lighting.h"
#include "render/shadow-cascades.h"

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
    math::Vec3 cameraPosition{};
    Lighting lighting;
    ShadowCamera camera;
    std::optional<OutdoorFrame> outdoor;
    std::vector<DrawItem> shadowCasters;
};

// Conservatively tests a local AABB against the six [0,1]-depth homogeneous clip planes.
bool boundsVisible(const Bounds& bounds, const math::Mat4& clipFromLocal) noexcept;
// Extracts visible submeshes and independent outdoor casters in creation order.
// Foreign/singular cameras and failed assets throw; entries retain ready assets and copied transforms.
DrawList buildDrawList(const Scene& scene, const Camera& camera, float aspectRatio);
} // namespace ofg
