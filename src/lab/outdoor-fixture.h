// Reproducible outdoor inspection scene for atmosphere, IBL and shadow continuity.
#pragma once
#include "scene/scene.h"
namespace ofg {
// Creates ground, PBR spheres, tall casters and distant landmarks with a paused daytime sky.
std::unique_ptr<Scene> createOutdoorFixture();
} // namespace ofg
