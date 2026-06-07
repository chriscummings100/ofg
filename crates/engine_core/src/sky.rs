// Rust-owned sky cycle and render parameters for the browser renderer.
// This module keeps time-of-day lighting deterministic and independent of
// browser TypeScript, while WGSL remains responsible for per-pixel sky color.

use crate::math::Vec3;
use crate::render_packet::RenderLightPacket;

pub const SKY_RENDER_PACKET_FLOAT_COUNT: usize = 12;

const DAY_LENGTH_SECONDS: f32 = 240.0;
const INITIAL_DAY_PHASE: f32 = 0.16;
const SUN_HORIZONTAL_X: f32 = 0.919_145;
const SUN_HORIZONTAL_Z: f32 = 0.393_919;
const DEFAULT_TURBIDITY: f32 = 2.25;
const DEFAULT_CLOUD_COVERAGE: f32 = 0.34;
const DEFAULT_CLOUD_SPEED: f32 = 0.018;
const DEFAULT_CLOUD_SCALE: f32 = 1.35;
const DEFAULT_CLOUD_SOFTNESS: f32 = 0.18;
const DEFAULT_CLOUD_SHADOW: f32 = 0.42;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SkyRenderPacket {
    pub elapsed_seconds: f32,
    pub day_phase: f32,
    pub sun_elevation: f32,
    pub star_intensity: f32,
    pub turbidity: f32,
    pub cloud_coverage: f32,
    pub cloud_speed: f32,
    pub cloud_scale: f32,
    pub cloud_softness: f32,
    pub cloud_shadow: f32,
    pub moon_intensity: f32,
    pub night_blend: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SkyRenderState {
    pub main_light: RenderLightPacket,
    pub sky: SkyRenderPacket,
}

impl SkyRenderPacket {
    pub const fn default_day() -> Self {
        Self {
            elapsed_seconds: 0.0,
            day_phase: INITIAL_DAY_PHASE,
            sun_elevation: 0.84,
            star_intensity: 0.0,
            turbidity: DEFAULT_TURBIDITY,
            cloud_coverage: DEFAULT_CLOUD_COVERAGE,
            cloud_speed: DEFAULT_CLOUD_SPEED,
            cloud_scale: DEFAULT_CLOUD_SCALE,
            cloud_softness: DEFAULT_CLOUD_SOFTNESS,
            cloud_shadow: DEFAULT_CLOUD_SHADOW,
            moon_intensity: 0.0,
            night_blend: 0.0,
        }
    }

    pub fn write_f32s(self, out: &mut [f32; SKY_RENDER_PACKET_FLOAT_COUNT]) {
        out[0] = self.elapsed_seconds;
        out[1] = self.day_phase;
        out[2] = self.sun_elevation;
        out[3] = self.star_intensity;
        out[4] = self.turbidity;
        out[5] = self.cloud_coverage;
        out[6] = self.cloud_speed;
        out[7] = self.cloud_scale;
        out[8] = self.cloud_softness;
        out[9] = self.cloud_shadow;
        out[10] = self.moon_intensity;
        out[11] = self.night_blend;
    }
}

pub fn sky_state_at_elapsed_seconds(elapsed_seconds: f64) -> SkyRenderState {
    let elapsed = if elapsed_seconds.is_finite() {
        elapsed_seconds as f32
    } else {
        0.0
    };
    let day_phase = (INITIAL_DAY_PHASE + elapsed / DAY_LENGTH_SECONDS).rem_euclid(1.0);
    sky_state_for_day_phase(day_phase, elapsed)
}

pub fn sky_state_for_day_phase(day_phase: f32, elapsed_seconds: f32) -> SkyRenderState {
    let phase = day_phase.rem_euclid(1.0);
    let angle = phase * std::f32::consts::TAU;
    let sun_elevation = angle.sin();
    let horizontal = angle.cos();
    let direction = Vec3::new(
        SUN_HORIZONTAL_X * horizontal,
        sun_elevation,
        SUN_HORIZONTAL_Z * horizontal,
    )
    .normalize();

    let daylight = smoothstep(-0.05, 0.16, sun_elevation);
    let low_sun = (1.0 - smoothstep(0.0, 0.32, sun_elevation)) * daylight;
    let night_blend = 1.0 - smoothstep(-0.18, 0.08, sun_elevation);
    let star_intensity = 1.0 - smoothstep(-0.28, -0.04, sun_elevation);
    let day_color = Vec3::new(1.0, 0.96, 0.88);
    let warm_color = Vec3::new(1.0, 0.36, 0.14);
    let color = mix_vec3(day_color, warm_color, low_sun * 1.05);
    let intensity = daylight * (1.0 - low_sun * 0.25);
    let ambient = 0.055 + daylight * 0.285 + low_sun * 0.035;

    SkyRenderState {
        main_light: RenderLightPacket {
            direction,
            color,
            intensity,
            ambient,
        },
        sky: SkyRenderPacket {
            elapsed_seconds,
            day_phase: phase,
            sun_elevation,
            star_intensity: star_intensity.clamp(0.0, 1.0),
            turbidity: DEFAULT_TURBIDITY,
            cloud_coverage: DEFAULT_CLOUD_COVERAGE,
            cloud_speed: DEFAULT_CLOUD_SPEED,
            cloud_scale: DEFAULT_CLOUD_SCALE,
            cloud_softness: DEFAULT_CLOUD_SOFTNESS,
            cloud_shadow: DEFAULT_CLOUD_SHADOW,
            moon_intensity: night_blend * 0.74,
            night_blend: night_blend.clamp(0.0, 1.0),
        },
    }
}

fn smoothstep(edge0: f32, edge1: f32, value: f32) -> f32 {
    let t = ((value - edge0) / (edge1 - edge0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn mix_vec3(a: Vec3, b: Vec3, amount: f32) -> Vec3 {
    let t = amount.clamp(0.0, 1.0);
    a.scale(1.0 - t).add(b.scale(t))
}
