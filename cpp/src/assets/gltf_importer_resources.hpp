// Internal helpers for glTF importer resource creation.
//
// This file keeps PBR material/texture cache logic out of the structural glTF
// node/mesh importer. It is not a public engine API.
#pragma once

#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"
#include "ofg/resources/material.hpp"

#include <string>

namespace ofg::gltf_importer_detail {

// Returns a stable source key for cache entries.
[[nodiscard]] std::string source_key(const GltfDocument& document, const GltfImportOptions& options);

// Returns whether a primitive material requires texture coordinates.
[[nodiscard]] bool primitive_requires_uvs(const GltfDocument& document, const GltfPrimitive& primitive);

// Returns whether the primitive's material references a normal texture.
[[nodiscard]] bool primitive_uses_normal_texture(const GltfDocument& document, const GltfPrimitive& primitive);

// Returns a PBR material for a primitive, creating a cached material if needed.
[[nodiscard]] Material& material_for_primitive(const GltfDocument& document,
    const GltfImportOptions& options,
    ModelResourceImportContext& context,
    const GltfPrimitive& primitive);

} // namespace ofg::gltf_importer_detail
