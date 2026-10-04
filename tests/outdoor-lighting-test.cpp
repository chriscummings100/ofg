// CPU contract tests for outdoor settings, clock, optical depth and conservative cascade construction.
#include "scene/outdoor-lighting.h"
#include "render/shadow-cascades.h"
#include "render/draw-list.h"
#include "scene/scene.h"
#include "resources/pbr-material.h"
#include "lab/render-settings.h"
#include "math/transform.h"
#include <doctest.h>
#include <cmath>
#include <limits>
using namespace ofg;
TEST_CASE("Outdoor clock wraps and settings drafts remain independent")
{
    Lighting initial;
    initial.outdoor = OutdoorLighting{};
    auto draft = initial;
    draft.outdoor->dayCycle.timeHours = 23.9;
    draft.outdoor->dayCycle.paused = false;
    advanceOutdoorLighting(*draft.outdoor, 10);
    CHECK(draft.outdoor->dayCycle.timeHours == doctest::Approx(.1));
    CHECK(initial.outdoor->dayCycle.timeHours == 10);
    CHECK(applyLightingEdit(initial, draft));
    draft.outdoor->clouds.coverage = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(applyLightingEdit(initial, draft));
    CHECK(initial.outdoor->clouds.coverage == .35f);
    CHECK_THROWS(advanceOutdoorLighting(*initial.outdoor, -1));
}
TEST_CASE("Outdoor celestial directions and horizon transitions are deterministic")
{
    OutdoorLighting s;
    s.dayCycle.timeHours = 12;
    auto day = evaluateOutdoorLighting(s, 2);
    CHECK(math::length(day.sunDirection) == doctest::Approx(1));
    CHECK(day.sunDirection.y == doctest::Approx(std::sqrt(.75f)));
    CHECK(math::dot(day.sunDirection, day.moonDirection) == doctest::Approx(-1));
    CHECK_FALSE(day.moon);
    s.dayCycle.timeHours = 0;
    auto night = evaluateOutdoorLighting(s, 2);
    CHECK(night.moon);
    CHECK(night.illuminance < day.illuminance * .001f);
    CHECK(night.exposureMultiplier > day.exposureMultiplier);
    s.dayCycle.timeHours = 18;
    CHECK(evaluateOutdoorLighting(s, 2).illuminance < .001f);
}
TEST_CASE("Outdoor exposure preserves authored EV controls independently of captured sun height")
{
    OutdoorExposureSettings settings;
    CHECK(outdoorExposureMultiplier(settings, 1) == doctest::Approx(1 / (1.2 * std::exp2(15))));
    CHECK(outdoorExposureMultiplier(settings, 0) == doctest::Approx(1 / (1.2 * std::exp2(10))));
    CHECK(outdoorExposureMultiplier(settings, -1) == doctest::Approx(1 / (1.2 * std::exp2(-3))));
    settings.dayEv = 12;
    CHECK(outdoorExposureMultiplier(settings, 1) == doctest::Approx(1 / (1.2 * std::exp2(12))));
    settings.automatic = false;
    settings.manualEv = 5;
    CHECK(outdoorExposureMultiplier(settings, -1) == outdoorExposureMultiplier(settings, 1));
    CHECK(outdoorExposureMultiplier(settings, 0) == doctest::Approx(1 / (1.2 * std::exp2(5))));
}
TEST_CASE("Outdoor transmission obeys vacuum planet and optical depth boundaries")
{
    AtmosphereSettings s;
    auto clear = atmosphereTransmission(s, 2, 1);
    CHECK(clear.x > clear.z);
    CHECK(clear.x <= 1);
    CHECK(clear.z > 0);
    auto horizon = atmosphereTransmission(s, 2, .01f);
    CHECK(horizon.x < clear.x);
    CHECK(atmosphereTransmission(s, 2, -1).x == 0);
    s.rayleigh = s.mie = s.ozone = 0;
    CHECK(atmosphereTransmission(s, 2, 1).x == doctest::Approx(1));
}
TEST_CASE("Outdoor cascades cover receiver splits and offscreen caster depths")
{
    ShadowCamera camera;
    camera.aspectRatio = 1.5f;
    ShadowSettings settings;
    Bounds caster{{-3, 50, 5}, {3, 100, 15}};
    auto cascades = buildShadowCascades(camera, {0, 1, 0}, settings, std::span(&caster, 1));
    float previous = camera.nearDistance;
    for (const auto& c : cascades)
    {
        CHECK(c.nearDistance == doctest::Approx(previous));
        CHECK(c.farDistance > previous);
        CHECK(c.texelSize > 0);
        for (float distance : {c.nearDistance, c.farDistance})
        {
            float y = std::tan(camera.verticalFov / 2) * distance;
            auto p = math::mul(c.clipFromWorld, math::Vec4{y * camera.aspectRatio, y, distance, 1});
            CHECK(std::abs(p.x / p.w) <= 1.00001);
            CHECK(std::abs(p.y / p.w) <= 1.00001);
            CHECK(p.z / p.w >= 0);
            CHECK(p.z / p.w <= 1);
        }
        previous = c.farDistance;
    }
    CHECK(previous == doctest::Approx(settings.distance));
    CHECK(boundsVisible(caster, cascades[0].clipFromWorld));
    auto shifted = camera;
    shifted.worldFromView[3].x = .00001f;
    auto stable = buildShadowCascades(shifted, {0, 1, 0}, settings, std::span(&caster, 1));
    CHECK(stable[0].clipFromWorld[3].x == doctest::Approx(cascades[0].clipFromWorld[3].x));
}
TEST_CASE("Outdoor extraction retains loaded casters outside camera visibility")
{
    Scene scene;
    auto camera = scene.createCamera(scene.createEntity(scene.getRoot()));
    scene.lighting.outdoor = OutdoorLighting{};
    auto material = createPbrMaterial({});
    auto mesh = Mesh::create({{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 0}}}, {0, 1, 2}, {{0, 3, material}});
    auto entity = scene.createEntity(scene.getRoot());
    entity->setLocalPosition({100, 0, 5});
    scene.createMeshRenderer(entity)->setMesh(mesh);
    auto list = buildDrawList(scene, *camera, 1);
    CHECK(list.items.empty());
    CHECK(list.shadowCasters.size() == 1);
    entity->meshRenderer()->setCastsShadows(false);
    CHECK(buildDrawList(scene, *camera, 1).shadowCasters.empty());
    scene.lighting.outdoor.reset();
    CHECK(buildDrawList(scene, *camera, 1).shadowCasters.empty());
}
