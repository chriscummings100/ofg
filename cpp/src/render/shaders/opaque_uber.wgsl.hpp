// WGSL source for OFG's opaque PBR draw-list renderer.
//
// The first PBR path supports metallic-roughness materials, base-color textures,
// normal maps, one renderer-facing directional light, and an ambient term.
#pragma once

namespace ofg::render::shaders {

constexpr char opaque_uber_wgsl[] = R"wgsl(
struct FrameUniforms {
    view_projection: mat4x4<f32>,
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
    base_color_factor: vec4<f32>,
    pbr_factors: vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var<uniform> draw: DrawUniforms;
@group(2) @binding(0) var<uniform> material: MaterialUniforms;
@group(2) @binding(1) var base_color_texture: texture_2d<f32>;
@group(2) @binding(2) var base_color_sampler: sampler;
@group(2) @binding(3) var metallic_roughness_texture: texture_2d<f32>;
@group(2) @binding(4) var metallic_roughness_sampler: sampler;
@group(2) @binding(5) var normal_texture: texture_2d<f32>;
@group(2) @binding(6) var normal_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) tangent: vec4<f32>,
    @location(3) uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) world_position: vec3<f32>,
    @location(1) world_normal: vec3<f32>,
    @location(2) world_tangent: vec4<f32>,
    @location(3) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_position = draw.model * vec4<f32>(input.position, 1.0);
    out.position = frame.view_projection * world_position;
    out.world_position = world_position.xyz;
    out.world_normal = normalize((draw.normal_model * vec4<f32>(input.normal, 0.0)).xyz);
    out.world_tangent = vec4<f32>(normalize((draw.normal_model * vec4<f32>(input.tangent.xyz, 0.0)).xyz), input.tangent.w);
    out.uv = input.uv;
    return out;
}

fn saturate(value: f32) -> f32 {
    return clamp(value, 0.0, 1.0);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - saturate(cos_theta), 5.0);
}

fn distribution_ggx(n_dot_h: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let denom_base = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom_base * denom_base, 0.0001);
}

fn geometry_schlick_ggx(n_dot_v: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, 0.0001);
}

fn geometry_smith(n_dot_v: f32, n_dot_l: f32, roughness: f32) -> f32 {
    return geometry_schlick_ggx(n_dot_v, roughness) * geometry_schlick_ggx(n_dot_l, roughness);
}

fn surface_normal(input: VertexOutput) -> vec3<f32> {
    let geometry_normal = normalize(input.world_normal);
    if (material.pbr_factors.w < 0.5) {
        return geometry_normal;
    }

    let tangent = normalize(input.world_tangent.xyz - geometry_normal * dot(geometry_normal, input.world_tangent.xyz));
    let bitangent = normalize(cross(geometry_normal, tangent) * input.world_tangent.w);
    var mapped = textureSample(normal_texture, normal_sampler, input.uv).xyz * 2.0 - vec3<f32>(1.0);
    mapped = normalize(vec3<f32>(mapped.xy * material.pbr_factors.z, mapped.z));
    return normalize(tangent * mapped.x + bitangent * mapped.y + geometry_normal * mapped.z);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let base_sample = textureSample(base_color_texture, base_color_sampler, input.uv);
    let base_color = base_sample * material.base_color_factor;
    let metallic_roughness = textureSample(metallic_roughness_texture, metallic_roughness_sampler, input.uv);
    let metallic = saturate(material.pbr_factors.x * metallic_roughness.b);
    let roughness = clamp(material.pbr_factors.y * metallic_roughness.g, 0.04, 1.0);

    let normal = surface_normal(input);
    let view_direction = normalize(frame.camera_position.xyz - input.world_position);
    let light_direction = normalize(-frame.main_light_direction.xyz);
    let half_direction = normalize(view_direction + light_direction);

    let n_dot_l = saturate(dot(normal, light_direction));
    let n_dot_v = max(saturate(dot(normal, view_direction)), 0.0001);
    let n_dot_h = saturate(dot(normal, half_direction));
    let h_dot_v = saturate(dot(half_direction, view_direction));

    let f0 = mix(vec3<f32>(0.04), base_color.rgb, vec3<f32>(metallic));
    let f = fresnel_schlick(h_dot_v, f0);
    let d = distribution_ggx(n_dot_h, roughness);
    let g = geometry_smith(n_dot_v, n_dot_l, roughness);
    let specular = (d * g * f) / max(4.0 * n_dot_v * n_dot_l, 0.0001);
    let diffuse = (vec3<f32>(1.0) - f) * (1.0 - metallic) * base_color.rgb / 3.14159265;
    let direct = (diffuse + specular) * frame.main_light_color.rgb * n_dot_l;
    let ambient = base_color.rgb * frame.ambient_light_color.rgb;
    return vec4<f32>(ambient + direct, base_color.a);
}
)wgsl";

} // namespace ofg::render::shaders
