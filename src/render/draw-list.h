// GPU-independent visible/caster extraction; entries retain assets and snapshot transforms, never scene pointers.
#pragma once

#include "resources/mesh.h"
#include "scene/lighting.h"
#include "render/shadow-cascades.h"
#include "resources/skin.h"
#include <optional>

namespace ofg {
class Scene;
class Camera;
class MeshRenderer;
struct DeformationStorage;
struct DeformationJob
{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Skin> skin;
    std::shared_ptr<DeformationStorage> storage;
    std::vector<math::Mat4> palette; // Mesh-local snapshot, no live scene references.
};
struct DrawItem
{
    std::shared_ptr<Mesh> mesh;
    uint32_t subMeshIndex;
    std::shared_ptr<Material> material;
    math::Mat4 worldFromLocal;
    std::optional<size_t> deformationJob;
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
    std::vector<DeformationJob> deformations;
};

// Conservatively tests a local AABB against the six [0,1]-depth homogeneous clip planes.
bool boundsVisible(const Bounds& bounds, const math::Mat4& clipFromLocal) noexcept;
// Snapshots inverse(meshWorld) * jointWorld * inverseBind for a bound renderer; fails on expired/singular bindings.
DeformationJob snapshotDeformation(const MeshRenderer& renderer);
// Extracts visible submeshes and independent outdoor casters in creation order.
// Foreign/singular cameras and failed assets throw; entries retain ready assets and copied transforms.
DrawList buildDrawList(const Scene& scene, const Camera& camera, float aspectRatio);
} // namespace ofg
