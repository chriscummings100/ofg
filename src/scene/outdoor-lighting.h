// Copyable outdoor controls and deterministic celestial evaluation; no graphics or UI dependencies.
#pragma once
#include "math/vec.h"
#include <cstdint>
namespace ofg {
struct DayCycleSettings
{
    double timeHours = 10, elapsedSeconds = 0;
    float cycleSeconds = 1200, noonElevation = 1.04719755f, azimuth = 0;
    bool paused = true;
};
struct AtmosphereSettings
{
    float rayleigh = 1, mie = 1, ozone = 1, groundAlbedo = 0.15f;
    float sunIlluminance = 120000, moonIlluminance = 0.25f;
    float aerialDistance = 10000;
};
struct CloudSettings
{
    float coverage = 0.35f, opticalThickness = 2, altitude = 1500, secondAltitude = 3500;
    float scale = 0.0005f, windSpeed = 10, windAngle = 0;
};
struct ShadowSettings
{
    bool enabled = true;
    uint32_t resolution = 1024;
    float distance = 250, splitLambda = 0.7f, transition = 0.1f;
    // Covers the 3x3 PCF footprint to reduce self-shadow striping on curved receivers.
    float normalBias = 1.5f, depthBias = 0.0001f;
    uint32_t debugView = 0; // 0 shaded, 1 cascade selection, 2 visibility.
};
struct OutdoorExposureSettings
{
    bool automatic = true;
    float dayEv = 15, twilightEv = 10, nightEv = -3, manualEv = 15;
};
struct OutdoorLighting
{
    DayCycleSettings dayCycle;
    AtmosphereSettings atmosphere;
    CloudSettings clouds;
    ShadowSettings shadows;
    OutdoorExposureSettings exposure;
    bool showDisks = true;
};
struct OutdoorFrame
{
    math::Vec3 sunDirection{}, moonDirection{}, lightDirection{}, lightColor{}; // Directions toward bodies.
    float illuminance = 0, exposureMultiplier = 1;
    bool moon = false;
};
// Validates finite controls and their supported physical domains without modifying settings.
bool validOutdoorLighting(const OutdoorLighting& settings) noexcept;
// Advances the unpaused game clock and wind time; rejects negative/nonfinite time intervals.
void advanceOutdoorLighting(OutdoorLighting& settings, float deltaSeconds);
// Integrates clear-air RGB transmission from observer altitude in metres toward a celestial source.
// Requires validated density controls and finite altitude/cosine inputs.
math::Vec3 atmosphereTransmission(const AtmosphereSettings& settings, float altitude, float cosineZenith);
// Evaluates opposite sun/moon directions, horizon fade and exposure at observer altitude in metres.
OutdoorFrame evaluateOutdoorLighting(const OutdoorLighting& settings, float altitude);
// Returns exposure for validated settings and a sun direction's vertical component [-1,1]; manual EV ignores height.
float outdoorExposureMultiplier(const OutdoorExposureSettings& settings, float sunHeight);
} // namespace ofg
