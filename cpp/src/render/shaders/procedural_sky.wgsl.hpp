// WGSL source for OFG's first procedural sky pass.
#pragma once

namespace ofg::render::shaders {

constexpr char procedural_sky_wgsl[] = R"wgsl(
struct SkyUniforms {
    camera_right: vec4<f32>,
    camera_up: vec4<f32>,
    camera_forward: vec4<f32>,
    sun_direction: vec4<f32>,
    sun_color: vec4<f32>,
    moon_direction: vec4<f32>,
    sky_factors: vec4<f32>,
    weather: vec4<f32>,
    cloud_motion: vec4<f32>,
    cloud_shape: vec4<f32>,
    unused0: vec4<f32>,
    unused1: vec4<f32>,
};

@group(0) @binding(0) var<uniform> sky: SkyUniforms;

const SKY_FBM_OCTAVES: i32 = 4;
const CLEAR_CLOUD_COVERAGE_EARLY_OUT: f32 = 0.01;
const CLOUD_BELOW_HORIZON_EARLY_OUT: f32 = -0.04;
const STORM_DETAIL_EARLY_OUT: f32 = 0.02;
const STAR_NIGHT_EARLY_OUT: f32 = 0.001;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );

    let ndc = positions[vertex_index];
    var output: VertexOutput;
    output.position = vec4<f32>(ndc, 1.0, 1.0);
    output.ndc = ndc;
    return output;
}

fn saturate(value: f32) -> f32 {
    return clamp(value, 0.0, 1.0);
}

fn hash21(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7)) + sky.cloud_shape.z) * 43758.5453123);
}

fn hash31(p: vec3<f32>) -> f32 {
    return fract(sin(dot(p, vec3<f32>(127.1, 311.7, 74.7)) + sky.cloud_shape.z) * 43758.5453123);
}

fn value_noise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (vec2<f32>(3.0) - 2.0 * f);
    let a = hash21(i);
    let b = hash21(i + vec2<f32>(1.0, 0.0));
    let c = hash21(i + vec2<f32>(0.0, 1.0));
    let d = hash21(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(p: vec2<f32>) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    for (var octave = 0; octave < SKY_FBM_OCTAVES; octave = octave + 1) {
        value += value_noise(p * frequency) * amplitude;
        frequency *= 2.13;
        amplitude *= 0.5;
    }
    return value;
}

fn clear_sky_radiance(ray: vec3<f32>, sun_dir: vec3<f32>) -> vec3<f32> {
    let day = saturate(sky.sky_factors.x);
    let twilight = saturate(sky.sky_factors.y);
    let haze = saturate(sky.sky_factors.z);
    let storm = saturate(sky.weather.y);

    let height = saturate(ray.y * 0.5 + 0.5);
    let horizon = pow(1.0 - saturate(abs(ray.y)), 2.6);
    let sun_horizon = pow(saturate(1.0 - abs(sun_dir.y)), 1.5);

    let night_horizon = vec3<f32>(0.015, 0.020, 0.040);
    let night_zenith = vec3<f32>(0.002, 0.004, 0.014);
    let day_horizon = mix(vec3<f32>(0.78, 0.92, 1.10), vec3<f32>(0.95, 0.98, 1.08), haze);
    let day_zenith = vec3<f32>(0.22, 0.46, 1.28);

    let night_sky = mix(night_horizon, night_zenith, pow(height, 0.65));
    let day_sky = mix(day_horizon, day_zenith, pow(height, 0.72));
    var color = mix(night_sky, day_sky, day);

    let sunset_color = vec3<f32>(1.55, 0.48, 0.18) * twilight * horizon * sun_horizon;
    color += sunset_color;

    let storm_dimming = mix(1.0, 0.55, storm);
    return color * storm_dimming;
}

