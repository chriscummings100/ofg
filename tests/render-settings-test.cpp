// Laboratory edit boundaries and entity display-name identity contracts, without UI or GPU dependencies.
#include "lab/render-settings.h"
#include "scene/scene.h"
#include <doctest.h>
#include <limits>

using namespace ofg;

TEST_CASE("entity display names are owned, optional and independent of identity")
{
    Scene scene;
    auto first = scene.createEntity(scene.getRoot());
    auto second = scene.createEntity(scene.getRoot());
    CHECK(first->name().empty());
    std::string label = "Sphere";
    first->setName(label);
    label = "Changed";
    second->setName("Sphere");
    CHECK(first->name() == second->name());
    CHECK(first->id() != second->id());
    Ptr<Entity> selected = first;
    scene.clear();
    CHECK_FALSE(selected);
    CHECK(scene.getRoot()->name().empty());
}

TEST_CASE("lighting edits commit finite values atomically and reject invalid shader domains")
{
    Lighting target;
    target.lightCount = 1;
    auto candidate = target;
    candidate.exposure = 2;
    candidate.environmentIntensity = 0;
    candidate.environmentRotation = -1;
    candidate.lights[0].intensity = 0;
    REQUIRE(applyLightingEdit(target, candidate));
    CHECK(target.exposure == 2);
    for (float value : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        candidate = target;
        candidate.exposure = value;
        candidate.debugView = 3;
        CHECK_FALSE(applyLightingEdit(target, candidate));
        CHECK(target.exposure == 2);
        CHECK(target.debugView == 0);
    }
    candidate = target;
    candidate.lights[0].direction = {};
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate.lights[0].type = LightType::Point;
    candidate.lights[0].direction = {0, 0, 1};
    CHECK(applyLightingEdit(target, candidate));
    candidate.lights[0].range = -1;
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate = target;
    candidate.lights[0].color.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate = target;
    candidate.lights[0].type = LightType::Spot;
    candidate.lights[0].direction = {0, 0, 1};
    candidate.lights[0].innerCone = 0.3f;
    candidate.lights[0].outerCone = 0.3f;
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate.lights[0].outerCone = 0.6f;
    CHECK(applyLightingEdit(target, candidate));
    candidate.lights[0].outerCone = 2;
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate = target;
    candidate.debugView = 5;
    CHECK_FALSE(applyLightingEdit(target, candidate));
    candidate = target;
    candidate.lightCount = 5;
    CHECK_FALSE(applyLightingEdit(target, candidate));
}
