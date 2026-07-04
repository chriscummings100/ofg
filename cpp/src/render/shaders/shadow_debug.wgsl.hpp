// WGSL source for visualizing cascaded shadow-map depth layers.
#pragma once

namespace ofg::render::shaders {

constexpr char shadow_debug_wgsl[] = R"wgsl(
struct ShadowDebugUniforms {
    output_size: vec4<f32>,
};

@group(0) @binding(0) var<uniform> debug: ShadowDebugUniforms;
@group(0) @binding(1) var shadow_map: texture_depth_2d_array;

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

fn cascade_tint(index: u32) -> vec3<f32> {
    if (index == 0u) {
        return vec3<f32>(1.0, 0.22, 0.16);
    }
    if (index == 1u) {
        return vec3<f32>(0.18, 0.78, 0.22);
    }
    return vec3<f32>(0.24, 0.42, 1.0);
}

fn load_shadow_depth(layer: u32, texel: vec2<i32>, max_texel: vec2<i32>) -> f32 {
    let clamped_texel = clamp(texel, vec2<i32>(0), max_texel);
    return clamp(textureLoad(shadow_map, clamped_texel, i32(layer), 0), 0.0, 1.0);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let output_size = max(debug.output_size.xy, vec2<f32>(1.0));
    let margin = 8.0;
    let gap = 8.0;
    let available_width = max(output_size.x - margin * 2.0 - gap * 2.0, 1.0);
    let panel_size = max(min(available_width / 3.0, output_size.y * 0.30), 1.0);
    let pixel = input.position.xy;
    let shadow_size = textureDimensions(shadow_map, 0);
    let max_texel = vec2<i32>(i32(shadow_size.x) - 1, i32(shadow_size.y) - 1);

    for (var cascade_index = 0u; cascade_index < 3u; cascade_index = cascade_index + 1u) {
        let origin = vec2<f32>(margin + f32(cascade_index) * (panel_size + gap), margin);
        let local = pixel - origin;
        if (local.x >= 0.0 && local.y >= 0.0 && local.x < panel_size && local.y < panel_size) {
            let border = 2.0;
            let tint = cascade_tint(cascade_index);
            if (local.x < border || local.y < border || local.x >= panel_size - border || local.y >= panel_size - border) {
                return vec4<f32>(tint, 1.0);
            }

            let uv = clamp(local / vec2<f32>(panel_size), vec2<f32>(0.0), vec2<f32>(0.99999));
            let texel = min(vec2<i32>(uv * vec2<f32>(shadow_size)), max_texel);
            let depth = load_shadow_depth(cascade_index, texel, max_texel);
            let left = load_shadow_depth(cascade_index, texel + vec2<i32>(-1, 0), max_texel);
            let right = load_shadow_depth(cascade_index, texel + vec2<i32>(1, 0), max_texel);
            let up = load_shadow_depth(cascade_index, texel + vec2<i32>(0, -1), max_texel);
            let down = load_shadow_depth(cascade_index, texel + vec2<i32>(0, 1), max_texel);
            let edge = clamp(max(max(abs(depth - left), abs(depth - right)), max(abs(depth - up), abs(depth - down))) * 96.0, 0.0, 1.0);
            let occupancy = clamp((1.0 - depth) * 16.0, 0.0, 1.0);
            let raw_depth = vec3<f32>(pow(depth, 0.75));
            let shaded = min(mix(raw_depth, tint, 0.10) + vec3<f32>(edge) + tint * occupancy * 0.25, vec3<f32>(1.0));
            return vec4<f32>(shaded, 1.0);
        }
    }

    discard;
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}
)wgsl";

} // namespace ofg::render::shaders
