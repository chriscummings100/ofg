// Homogeneous bound culling and shared asset extraction from passive scene components.
#include "render/draw-list.h"
#include "scene/scene.h"
#include "math/transform.h"
#include "core/engine-error.h"
#include <array>
#include <algorithm>

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
    list.lighting = scene.lighting;
    const auto cameraWorld = entity->worldTransform()[3];
    list.cameraPosition = {cameraWorld.x, cameraWorld.y, cameraWorld.z};
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
        bool visible = boundsVisible(mesh->bounds(), math::mul(list.clipFromWorld, world));
        if (!visible && !list.outdoor)
        {
            continue;
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
                        list.shadowCasters.push_back({mesh, index, material, world});
                    }
                    if (visible)
                    {
                        list.items.push_back({mesh, index, std::move(material), world});
                    }
                }
            }
        }
    }
    return list;
}
} // namespace ofg
