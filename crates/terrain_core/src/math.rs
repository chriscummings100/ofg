use crate::*;

#[derive(Clone, Copy, Debug)]
pub(crate) struct Vec3 {
    pub(crate) x: f64,
    pub(crate) y: f64,
    pub(crate) z: f64,
}

pub(crate) fn lerp_vec3(a: Vec3, b: Vec3, t: f64) -> Vec3 {
    Vec3 {
        x: a.x + (b.x - a.x) * t,
        y: a.y + (b.y - a.y) * t,
        z: a.z + (b.z - a.z) * t,
    }
}

pub(crate) fn normalize_vec3(value: Vec3) -> Vec3 {
    let length = (value.x * value.x + value.y * value.y + value.z * value.z).sqrt();
    if length <= f64::EPSILON {
        return Vec3 {
            x: 0.0,
            y: 0.0,
            z: 0.0,
        };
    }

    Vec3 {
        x: value.x / length,
        y: value.y / length,
        z: value.z / length,
    }
}

pub(crate) fn clamp_vec3_to_bounds(position: Vec3, bounds: TerrainChunkBounds) -> Vec3 {
    Vec3 {
        x: clamp(position.x, bounds.min.x, bounds.max.x),
        y: clamp(position.y, bounds.min.y, bounds.max.y),
        z: clamp(position.z, bounds.min.z, bounds.max.z),
    }
}

pub(crate) fn clamp(value: f64, minimum: f64, maximum: f64) -> f64 {
    value.max(minimum).min(maximum)
}

pub(crate) fn smoothstep(edge0: f64, edge1: f64, value: f64) -> f64 {
    let t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}
