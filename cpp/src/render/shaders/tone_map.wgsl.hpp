// WGSL source for the final HDR scene-color tone-map pass.
#pragma once

namespace ofg::render::shaders {

constexpr char tone_map_wgsl[] = R"wgsl(
struct ToneMapUniforms {
    exposure: f32,
    output_encoding: f32,
    bloom_intensity: f32,
    bloom_width: f32,
    bloom_height: f32,
    bloom_tint_r: f32,
    bloom_tint_g: f32,
    bloom_tint_b: f32,
};

@group(0) @binding(0) var<uniform> tone_map: ToneMapUniforms;
@group(0) @binding(1) var scene_color: texture_2d<f32>;
@group(0) @binding(2) var bloom_texture: texture_2d<f32>;

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

fn sample_bloom(pixel: vec2<i32>, output_size: vec2<u32>) -> vec3<f32> {
    if (tone_map.bloom_intensity <= 0.0 || tone_map.bloom_width < 1.0 || tone_map.bloom_height < 1.0) {
        return vec3<f32>(0.0);
    }

    let bloom_size = vec2<u32>(u32(tone_map.bloom_width), u32(tone_map.bloom_height));
    let clamped_pixel = max(pixel, vec2<i32>(0));
    let safe_pixel = vec2<u32>(u32(clamped_pixel.x), u32(clamped_pixel.y));
    let output_extent = max(output_size, vec2<u32>(1));
    let bloom_pixel_u = min((safe_pixel * bloom_size) / output_extent, bloom_size - vec2<u32>(1));
    let bloom_pixel = vec2<i32>(i32(bloom_pixel_u.x), i32(bloom_pixel_u.y));
    let bloom = max(textureLoad(bloom_texture, bloom_pixel, 0).rgb, vec3<f32>(0.0));
    let tint = vec3<f32>(tone_map.bloom_tint_r, tone_map.bloom_tint_g, tone_map.bloom_tint_b);
    return bloom * tone_map.bloom_intensity * tint;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let pixel = vec2<i32>(floor(input.position.xy));
    let scene_size = textureDimensions(scene_color, 0);
    let hdr_color = max(textureLoad(scene_color, pixel, 0).rgb, vec3<f32>(0.0)) + sample_bloom(pixel, scene_size);
    let mapped = aces_fitted(hdr_color * tone_map.exposure);
    var output_color = mapped;
    if (tone_map.output_encoding > 0.5) {
        output_color = linear_to_srgb(mapped);
    }
    return vec4<f32>(output_color, 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
