// Scene-space punctual lights and explicit HDR/IBL settings, independent of a graphics device.
#pragma once
#include "math/vec.h"
#include "resources/environment.h"
#include <array>
#include <optional>
#include "scene/outdoor-lighting.h"
namespace ofg {
enum class LightType : uint32_t
{
    Directional,
    Point,
    Spot
};
struct PunctualLight
{
    math::Vec3 position{};
    float range = 0;               // Metres; zero means infinite range.
    math::Vec3 direction{0, 0, 1}; // Direction the light travels.
    LightType type = LightType::Directional;
    math::Vec3 color{1, 1, 1};
    float intensity = 1;                           // Lux for directional, candela for point/spot.
    float innerCone = 0, outerCone = 0.785398163f; // Radians.
};
struct Lighting
{
    std::optional<OutdoorLighting> outdoor;
    std::array<PunctualLight, 4> lights{};
    uint32_t lightCount = 0;
    std::shared_ptr<Environment> environment;
    float environmentIntensity = 1, environmentRotation = 0, exposure = 1;
    bool hdr = false, linearOutput = false;
    uint32_t debugView = 0; // 0 shaded, 1 normals, 2 roughness, 3 metallic, 4 base color.
};
} // namespace ofg
