// Validated laboratory edits to existing scene lighting, independent of UI and graphics libraries.
#pragma once
#include "scene/lighting.h"

namespace ofg {
// Applies a complete valid edit atomically; rejects nonfinite/domain-invalid values without changing destination.
bool applyLightingEdit(Lighting& destination, const Lighting& candidate);
} // namespace ofg
