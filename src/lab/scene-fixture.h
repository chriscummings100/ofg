// Procedural diagnostic assets and stationary scene shared by native/browser hosts and integration tests.
#pragma once

#include "scene/scene.h"

namespace ofg {
// Creates the embedded UV checker shader's white-tinted material.
std::shared_ptr<Material> createFixtureMaterial();
// Creates a unit cube with 24 vertices and six independently material-addressable face submeshes.
std::shared_ptr<Mesh> createCubeMesh(std::shared_ptr<Material> material);
// Creates parented, overlapping shared cubes, a tinted material clone, and one culled instance.
std::unique_ptr<Scene> createSceneFixture();
} // namespace ofg
