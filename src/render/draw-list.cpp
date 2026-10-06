// Homogeneous bound culling and shared asset extraction from passive scene components.
#include "render/draw-list.h"
#include "scene/scene.h"
#include "math/transform.h"
#include "core/engine-error.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace ofg {
DeformationJob snapshotDeformation(const MeshRenderer& renderer)
{
    const auto context = "Skin palette for entity '" + renderer.entity()->name() + "': ";
    if (!renderer.mesh() || !renderer.skin() || !renderer.deformationStorage())
    {
        throw EngineError(context + "requires a mesh and skin binding.");
    }
    std::string error;
    auto inverse = math::inverseAffine(renderer.entity()->worldTransform(), error);
    if (!inverse)
    {
        throw EngineError(context + error);
    }
    DeformationJob job{renderer.mesh(), renderer.skin(), renderer.deformationStorage(), {}};
    for (size_t index = 0; index < renderer.joints().size(); ++index)
    {
        const auto& joint = renderer.joints()[index];
        if (!joint)
        {
            throw EngineError(context + "joint " + std::to_string(index) + " has expired.");
        }
        const auto matrix = math::mul(
            math::mul(*inverse, joint->worldTransform()),
            renderer.skin()->desc().joints[index].inverseBindMatrix
        );
        for (float value : math::packMat4(matrix))
        {
            if (!std::isfinite(value))
            {
                throw EngineError(context + "joint " + std::to_string(index) + " produced a non-finite matrix.");
            }
        }
        job.palette.push_back(matrix);
    }
    return job;
}

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
    list.lighting = scene.lighting;
    const auto cameraWorld = entity->worldTransform()[3];
    list.cameraPosition = {cameraWorld.x, cameraWorld.y, cameraWorld.z};
    list.atmosphereHeight = cameraWorld.y;
    list.camera =
        {entity->worldTransform(), camera.nearDistance(), camera.farDistance(), camera.verticalFov(), aspectRatio};
    if (list.lighting.outdoor)
    {
        list.outdoor = evaluateOutdoorLighting(*list.lighting.outdoor, list.cameraPosition.y);
        auto& light = list.lighting.lights[0];
        light.type = LightType::Directional;
        light.direction = math::mul(list.outdoor->lightDirection, -1);
        light.color = list.outdoor->lightColor;
        light.intensity = list.outdoor->illuminance;
        list.lighting.lightCount = std::max(1u, list.lighting.lightCount);
        list.lighting.environmentRotation = 0;
    }
    list.clipFromWorld = math::mul(camera.projectionMatrix(aspectRatio), *view);
    for (const auto& renderer : scene.meshRenderers())
    {
        const auto& mesh = renderer->mesh();
        if (!mesh || !ready(*mesh))
        {
            continue;
        }
        const auto world = renderer->entity()->worldTransform();
        const bool visible = renderer->skin() || boundsVisible(mesh->bounds(), math::mul(list.clipFromWorld, world));
        if (!visible && !list.outdoor)
        {
            continue;
        }
        std::optional<size_t> deformation;
        if (renderer->skin())
        {
            deformation = list.deformations.size();
            list.deformations.push_back(snapshotDeformation(*renderer));
        }
        for (uint32_t index = 0; index < mesh->subMeshes().size(); ++index)
        {
            auto material = renderer->material(index);
            if (ready(*material) && ready(*material->shader()))
            {
                bool texturesReady = true;
                for (const auto& [name, binding] : material->textures())
                {
                    texturesReady = ready(*bindingTexture(binding)) && texturesReady;
                }
                if (texturesReady)
                {
                    if (list.outdoor && renderer->castsShadows() &&
                        material->renderState().alphaMode != AlphaMode::Blend)
                    {
                        list.shadowCasters.push_back({mesh, index, material, world, deformation});
                    }
                    if (visible)
                    {
                        list.items.push_back({mesh, index, std::move(material), world, deformation});
                    }
                }
            }
        }
    }
    return list;
}
} // namespace ofg
