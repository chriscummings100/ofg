// Stable sphere fitting, light-space texel snapping and independent caster depth coverage.
#include "render/shadow-cascades.h"
#include "math/transform.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ofg {
// Enumerates corners without relying on minimum/maximum vector layout.
static math::Vec3 corner(const Bounds& b, int i)
{
    return {i & 1 ? b.maximum.x : b.minimum.x, i & 2 ? b.maximum.y : b.minimum.y, i & 4 ? b.maximum.z : b.minimum.z};
}
// Extends an AABB with one point.
static void include(Bounds& b, math::Vec3 p)
{
    b.minimum = {std::min(b.minimum.x, p.x), std::min(b.minimum.y, p.y), std::min(b.minimum.z, p.z)};
    b.maximum = {std::max(b.maximum.x, p.x), std::max(b.maximum.y, p.y), std::max(b.maximum.z, p.z)};
}
Bounds worldBounds(const Bounds& local, const math::Mat4& transform)
{
    float inf = std::numeric_limits<float>::infinity();
    Bounds b{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (int i = 0; i < 8; ++i)
    {
        include(b, math::transformPoint(transform, corner(local, i)));
    }
    return b;
}
std::array<ShadowCascade, 4> buildShadowCascades(
    const ShadowCamera& camera,
    math::Vec3 lightDirection,
    const ShadowSettings& settings,
    std::span<const Bounds> casters
)
{
    std::string error;
    auto light = math::normalize(lightDirection, error);
    if (!light ||
        !math::perspectiveLh(camera.verticalFov, camera.aspectRatio, camera.nearDistance, camera.farDistance, error) ||
        !std::isfinite(settings.distance) || settings.distance <= camera.nearDistance || settings.resolution < 8 ||
        !std::isfinite(settings.splitLambda) || settings.splitLambda < 0 || settings.splitLambda > 1 ||
        !std::isfinite(settings.transition) || settings.transition < 0 || settings.transition > 0.3f)
    {
        throw EngineError("Invalid cascade camera, light or shadow settings.");
    }
    auto toward = math::mul(*light, -1);
    auto view =
        *math::lookAtLh({}, toward, std::abs(toward.y) > 0.99f ? math::Vec3{0, 0, 1} : math::Vec3{0, 1, 0}, error);
    float near = camera.nearDistance, far = std::min(camera.farDistance, settings.distance);
    float previous = near, tangent = std::tan(camera.verticalFov / 2);
    std::array<ShadowCascade, 4> result;
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        float fraction = (cascade + 1) / 4.0f;
        float split =
            std::lerp(near + (far - near) * fraction, near * std::pow(far / near, fraction), settings.splitLambda);
        float start = cascade == 0 ? near : previous - (previous - near) * settings.transition;
        math::Vec3 centerLocal{0, 0, (start + split) * 0.5f};
        float radius = std::sqrt(
            split * split * tangent * tangent * (1 + camera.aspectRatio * camera.aspectRatio) +
            (split - centerLocal.z) * (split - centerLocal.z)
        );
        // Rotation-independent radius; round upward and include a PCF/snapping guard band.
        radius = std::ceil(radius * 16) / 16;
        radius *= float(settings.resolution) / float(settings.resolution - 4);
        float texel = 2 * radius / settings.resolution;
        auto center = math::transformPoint(view, math::transformPoint(camera.worldFromView, centerLocal));
        center.x = std::floor(center.x / texel + 0.5f) * texel;
        center.y = std::floor(center.y / texel + 0.5f) * texel;
        float zmin = center.z - radius, zmax = center.z + radius;
        for (const auto& caster : casters)
        {
            auto bounds = worldBounds(caster, view);
            if (bounds.maximum.x < center.x - radius || bounds.minimum.x > center.x + radius ||
                bounds.maximum.y < center.y - radius || bounds.minimum.y > center.y + radius)
            {
                continue;
            }
            zmin = std::min(zmin, bounds.minimum.z);
            zmax = std::max(zmax, bounds.maximum.z);
        }
        // Translate depth to a positive interval required by the shared projection helper.
        auto shiftedView = view;
        shiftedView[3].z -= zmin - 1;
        auto projection = math::orthographicLh(
            center.x - radius,
            center.x + radius,
            center.y - radius,
            center.y + radius,
            0.1f,
            zmax - zmin + 2,
            error
        );
        if (!projection)
        {
            throw EngineError("Cascade projection: " + error);
        }
        result[cascade] = {math::mul(*projection, shiftedView), previous, split, texel};
        previous = split;
    }
    return result;
}
} // namespace ofg
