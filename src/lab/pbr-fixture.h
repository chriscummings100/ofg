// The shared material-review scene and its reproducible camera pose.
#pragma once
#include "scene/scene.h"

namespace ofg {
// Decodes the embedded reproducible studio bake, with no file loading or Python at runtime.
std::shared_ptr<Environment> createStudioEnvironment();
// Creates the 7x7 sphere grid; columns are roughness and rows are metallic, increasing from left/bottom.
std::unique_ptr<Scene> createPbrFixture(bool maximumLayout = false);
} // namespace ofg
