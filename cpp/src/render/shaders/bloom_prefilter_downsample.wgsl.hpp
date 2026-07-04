// WGSL source for bloom bright-pass extraction and downsample passes.
#pragma once

namespace ofg::render::shaders {

constexpr char bloom_prefilter_downsample_wgsl[] = R"wgsl(
struct BloomUniforms {
    values: array<vec4<f32>, 4>,
};

@group(0) @binding(0) var<uniform> bloom: BloomUniforms;
@group(0) @binding(1) var source_texture: texture_2d<f32>;

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

fn threshold() -> f32 {
    return bloom.values[0].x;
}

fn soft_knee() -> f32 {
    return bloom.values[0].y;
}

fn clamp_value() -> f32 {
    return bloom.values[1].x;
}

fn initial_downscale() -> i32 {
    return clamp(i32(bloom.values[2].x + 0.5), 1, 4);
}

fn source_dimensions_i32() -> vec2<i32> {
    let dims = textureDimensions(source_texture, 0);
    return vec2<i32>(i32(dims.x), i32(dims.y));
}

fn clamp_coord(coord: vec2<i32>, dims: vec2<i32>) -> vec2<i32> {
    return clamp(coord, vec2<i32>(0), dims - vec2<i32>(1));
}

fn load_source(coord: vec2<i32>, dims: vec2<i32>) -> vec3<f32> {
    return max(textureLoad(source_texture, clamp_coord(coord, dims), 0).rgb, vec3<f32>(0.0));
}

fn prefilter_color(hdr_color: vec3<f32>) -> vec3<f32> {
    let limited = min(max(hdr_color, vec3<f32>(0.0)), vec3<f32>(clamp_value()));
    let brightness = max(max(limited.r, limited.g), limited.b);
    if (brightness <= 0.000001) {
        return vec3<f32>(0.0);
    }

    let knee = threshold() * soft_knee();
    var contribution = 0.0;
    if (knee <= 0.000001) {
        contribution = max(brightness - threshold(), 0.0) / brightness;
    } else {
        var soft = clamp(brightness - threshold() + knee, 0.0, 2.0 * knee);
        soft = soft * soft / max(4.0 * knee, 0.000001);
        contribution = max(brightness - threshold(), soft) / brightness;
    }
    return limited * clamp(contribution, 0.0, 1.0);
}

@fragment
fn fs_prefilter(input: VertexOutput) -> @location(0) vec4<f32> {
    let pixel = vec2<i32>(floor(input.position.xy));
    let dims = source_dimensions_i32();
    let factor = initial_downscale();
    let source_origin = pixel * factor;
    var sum = vec3<f32>(0.0);

    for (var y = 0; y < 4; y = y + 1) {
        if (y < factor) {
            for (var x = 0; x < 4; x = x + 1) {
                if (x < factor) {
                    sum += prefilter_color(load_source(source_origin + vec2<i32>(x, y), dims));
                }
            }
        }
    }

    let sample_count = f32(factor * factor);
    return vec4<f32>(sum / sample_count, 1.0);
}

@fragment
fn fs_downsample(input: VertexOutput) -> @location(0) vec4<f32> {
    let pixel = vec2<i32>(floor(input.position.xy));
    let dims = source_dimensions_i32();
    let center = pixel * 2;
    let offsets = array<vec2<i32>, 13>(
        vec2<i32>(-2, -2), vec2<i32>( 0, -2), vec2<i32>( 2, -2),
        vec2<i32>(-2,  0), vec2<i32>( 0,  0), vec2<i32>( 2,  0),
        vec2<i32>(-2,  2), vec2<i32>( 0,  2), vec2<i32>( 2,  2),
        vec2<i32>(-1, -1), vec2<i32>( 1, -1),
        vec2<i32>(-1,  1), vec2<i32>( 1,  1)
    );
    let weights = array<f32, 13>(
        0.03125, 0.0625, 0.03125,
        0.0625,  0.125,  0.0625,
        0.03125, 0.0625, 0.03125,
        0.125,   0.125,
        0.125,   0.125
    );

    var sum = vec3<f32>(0.0);
    for (var index: u32 = 0u; index < 13u; index = index + 1u) {
        sum += load_source(center + offsets[index], dims) * weights[index];
    }
    return vec4<f32>(sum, 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
