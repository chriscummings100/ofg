// Scene-owned global environment state implementation.
#include "ofg/scene/environment.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace ofg {
namespace {

constexpr float _pi = 3.14159265358979323846f;

// Reports whether every component is finite.
bool is_finite_vec3(math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Clamps a scalar into a normalized range.
float saturate(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

// GLSL-style smoothstep.
float smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation for scalars.
float mix(float a, float b, float t) noexcept {
    return a * (1.0f - t) + b * t;
}

// Linear interpolation for vectors.
math::Vec3 mix(math::Vec3 a, math::Vec3 b, float t) noexcept {
    return math::vec3(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t));
}

// Validates non-negative finite light values.
void validate_light_values(math::Vec3 color, float intensity, const char* label) {
    if (!is_finite_vec3(color) || !std::isfinite(intensity)) {
        throw EngineError(std::string(label) + " light values must be finite.");
    }
    if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || intensity < 0.0f) {
        throw EngineError(std::string(label) + " light color and intensity must be non-negative.");
    }
}

// Validates one normalized weather scalar.
void validate_normalized(float value, const char* label) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        throw EngineError(std::string(label) + " must be finite and in [0, 1].");
    }
}

// Validates environment weather controls.
void validate_weather(const SkyWeather& weather) {
    validate_normalized(weather.m_cloud_coverage, "Cloud coverage");
    validate_normalized(weather.m_storm_intensity, "Storm intensity");
    validate_normalized(weather.m_haze, "Haze");
    validate_normalized(weather.m_precipitation_hint, "Precipitation hint");
    validate_normalized(weather.m_cloud_opacity, "Cloud opacity");
    validate_normalized(weather.m_cloud_sharpness, "Cloud sharpness");
    if (!is_finite_vec3(weather.m_wind_direction) || !std::isfinite(weather.m_wind_speed) ||
        !std::isfinite(weather.m_cloud_scale) || !std::isfinite(weather.m_cloud_height)) {
        throw EngineError("Weather wind and cloud dimensions must be finite.");
    }
    if (weather.m_wind_speed < 0.0f || weather.m_cloud_scale < 0.0f || weather.m_cloud_height < 0.0f) {
        throw EngineError("Weather wind speed, cloud scale, and cloud height must be non-negative.");
    }
}

// Picks a stable up vector that is not parallel to a direction.
math::Vec3 safe_up_for_direction(math::Vec3 direction) noexcept {
    if (std::fabs(math::dot(direction, math::vec3(0.0f, 1.0f, 0.0f))) > 0.98f) {
        return math::vec3(1.0f, 0.0f, 0.0f);
    }
    return math::vec3(0.0f, 1.0f, 0.0f);
}

// Returns whether the candidate light belongs to the scene's current component storage.
bool scene_contains_light(Scene& scene, const Light* candidate) noexcept {
    if (candidate == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < scene.light_count(); ++index) {
        if (scene.get_light(index) == candidate) {
            return true;
        }
    }
    return false;
}

} // namespace

// Updates time/weather-derived state and adopts a sun light when needed.
void Environment::update(Scene& scene, double time_ms, float delta_seconds) {
    if (!std::isfinite(time_ms) || !std::isfinite(delta_seconds) || delta_seconds < 0.0f) {
        throw EngineError("Environment update requires finite time and non-negative delta.");
    }
    if (m_main_directional_light != nullptr && !scene_contains_light(scene, m_main_directional_light.get())) {
        m_main_directional_light = nullptr;
    }
    if (m_main_directional_light == nullptr) {
        adopt_first_directional_light(scene);
    }

    update_celestial_state(static_cast<float>(time_ms * 0.001));
    update_ambient_light();
    update_sun_light();
}

// Returns the current weather controls.
const SkyWeather& Environment::weather() const noexcept {
    return m_weather;
}

// Replaces weather controls after validating their ranges.
void Environment::set_weather(SkyWeather weather) {
    validate_weather(weather);
    m_weather = weather;
    update_ambient_light();
}

