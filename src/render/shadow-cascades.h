// Pure cascade fitting in OFG's left-handed world and [0,1] clip-depth convention.
#pragma once
#include "resources/mesh.h"
#include "scene/outdoor-lighting.h"
#include <array>
#include <span>
namespace ofg {
struct ShadowCamera
{
    math::Mat4 worldFromView{math::mat4Identity()};
    float nearDistance = 0.1f, farDistance = 1000, verticalFov = 1.04719755f, aspectRatio = 1;
};
struct ShadowCascade
{
    math::Mat4 clipFromWorld{math::mat4Identity()};
    float nearDistance = 0, farDistance = 0, texelSize = 0;
};
// Fits four stable, overlapping cascades including world-space caster bounds; direction points toward the light.
// Camera worldFromView is a rigid pose; distances are metres, vertical FOV is radians.
std::array<ShadowCascade, 4> buildShadowCascades(
    const ShadowCamera& camera,
    math::Vec3 lightDirection,
    const ShadowSettings& settings,
    std::span<const Bounds> casterBounds
);
// Conservatively transforms a mesh AABB into world space, including mirrored and nonuniform scale.
Bounds worldBounds(const Bounds& local, const math::Mat4& worldFromLocal);
} // namespace ofg
