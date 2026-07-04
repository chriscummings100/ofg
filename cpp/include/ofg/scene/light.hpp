// Entity-owned scene light component.
//
// Light stores direct-light authoring data. Directional light direction is not
// stored here; it is resolved from the owning entity's world-space local +Z
// direction when the renderer builds LightProperties.
#pragma once

#include "ofg/math/vec.hpp"
#include "ofg/scene/component.hpp"

namespace ofg {

class Entity;

enum class LightType {
    Directional,
};

class Light : public Component {
public:
    // Binds this light to one scene-owned entity.
    explicit Light(Entity* entity) noexcept;

    // Returns the direct-light type.
    [[nodiscard]] LightType light_type() const noexcept;
    // Returns the linear RGB light color multiplier.
    [[nodiscard]] math::Vec3 color() const noexcept;
    // Returns the non-negative direct-light intensity.
    [[nodiscard]] float intensity() const noexcept;
    // Returns whether this light contributes to render extraction.
    [[nodiscard]] bool enabled() const noexcept;
    // Replaces color and intensity after validating finite non-negative values.
    void set_color_intensity(math::Vec3 color, float intensity);
    // Sets whether this light contributes to render extraction.
    void set_enabled(bool enabled) noexcept;

private:
    LightType m_light_type{LightType::Directional};
    math::Vec3 m_color{1.0f, 1.0f, 1.0f};
    float m_intensity{1.0f};
    bool m_enabled{true};
};

} // namespace ofg