// Applies deterministic time/weather values for smoke tests and authored scenarios.
void Environment::apply_preset(EnvironmentPreset preset) {
    m_preset = preset;
    SkyWeather weather;
    switch (preset) {
    case EnvironmentPreset::Daylight:
        m_day_phase_offset = 0.35f;
        weather.m_cloud_coverage = 0.25f;
        weather.m_haze = 0.08f;
        weather.m_cloud_opacity = 0.55f;
        m_moon_phase = 0.82f;
        m_star_seed = 1337U;
        break;
    case EnvironmentPreset::Sunset:
        m_day_phase_offset = 0.735f;
        weather.m_cloud_coverage = 0.34f;
        weather.m_haze = 0.18f;
        weather.m_cloud_opacity = 0.65f;
        m_moon_phase = 0.55f;
        m_star_seed = 2112U;
        break;
    case EnvironmentPreset::Night:
        m_day_phase_offset = 0.04f;
        weather.m_cloud_coverage = 0.12f;
        weather.m_haze = 0.03f;
        weather.m_cloud_opacity = 0.35f;
        m_moon_phase = 0.82f;
        m_star_seed = 4242U;
        break;
    case EnvironmentPreset::Storm:
        m_day_phase_offset = 0.37f;
        weather.m_cloud_coverage = 0.86f;
        weather.m_storm_intensity = 0.82f;
        weather.m_haze = 0.30f;
        weather.m_precipitation_hint = 0.55f;
        weather.m_wind_direction = math::vec3(0.6f, 0.0f, 0.8f);
        weather.m_wind_speed = 18.0f;
        weather.m_cloud_opacity = 0.88f;
        weather.m_cloud_sharpness = 0.62f;
        m_moon_phase = 0.20f;
        m_star_seed = 9001U;
        break;
    }
    set_weather(weather);
    update_celestial_state(0.0f);
    update_ambient_light();
}

// Returns the most recently applied deterministic preset.
EnvironmentPreset Environment::preset() const noexcept {
    return m_preset;
}

// Replaces the explicitly selected sun light, or clears selection for fallback scanning.
void Environment::set_main_directional_light(Light* light) {
    if (light != nullptr && light->light_type() != LightType::Directional) {
        throw EngineError("Environment main light must be directional.");
    }
    m_main_directional_light = light;
}

// Returns the current explicit-or-discovered sun light, if live.
Light* Environment::main_directional_light() noexcept {
    return m_main_directional_light.get();
}

// Returns the current explicit-or-discovered sun light, if live.
const Light* Environment::main_directional_light() const noexcept {
    return m_main_directional_light.get();
}

// Replaces the ambient term used by renderer extraction.
void Environment::set_ambient_light(AmbientLight ambient_light) {
    validate_light_values(ambient_light.m_color, ambient_light.m_intensity, "Ambient");
    m_ambient_light = ambient_light;
}

// Returns the current ambient term used by renderer extraction.
AmbientLight Environment::ambient_light() const noexcept {
    return m_ambient_light;
}

// Returns the observer-to-sun direction.
math::Vec3 Environment::sun_direction() const noexcept {
    return m_sun_direction;
}

// Returns the observer-to-moon direction.
math::Vec3 Environment::moon_direction() const noexcept {
    return m_moon_direction;
}

// Returns the daylight factor in [0, 1].
float Environment::day_factor() const noexcept {
    return m_day_factor;
}

// Returns the twilight factor in [0, 1].
float Environment::twilight_factor() const noexcept {
    return m_twilight_factor;
}

// Returns the simple normalized moon phase in [0, 1].
float Environment::moon_phase() const noexcept {
    return m_moon_phase;
}

// Returns the latest environment time in seconds.
float Environment::time_seconds() const noexcept {
    return m_time_seconds;
}

