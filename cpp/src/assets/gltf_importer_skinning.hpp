// Internal helpers for glTF skinning attribute import.
//
// These helpers decode JOINTS_0 and WEIGHTS_0 into OFG skin influences while
// keeping source-format accessor details out of scene renderer code.
#pragma once

#include "ofg/animation/skinning.hpp"
#include "ofg/assets/gltf_document.hpp"

#include <cstdint>
#include <vector>

namespace ofg::gltf_importer_detail {

// Imports per-vertex skin influences for one glTF mesh.
[[nodiscard]] std::vector<SkinVertexInfluence> import_skin_vertex_influences(
    const GltfDocument& document, std::uint32_t mesh_index);

} // namespace ofg::gltf_importer_detail
