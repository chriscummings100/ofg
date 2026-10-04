// Procedural smooth sphere geometry for repeatable material inspection without an asset importer.
#pragma once
#include "resources/mesh.h"

namespace ofg {
// Creates a unit-radius sphere with duplicated UV seam/poles and outward triangle winding.
std::shared_ptr<Mesh> createSphereMesh(std::shared_ptr<Material> material, uint32_t slices = 48, uint32_t stacks = 24);
} // namespace ofg
