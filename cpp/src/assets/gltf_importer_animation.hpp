// Internal helpers for glTF animation import.
//
// The public model importer owns ModelResource creation. This helper keeps
// animation sampler/channel decoding in a focused translation unit.
#pragma once

#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"

#include <cstdint>
#include <memory>

namespace ofg {

class AnimationClip;

namespace gltf_importer_detail {

// Imports one glTF animation into an engine-owned AnimationClip.
[[nodiscard]] std::unique_ptr<AnimationClip> import_animation_clip(
    const GltfDocument& document, const GltfImportOptions& options, std::uint32_t animation_index);

} // namespace gltf_importer_detail
} // namespace ofg
