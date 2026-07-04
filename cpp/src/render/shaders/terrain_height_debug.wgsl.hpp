// WGSL source for visualizing terrain heightfield debug textures.
//
// The shader consumes the same frame and draw bind groups as the opaque pass,
// plus one R16Float heightfield texture per material. Negative heights render
// red, positive heights render green, and zero height renders black.
#pragma once

namespace ofg::render::shaders {

constexpr char terrain_height_debug_wgsl[] = R"wgsl(
struct FrameUniforms {
    view_projection: mat4x4<f32>,
    view_from_world: mat4x4<f32>,
    main_light_direction: vec4<f32>,
    main_light_color: vec4<f32>,
    ambient_light_color: vec4<f32>,
    camera_position: vec4<f32>,
};

struct DrawUniforms {
    model: mat4x4<f32>,
    normal_model: mat4x4<f32>,
};

struct MaterialUniforms {
    height_debug_options: vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var<uniform> draw: DrawUniforms;
@group(2) @binding(0) var<uniform> material: MaterialUniforms;
@group(2) @binding(1) var heightfield_texture: texture_2d<f32>;
@group(2) @binding(2) var heightfield_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) tangent: vec4<f32>,
    @location(3) uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_position = draw.model * vec4<f32>(input.position, 1.0);
    out.position = frame.view_projection * world_position;
    out.uv = input.uv;
    return out;
}

fn heightfield_texel(uv: vec2<f32>) -> f32 {
    let dimensions = vec2<i32>(textureDimensions(heightfield_texture));
    let coordinate = clamp(vec2<i32>(floor(uv * vec2<f32>(dimensions))), vec2<i32>(0), dimensions - vec2<i32>(1));
    return textureLoad(heightfield_texture, coordinate, 0).r;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let height = heightfield_texel(input.uv);
    let divisor = max(abs(material.height_debug_options.x), 0.0001);
    let intensity = clamp(abs(height) / divisor, 0.0, 1.0);
    if (height < 0.0) {
        return vec4<f32>(intensity, 0.0, 0.0, 1.0);
    }
    return vec4<f32>(0.0, intensity, 0.0, 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