// Returns the deterministic seed used by procedural sky star generation.
std::uint32_t Environment::star_seed() const noexcept {
    return m_star_seed;
}

// Finds and stores the first directional light when no current sun is live.
void Environment::adopt_first_directional_light(Scene& scene) {
    for (std::size_t index = 0; index < scene.light_count(); ++index) {
        Light* light = scene.get_light(index);
        if (light != nullptr && light->light_type() == LightType::Directional) {
            m_main_directional_light = light;
            return;
        }
    }
}

// Updates sun, moon, day, and twilight state for a deterministic time.
void Environment::update_celestial_state(float time_seconds) {
    m_time_seconds = time_seconds;
    const float day_phase =
        (m_day_cycle_seconds <= 0.0f) ? 0.0f : m_time_seconds / m_day_cycle_seconds + m_day_phase_offset;
    const float wrapped_phase = day_phase - std::floor(day_phase);
    const float theta = wrapped_phase * 2.0f * _pi - _pi * 0.5f;
    const float horizontal = std::cos(theta);

    std::string error;
    const math::Vec3 raw_sun_direction = math::vec3(
        horizontal * std::sin(m_sun_azimuth_radians), std::sin(theta), horizontal * std::cos(m_sun_azimuth_radians));
    const std::optional<math::Vec3> normalized_sun = math::normalize(raw_sun_direction, error);
    if (!normalized_sun.has_value()) {
        throw EngineError(error.empty() ? "Environment sun direction could not be normalized." : error);
    }
    m_sun_direction = *normalized_sun;

    const std::optional<math::Vec3> normalized_moon =
        math::normalize(math::add(math::mul(m_sun_direction, -1.0f), math::vec3(0.08f, 0.03f, -0.04f)), error);
    if (!normalized_moon.has_value()) {
        throw EngineError(error.empty() ? "Environment moon direction could not be normalized." : error);
    }
    m_moon_direction = *normalized_moon;

    m_day_factor = smoothstep(-0.06f, 0.08f, m_sun_direction.y);
    m_twilight_factor =
        smoothstep(-0.22f, 0.02f, m_sun_direction.y) * (1.0f - smoothstep(0.02f, 0.20f, m_sun_direction.y));
}

// Updates the ambient light from the current celestial and weather state.
void Environment::update_ambient_light() noexcept {
    const float ambient_intensity = mix(0.025f, 0.22f, m_day_factor) * mix(1.0f, 0.55f, m_weather.m_cloud_coverage);
    const math::Vec3 ambient_color =
        mix(math::vec3(0.08f, 0.10f, 0.18f), math::vec3(0.46f, 0.52f, 0.62f), m_day_factor);
    m_ambient_light = AmbientLight{ambient_color, ambient_intensity};
}

// Applies the current sun direction and light intensity to the selected light.
void Environment::update_sun_light() {
    Light* light = m_main_directional_light.get();
    if (light == nullptr) {
        return;
    }
    Entity* entity = light->entity();
    if (entity == nullptr) {
        return;
    }

    const math::Vec3 sunlight_forward = math::mul(m_sun_direction, -1.0f);
    const math::Vec3 eye = entity->local_transform().m_position;
    std::string error;
    const std::optional<math::Quat> rotation =
        math::quat_look_at_lh(eye, math::add(eye, sunlight_forward), safe_up_for_direction(sunlight_forward), error);
    if (!rotation.has_value()) {
        throw EngineError(error.empty() ? "Environment sun-light rotation creation failed." : error);
    }
    entity->local_transform().m_rotation = *rotation;

    const float storm_dimming = mix(1.0f, 0.45f, m_weather.m_storm_intensity);
    const float direct_intensity = 3.2f * m_day_factor * storm_dimming;
    const math::Vec3 direct_color = mix(math::vec3(1.0f, 0.72f, 0.48f), math::vec3(1.0f, 0.96f, 0.88f), m_day_factor);
    light->set_color_intensity(direct_color, direct_intensity);
}

} // namespace ofg
