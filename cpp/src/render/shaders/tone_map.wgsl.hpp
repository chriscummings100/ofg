// WGSL source for the final HDR scene-color tone-map pass.
#pragma once

namespace ofg::render::shaders {

constexpr char tone_map_wgsl[] = R"wgsl(
struct ToneMapUniforms {
    exposure: f32,
    output_encoding: f32,
    unused0: f32,
    unused1: f32,
};

@group(0) @binding(0) var<uniform> tone_map: ToneMapUniforms;
@group(0) @binding(1) var scene_color: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );

    var output: VertexOutput;
    output.position = vec4<f32>(positions[vertex_index], 0.0, 1.0);
    return output;
}

fn aces_fitted(color: vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linear_to_srgb_channel(value: f32) -> f32 {
    if (value <= 0.0031308) {
        return value * 12.92;
    }
    return 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

fn linear_to_srgb(color: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(
        linear_to_srgb_channel(color.r),
        linear_to_srgb_channel(color.g),
        linear_to_srgb_channel(color.b)
    );
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let pixel = vec2<i32>(floor(input.position.xy));
    let hdr_color = max(textureLoad(scene_color, pixel, 0).rgb, vec3<f32>(0.0));
    let mapped = aces_fitted(hdr_color * tone_map.exposure);
    var output_color = mapped;
    if (tone_map.output_encoding > 0.5) {
        output_color = linear_to_srgb(mapped);
    }
    return vec4<f32>(output_color, 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
