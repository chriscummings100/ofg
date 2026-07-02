// Shared shader layout for the opaque PBR renderer path.
//
// The demo scene, glTF importer, and renderer tests all use this layout so
// frame, draw, and material bindings cannot drift apart as the shader evolves.
#pragma once

#include "ofg/resources/shader.hpp"

namespace ofg {

// Returns the parameter layout expected by the opaque metallic-roughness WGSL shader.
[[nodiscard]] ShaderParameterLayout opaque_pbr_shader_layout();

} // namespace ofg
