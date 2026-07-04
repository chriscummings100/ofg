// WGSL source for depth-only cascaded shadow caster rendering.
#pragma once

namespace ofg::render::shaders {

constexpr char shadow_caster_wgsl[] = R"wgsl(
struct CascadeFrameUniforms {
    clip_from_world: mat4x4<f32>,
};

struct DrawUniforms {
    model: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> frame: CascadeFrameUniforms;
@group(1) @binding(0) var<uniform> draw: DrawUniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4<f32> {
    return frame.clip_from_world * draw.model * vec4<f32>(input.position, 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
