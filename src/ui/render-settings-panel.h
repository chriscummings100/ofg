// Lighting widgets edit a draft; Workspace retains validation, reset and window ownership.
#pragma once
#include "scene/lighting.h"
namespace ofg {
// Draws existing and outdoor controls, returning whether the draft changed.
bool drawLightingControls(Lighting& draft);
} // namespace ofg
