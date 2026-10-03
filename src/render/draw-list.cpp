// Homogeneous bound culling and shared asset extraction from passive scene components.
#include "render/draw-list.h"
#include "scene/scene.h"
#include "math/transform.h"
#include "core/engine-error.h"
#include <array>

namespace ofg {
bool boundsVisible(const Bounds& bounds, const math::Mat4& clipFromLocal) noexcept
{
    std::array<bool, 6> outside{true, true, true, true, true, true};
    for (int corner = 0; corner < 8; ++corner)
    {
        const auto p = math::mul(
            clipFromLocal,
            math::Vec4{
                corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                corner & 4 ? bounds.maximum.z : bounds.minimum.z,
                1.0f
            }
        );
        // No divide by w: near-plane intersections and points behind the eye remain well-defined.
        const std::array<float, 6> distances{p.x + p.w, p.w - p.x, p.y + p.w, p.w - p.y, p.z, p.w - p.z};
        for (size_t plane = 0; plane < outside.size(); ++plane)
        {
            outside[plane] = outside[plane] && distances[plane] < 0.0f;
        }
    }
    for (bool rejected : outside)
    {
        if (rejected)
        {
            return false;
        }
    }
    return true;
}

// Distinguishes pending dependencies from terminal errors at draw extraction boundaries.
static bool ready(const Resource& resource)
{
    if (resource.isFailed())
    {
        throw EngineError("Draw resource failed: " + resource.key() + ": " + resource.error());
    }
    return resource.isLoaded();
}

DrawList buildDrawList(const Scene& scene, const Camera& camera, float aspectRatio)
{
    const Entity* entity = camera.entity();
    if (!entity || scene.getEntity(entity->id()) != entity || entity->camera() != &camera)
    {
        throw EngineError("Draw camera must belong to the scene.");
    }
    std::string error;
    auto view = math::inverseAffine(entity->worldTransform(), error);
    if (!view)
    {
        throw EngineError("Camera view: " + error);
    }
    DrawList list;
    list.clipFromWorld = math::mul(camera.projectionMatrix(aspectRatio), *view);
    for (const auto& renderer : scene.meshRenderers())
    {
        const auto& mesh = renderer->mesh();
        if (!mesh || !ready(*mesh))
        {
            continue;
        }
        const auto world = renderer->entity()->worldTransform();
        if (!boundsVisible(mesh->bounds(), math::mul(list.clipFromWorld, world)))
        {
            continue;
        }
        for (uint32_t index = 0; index < mesh->subMeshes().size(); ++index)
        {
            auto material = renderer->material(index);
            if (ready(*material) && ready(*material->shader()))
            {
                list.items.push_back({mesh, index, std::move(material), world});
            }
        }
    }
    return list;
}
} // namespace ofg
