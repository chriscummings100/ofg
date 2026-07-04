// Scene-owned global environment state.
//
// Environment is not an entity component. It owns world-level ambient lighting,
// deterministic time/weather inputs, and the current sun light selection. During
// update it can adopt the first directional scene Light and rotate/update that
// entity so the renderer can remain a simple consumer of LightProperties.
#pragma once

#include "ofg/core/ptr.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/scene/light.hpp"

#include <cstdint>

namespace ofg {

class Scene;

struct SkyWeather {
    float m_cloud_coverage{0.25f};
    float m_storm_intensity{0.0f};
    float m_haze{0.08f};
    float m_precipitation_hint{0.0f};
    math::Vec3 m_wind_direction{1.0f, 0.0f, 0.0f};
    float m_wind_speed{0.0f};
    float m_cloud_scale{0.0008f};
    float m_cloud_height{1200.0f};
    float m_cloud_opacity{0.55f};
    float m_cloud_sharpness{0.45f};
};

enum class EnvironmentPreset {
    Daylight,
    Sunset,
    Night,
    Storm,
};

class Environment {
public:
    Environment() = default;

    // Updates time/weather-derived state and adopts a sun light when needed.
    void update(Scene& scene, double time_ms, float delta_seconds);
    // Returns the current weather controls.
    [[nodiscard]] const SkyWeather& weather() const noexcept;
    // Replaces weather controls after validating their ranges.
    void set_weather(SkyWeather weather);
    // Applies deterministic time/weather values for smoke tests and authored scenarios.
    void apply_preset(EnvironmentPreset preset);
    // Returns the most recently applied deterministic preset.
    [[nodiscard]] EnvironmentPreset preset() const noexcept;
    // Replaces the explicitly selected sun light, or clears selection for fallback scanning.
    void set_main_directional_light(Light* light);
    // Returns the current explicit-or-discovered sun light, if live.
    [[nodiscard]] Light* main_directional_light() noexcept;
    // Returns the current explicit-or-discovered sun light, if live.
    [[nodiscard]] const Light* main_directional_light() const noexcept;
    // Replaces the ambient term used by renderer extraction.
    void set_ambient_light(AmbientLight ambient_light);
    // Returns the current ambient term used by renderer extraction.
    [[nodiscard]] AmbientLight ambient_light() const noexcept;
    // Returns the observer-to-sun direction.
    [[nodiscard]] math::Vec3 sun_direction() const noexcept;
    // Returns the observer-to-moon direction.
    [[nodiscard]] math::Vec3 moon_direction() const noexcept;
    // Returns the daylight factor in [0, 1].
    [[nodiscard]] float day_factor() const noexcept;
    // Returns the twilight factor in [0, 1].
    [[nodiscard]] float twilight_factor() const noexcept;
    // Returns the simple normalized moon phase in [0, 1].
    [[nodiscard]] float moon_phase() const noexcept;
    // Returns the latest environment time in seconds.
    [[nodiscard]] float time_seconds() const noexcept;
    // Returns the deterministic seed used by procedural sky star generation.
    [[nodiscard]] std::uint32_t star_seed() const noexcept;

private:
    // Finds and stores the first directional light when no current sun is live.
    void adopt_first_directional_light(Scene& scene);
    // Updates sun, moon, day, and twilight state for a deterministic time.
    void update_celestial_state(float time_seconds);
    // Updates the ambient light from the current celestial and weather state.
    void update_ambient_light() noexcept;
    // Applies the current sun direction and light intensity to the selected light.
    void update_sun_light();

    EnvironmentPreset m_preset{EnvironmentPreset::Daylight};
    float m_day_cycle_seconds{600.0f};
    float m_day_phase_offset{0.35f};
    float m_sun_azimuth_radians{2.5f};
    Ptr<Light> m_main_directional_light;
    SkyWeather m_weather;
    AmbientLight m_ambient_light;
    math::Vec3 m_sun_direction{0.0f, 1.0f, 0.0f};
    math::Vec3 m_moon_direction{0.0f, -1.0f, 0.0f};
    float m_day_factor{1.0f};
    float m_twilight_factor{0.0f};
    float m_moon_phase{0.82f};
    float m_time_seconds{0.0f};
    std::uint32_t m_star_seed{1337};
};

} // namespace ofg
