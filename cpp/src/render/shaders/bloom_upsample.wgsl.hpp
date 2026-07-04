// WGSL source for bloom pyramid upsample and accumulation passes.
#pragma once

namespace ofg::render::shaders {

constexpr char bloom_upsample_wgsl[] = R"wgsl(
struct BloomUniforms {
    values: array<vec4<f32>, 4>,
};

@group(0) @binding(0) var<uniform> bloom: BloomUniforms;
@group(0) @binding(1) var lower_texture: texture_2d<f32>;
@group(0) @binding(2) var higher_texture: texture_2d<f32>;

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

fn scatter() -> f32 {
    return clamp(bloom.values[0].w, 0.0, 1.0);
}

fn clamp_value() -> f32 {
    return bloom.values[1].x;
}

fn clamp_coord(coord: vec2<i32>, dims: vec2<i32>) -> vec2<i32> {
    return clamp(coord, vec2<i32>(0), dims - vec2<i32>(1));
}

fn load_lower(coord: vec2<i32>, dims: vec2<i32>) -> vec3<f32> {
    return max(textureLoad(lower_texture, clamp_coord(coord, dims), 0).rgb, vec3<f32>(0.0));
}

fn lower_center_for_pixel(pixel: vec2<i32>, output_size: vec2<u32>, lower_size: vec2<u32>) -> vec2<i32> {
    let clamped_pixel = max(pixel, vec2<i32>(0));
    let safe_pixel = vec2<u32>(u32(clamped_pixel.x), u32(clamped_pixel.y));
    let center = min((safe_pixel * lower_size) / max(output_size, vec2<u32>(1)), lower_size - vec2<u32>(1));
    return vec2<i32>(i32(center.x), i32(center.y));
}

fn tent_sample_lower(center: vec2<i32>, dims: vec2<i32>) -> vec3<f32> {
    var sum = vec3<f32>(0.0);
    sum += load_lower(center + vec2<i32>(-1, -1), dims) * 1.0;
    sum += load_lower(center + vec2<i32>( 0, -1), dims) * 2.0;
    sum += load_lower(center + vec2<i32>( 1, -1), dims) * 1.0;
    sum += load_lower(center + vec2<i32>(-1,  0), dims) * 2.0;
    sum += load_lower(center + vec2<i32>( 0,  0), dims) * 4.0;
    sum += load_lower(center + vec2<i32>( 1,  0), dims) * 2.0;
    sum += load_lower(center + vec2<i32>(-1,  1), dims) * 1.0;
    sum += load_lower(center + vec2<i32>( 0,  1), dims) * 2.0;
    sum += load_lower(center + vec2<i32>( 1,  1), dims) * 1.0;
    return sum * (1.0 / 16.0);
}

@fragment
fn fs_upsample(input: VertexOutput) -> @location(0) vec4<f32> {
    let pixel = vec2<i32>(floor(input.position.xy));
    let lower_size_u = textureDimensions(lower_texture, 0);
    let higher_size_u = textureDimensions(higher_texture, 0);
    let lower_dims = vec2<i32>(i32(lower_size_u.x), i32(lower_size_u.y));
    let higher_dims = vec2<i32>(i32(higher_size_u.x), i32(higher_size_u.y));
    let lower_center = lower_center_for_pixel(pixel, higher_size_u, lower_size_u);
    let lower_blur = tent_sample_lower(lower_center, lower_dims);
    let higher = max(textureLoad(higher_texture, clamp_coord(pixel, higher_dims), 0).rgb, vec3<f32>(0.0));
    return vec4<f32>(min(higher + lower_blur * scatter(), vec3<f32>(clamp_value())), 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
