// Entity-owned scene light component implementation.
#include "ofg/scene/light.hpp"

#include "ofg/core/engine_error.hpp"

#include <cmath>

namespace ofg {
namespace {

// Reports whether every component is finite.
bool is_finite_vec3(math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Validates a non-negative finite linear light color and intensity.
void validate_light_values(math::Vec3 color, float intensity) {
    if (!is_finite_vec3(color) || !std::isfinite(intensity)) {
        throw EngineError("Light color and intensity must be finite.");
    }
    if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || intensity < 0.0f) {
        throw EngineError("Light color and intensity must be non-negative.");
    }
}

} // namespace

// Binds this light to one scene-owned entity.
Light::Light(Entity* entity) noexcept : Component(ComponentType::Light, entity) {}

// Returns the direct-light type.
LightType Light::light_type() const noexcept {
    return m_light_type;
}

// Returns the linear RGB light color multiplier.
math::Vec3 Light::color() const noexcept {
    return m_color;
}

// Returns the non-negative direct-light intensity.
float Light::intensity() const noexcept {
    return m_intensity;
}

// Returns whether this light contributes to render extraction.
bool Light::enabled() const noexcept {
    return m_enabled;
}

// Replaces color and intensity after validating finite non-negative values.
void Light::set_color_intensity(math::Vec3 color, float intensity) {
    validate_light_values(color, intensity);
    m_color = color;
    m_intensity = intensity;
}

// Sets whether this light contributes to render extraction.
void Light::set_enabled(bool enabled) noexcept {
    m_enabled = enabled;
}

} // namespace ofg
