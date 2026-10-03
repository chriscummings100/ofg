// Procedural diagnostic assets and stationary scene shared by native/browser hosts and integration tests.
#pragma once

#include "scene/scene.h"

namespace ofg {
// Creates a white-tinted sampled material; omitted texture uses a procedural checker.
std::shared_ptr<Material> createFixtureMaterial(std::shared_ptr<Texture> texture = {});
// Creates a unit cube with 24 vertices and six independently material-addressable face submeshes.
std::shared_ptr<Mesh> createCubeMesh(std::shared_ptr<Material> material);
// Creates a procedural checker texture in a supported linear floating-point format for portability checks.
std::shared_ptr<Texture> createFloatFixtureTexture(bool fp32);
// Creates the scene around an explicitly supplied sampled texture.
std::unique_ptr<Scene> createSceneFixture(std::shared_ptr<Texture> texture);
// Loads the given image path, or uses a procedural checker, in the shared cube scene.
std::unique_ptr<Scene> createSceneFixture(const std::string& texturePath = {});
} // namespace ofg
