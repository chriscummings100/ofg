// WGSL source for the first OFG opaque draw-list renderer.
//
// MeshVertex normal data is treated as vertex color for this first renderer
// slice. Materials always bind a color factor plus base-color texture/sampler.
#pragma once

namespace ofg::render::shaders {

constexpr char opaque_uber_wgsl[] = R"wgsl(
struct FrameUniforms {
    view_projection: mat4x4<f32>,
};

struct DrawUniforms {
    model: mat4x4<f32>,
};

struct MaterialUniforms {
    base_color_factor: vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var<uniform> draw: DrawUniforms;
@group(2) @binding(0) var<uniform> material: MaterialUniforms;
@group(2) @binding(1) var base_color_texture: texture_2d<f32>;
@group(2) @binding(2) var base_color_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_position = draw.model * vec4<f32>(input.position, 1.0);
    out.position = frame.view_projection * world_position;
    out.color = input.color;
    out.uv = input.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let texel = textureSample(base_color_texture, base_color_sampler, input.uv);
    return vec4<f32>(input.color, 1.0) * texel * material.base_color_factor;
}
)wgsl";

} // namespace ofg::render::shaders
