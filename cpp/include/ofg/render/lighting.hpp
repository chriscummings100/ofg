// Renderer-facing light value types.
//
// Scene light components are authoring/state objects. The renderer consumes
// compact LightProperties values built from the current scene, analogous to how
// it consumes CameraProperties built from a Camera component.
#pragma once

#include "ofg/math/vec.hpp"

#include <cstddef>
#include <span>

namespace ofg {

class Scene;

struct AmbientLight {
    math::Vec3 m_color{1.0f, 1.0f, 1.0f};
    float m_intensity{0.08f};
};

enum class LightPropertiesType {
    Directional,
};

struct LightProperties {
    LightPropertiesType m_type{LightPropertiesType::Directional};
    math::Vec3 m_direction{0.0f, -1.0f, 0.0f};
    math::Vec3 m_color{1.0f, 1.0f, 1.0f};
    float m_intensity{1.0f};
};

// Builds the transient light-property list consumed by renderer passes.
[[nodiscard]] std::size_t build_light_properties(const Scene& scene, std::span<LightProperties> output);

} // namespace ofg
