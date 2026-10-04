// Deterministic game-clock orbit and Earth-like optical depth used by directional lighting.
#include "scene/outdoor-lighting.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace ofg {
// Tests a bounded finite scalar at the settings boundary.
static bool within(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}
// Smooth interpolation with zero endpoint derivatives.
static float smooth(float low, float high, float value)
{
    float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
bool validOutdoorLighting(const OutdoorLighting& s) noexcept
{
    const auto& d = s.dayCycle;
    const auto& a = s.atmosphere;
    const auto& c = s.clouds;
    const auto& h = s.shadows;
    const auto& e = s.exposure;
    return within(d.timeHours, 0, 24) && within(d.elapsedSeconds, 0, 1e12) && within(d.cycleSeconds, 1, 86400) &&
           within(d.noonElevation, 0.01, 1.5707964) && within(d.azimuth, -1000, 1000) && within(a.rayleigh, 0, 10) &&
           within(a.mie, 0, 20) && within(a.ozone, 0, 10) && within(a.groundAlbedo, 0, 1) &&
           within(a.sunIlluminance, 0, 200000) && within(a.moonIlluminance, 0, 10) &&
           within(a.aerialDistance, 100, 100000) && within(c.coverage, 0, 1) && within(c.opticalThickness, 0, 20) &&
           within(c.altitude, 100, 20000) && within(c.secondAltitude, c.altitude + 1, 30000) &&
           within(c.scale, 0.00001, 0.1) && within(c.windSpeed, 0, 200) && within(c.windAngle, -1000, 1000) &&
           (h.resolution == 1024 || h.resolution == 2048) && within(h.distance, 1, 10000) &&
           within(h.splitLambda, 0, 1) && within(h.transition, 0.01, 0.3) && within(h.normalBias, 0, 5) &&
           within(h.depthBias, 0, 0.01) && h.debugView <= 2 && within(e.dayEv, -10, 25) &&
           within(e.twilightEv, -10, 25) && within(e.nightEv, -10, 25) && within(e.manualEv, -10, 25);
}
void advanceOutdoorLighting(OutdoorLighting& settings, float deltaSeconds)
{
    if (!validOutdoorLighting(settings) || !std::isfinite(deltaSeconds) || deltaSeconds < 0)
    {
        throw EngineError("Outdoor clock requires valid settings and finite nonnegative delta seconds.");
    }
    if (!settings.dayCycle.paused)
    {
        auto& clock = settings.dayCycle;
        clock.timeHours = std::fmod(clock.timeHours + double(deltaSeconds) * 24 / clock.cycleSeconds, 24.0);
        clock.elapsedSeconds += deltaSeconds;
    }
}
math::Vec3 atmosphereTransmission(const AtmosphereSettings& s, float altitude, float mu)
{
    // Spherical atmosphere in kilometres. Coefficients are extinction per kilometre.
    const double radius = 6360, top = 6460, height = radius + std::max(0.001, double(altitude) * 0.001);
    if (height >= top)
    {
        return {1, 1, 1};
    }
    mu = std::clamp(mu, -1.0f, 1.0f);
    double ground = height * height * (mu * mu - 1) + radius * radius;
    if (mu < 0 && ground >= 0)
    {
        return {};
    }
    double distance = -height * mu + std::sqrt(height * height * (mu * mu - 1) + top * top);
    double ray = 0, mie = 0, ozone = 0;
    for (int i = 0; i < 64; ++i)
    {
        double t = distance * (i + 0.5) / 64;
        double h = std::sqrt(height * height + t * t + 2 * height * mu * t) - radius;
        ray += std::exp(-h / 8) * distance / 64;
        mie += std::exp(-h / 1.2) * distance / 64;
        ozone += std::max(0.0, 1 - std::abs(h - 25) / 15) * distance / 64;
    }
    return {
        float(std::exp(-ray * 0.005802 * s.rayleigh - mie * 0.00444 * s.mie - ozone * 0.000650 * s.ozone)),
        float(std::exp(-ray * 0.013558 * s.rayleigh - mie * 0.00444 * s.mie - ozone * 0.001881 * s.ozone)),
        float(std::exp(-ray * 0.0331 * s.rayleigh - mie * 0.00444 * s.mie - ozone * 0.000085 * s.ozone))
    };
}
OutdoorFrame evaluateOutdoorLighting(const OutdoorLighting& s, float altitude)
{
    if (!validOutdoorLighting(s) || !std::isfinite(altitude))
    {
        throw EngineError("Invalid outdoor lighting inputs.");
    }
    float angle = float((s.dayCycle.timeHours - 6) * std::numbers::pi / 12);
    float y = std::sin(angle) * std::sin(s.dayCycle.noonElevation);
    float z = -std::sin(angle) * std::cos(s.dayCycle.noonElevation), x = std::cos(angle);
    float az = s.dayCycle.azimuth;
    OutdoorFrame frame;
    frame.sunDirection = {std::cos(az) * x + std::sin(az) * z, y, -std::sin(az) * x + std::cos(az) * z};
    frame.moonDirection = math::mul(frame.sunDirection, -1);
    frame.moon = y < 0;
    frame.lightDirection = frame.moon ? frame.moonDirection : frame.sunDirection;
    frame.lightColor = atmosphereTransmission(s.atmosphere, altitude, frame.lightDirection.y);
    frame.illuminance = (frame.moon ? s.atmosphere.moonIlluminance : s.atmosphere.sunIlluminance) *
                        smooth(0, 0.035f, frame.lightDirection.y);
    frame.exposureMultiplier = outdoorExposureMultiplier(s.exposure, y);
    return frame;
}
float outdoorExposureMultiplier(const OutdoorExposureSettings& settings, float sunHeight)
{
    float elevation = std::asin(std::clamp(sunHeight, -1.0f, 1.0f)) * 180 / std::numbers::pi_v<float>;
    float ev = elevation >= 0 ? std::lerp(settings.twilightEv, settings.dayEv, smooth(0, 15, elevation))
                              : std::lerp(settings.nightEv, settings.twilightEv, smooth(-12, 0, elevation));
    if (!settings.automatic)
    {
        ev = settings.manualEv;
    }
    return 1 / (1.2f * std::exp2(ev));
}
} // namespace ofg