fn clouded_radiance(ray: vec3<f32>, base_color: vec3<f32>) -> vec3<f32> {
    let coverage = saturate(sky.weather.x);
    let storm = saturate(sky.weather.y);
    let opacity = saturate(sky.weather.z);
    let precipitation = saturate(sky.weather.w);
    if (coverage <= CLEAR_CLOUD_COVERAGE_EARLY_OUT || ray.y <= CLOUD_BELOW_HORIZON_EARLY_OUT) {
        return base_color;
    }

    let wind = sky.cloud_motion.xy;
    let wind_length = max(length(wind), 0.0001);
    let wind_dir = wind / wind_length;
    let wind_offset = wind_dir * sky.sky_factors.w * sky.cloud_motion.z * 0.004;
    let authored_scale = clamp(sky.cloud_motion.w * 1400.0, 0.45, 3.5);
    let projection = ray.xz / max(ray.y + 0.28, 0.08);
    let cloud_uv = projection * authored_scale + wind_offset;
    let broad = fbm(cloud_uv);
    var detail = 0.0;
    if (storm > STORM_DETAIL_EARLY_OUT || coverage > 0.18) {
        detail = fbm(cloud_uv * 2.7 + vec2<f32>(19.1, 7.3));
    }
    let shaped = broad + detail * mix(0.14, 0.28, sky.cloud_shape.y);
    let threshold = mix(0.83, 0.24, coverage);
    let softness = mix(0.26, 0.06, sky.cloud_shape.y);
    let horizon_fade = smoothstep(-0.02, 0.20, ray.y) * (1.0 - smoothstep(0.92, 1.0, ray.y));
    let cloud_mask = smoothstep(threshold, threshold + softness, shaped) * opacity * horizon_fade;

    let day = saturate(sky.sky_factors.x);
    let twilight = saturate(sky.sky_factors.y);
    let lit_cloud = vec3<f32>(1.10, 1.14, 1.12) * (0.20 + day * 0.86 + twilight * 0.25);
    let storm_cloud = vec3<f32>(0.12, 0.14, 0.17) * (0.85 - precipitation * 0.18);
    let cloud_color = mix(lit_cloud, storm_cloud, storm);
    return mix(base_color, cloud_color, saturate(cloud_mask));
}

fn sun_radiance(ray: vec3<f32>, sun_dir: vec3<f32>) -> vec3<f32> {
    let day = saturate(sky.sky_factors.x);
    let sun_alignment = dot(ray, sun_dir);
    let disc = smoothstep(0.99976, 0.99994, sun_alignment);
    let core = smoothstep(0.99990, 0.999985, sun_alignment);
    let halo = exp((sun_alignment - 1.0) * 150.0) * saturate(sun_alignment);
    let horizon_warmth = saturate(1.0 - abs(sun_dir.y));
    let warm_sun = mix(sky.sun_color.rgb, vec3<f32>(1.0, 0.62, 0.32), horizon_warmth * 0.45);
    let core_sun = mix(warm_sun, vec3<f32>(1.0, 0.82, 0.52), 0.22 + horizon_warmth * 0.20);
    return core_sun * sky.sun_color.a * day * (disc * 18.0 + core * 10.0 + halo * 0.08);
}

fn moon_radiance(ray: vec3<f32>) -> vec3<f32> {
    let day = saturate(sky.sky_factors.x);
    let twilight = saturate(sky.sky_factors.y);
    let night = saturate(1.0 - day - twilight * 0.65);
    let moon_dir = normalize(sky.moon_direction.xyz);
    let alignment = dot(ray, moon_dir);
    let disc = smoothstep(0.99942, 0.99978, alignment);
    let halo = exp((alignment - 1.0) * 58.0) * saturate(alignment);
    let phase = saturate(sky.moon_direction.w);
    let phase_light = 0.22 + phase * 0.78;
    let moon_color = vec3<f32>(0.58, 0.66, 0.88);
    return moon_color * night * (disc * phase_light * 1.35 + halo * 0.10);
}

fn star_radiance(ray: vec3<f32>) -> vec3<f32> {
    let day = saturate(sky.sky_factors.x);
    let twilight = saturate(sky.sky_factors.y);
    let night = saturate(1.0 - day - twilight * 0.75);
    if (night <= STAR_NIGHT_EARLY_OUT || ray.y <= 0.0) {
        return vec3<f32>(0.0);
    }

    let p = normalize(ray) * 180.0;
    let cell = floor(p);
    let local = fract(p) - vec3<f32>(0.5);
    let star_hash = hash31(cell);
    let star_gate = smoothstep(0.9968, 0.9997, star_hash);
    let star_shape = 1.0 - smoothstep(0.02, 0.42, length(local));
    let horizon_fade = smoothstep(0.02, 0.24, ray.y);
    let brightness = star_gate * star_shape * horizon_fade * night;
    let color_shift = hash31(cell + vec3<f32>(4.7, 1.3, 9.2));
    let star_color = mix(vec3<f32>(0.65, 0.74, 1.0), vec3<f32>(1.0, 0.86, 0.64), color_shift);
    return star_color * brightness * 2.4;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let ray = normalize(
        sky.camera_right.xyz * (input.ndc.x * sky.camera_right.w) +
        sky.camera_up.xyz * (input.ndc.y * sky.camera_up.w) +
        sky.camera_forward.xyz
    );
    let sun_dir = normalize(sky.sun_direction.xyz);
    var color = clear_sky_radiance(ray, sun_dir);
    color = clouded_radiance(ray, color);
    color += sun_radiance(ray, sun_dir);
    color += moon_radiance(ray);
    color += star_radiance(ray);
    return vec4<f32>(max(color, vec3<f32>(0.0)), 1.0);
}
)wgsl";

} // namespace ofg::render::shaders
