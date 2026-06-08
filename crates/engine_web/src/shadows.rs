// CPU-side cascaded shadow map math.
// This module builds deterministic camera cascade intervals and directional-light
// view-projection matrices without allocating WebGPU resources.

use crate::config::{
    SHADOW_CASCADE_COUNT, SHADOW_CASTER_MARGIN, SHADOW_MAP_SIZE, SHADOW_MAX_DISTANCE,
    SHADOW_SPLIT_LAMBDA,
};
use crate::render_math::{
    frustum_from_view_projection, frustum_intersects_aabb, look_at_mat4, multiply_mat4,
    orthographic_mat4, transform_point, Aabb, RenderVec3, MATRIX_FLOATS,
};

const SHADOW_EXTENT_PADDING: f32 = 1.05;
const MIN_SHADOW_NEAR_DISTANCE: f32 = 0.01;
pub const SHADOW_FULL_STRENGTH_SUN_ELEVATION: f32 = 0.22;
pub const SHADOW_DISABLED_SUN_ELEVATION: f32 = 0.08;
pub const SHADOW_MIN_EFFECTIVE_SUN_ELEVATION: f32 = 0.18;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ShadowSunMode {
    #[default]
    Production,
    Overhead,
    Angled,
    Low,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ShadowCascade {
    pub near_depth: f32,
    pub far_depth: f32,
    pub light_view_projection: [f32; MATRIX_FLOATS],
    pub light_view: [f32; MATRIX_FLOATS],
    pub light_projection: [f32; MATRIX_FLOATS],
    pub world_bounds: Aabb,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ShadowCascadeSet {
    pub cascades: [ShadowCascade; SHADOW_CASCADE_COUNT],
    pub split_depths: [f32; SHADOW_CASCADE_COUNT],
}

#[derive(Clone, Copy)]
struct CameraBasis {
    forward: RenderVec3,
    right: RenderVec3,
    up: RenderVec3,
}

/// Computes practical logarithmic/linear CSM split far depths.
pub fn compute_cascade_splits(
    near: f32,
    far: f32,
    max_shadow_distance: f32,
    lambda: f32,
) -> Option<[f32; SHADOW_CASCADE_COUNT]> {
    if !near.is_finite()
        || !far.is_finite()
        || !max_shadow_distance.is_finite()
        || !lambda.is_finite()
        || near <= 0.0
        || far <= near
        || max_shadow_distance <= near
        || !(0.0..=1.0).contains(&lambda)
    {
        return None;
    }

    let shadow_far = far.min(max_shadow_distance);
    if shadow_far <= near {
        return None;
    }

    let mut splits = [0.0; SHADOW_CASCADE_COUNT];
    for index in 0..SHADOW_CASCADE_COUNT {
        let progress = (index + 1) as f32 / SHADOW_CASCADE_COUNT as f32;
        let logarithmic = near * (shadow_far / near).powf(progress);
        let linear = near + (shadow_far - near) * progress;
        let split = lambda * logarithmic + (1.0 - lambda) * linear;
        if !split.is_finite() {
            return None;
        }
        splits[index] = split;
    }
    splits[SHADOW_CASCADE_COUNT - 1] = shadow_far;

    let mut previous = near;
    for split in splits {
        if split <= previous {
            return None;
        }
        previous = split;
    }

    Some(splits)
}

/// Returns the eight world-space corners for a camera frustum slice.
pub fn camera_frustum_corners_world(
    eye: RenderVec3,
    target: RenderVec3,
    fov_y_radians: f32,
    aspect: f32,
    near: f32,
    far: f32,
) -> Option<[RenderVec3; 8]> {
    if !fov_y_radians.is_finite()
        || !aspect.is_finite()
        || !near.is_finite()
        || !far.is_finite()
        || fov_y_radians <= 0.0
        || aspect <= 0.0
        || near <= 0.0
        || far <= near
    {
        return None;
    }

    let tangent = (fov_y_radians * 0.5).tan();
    if !tangent.is_finite() || tangent <= 0.0 {
        return None;
    }

    let basis = camera_basis(eye, target)?;
    let near_center = eye.add(basis.forward.scale(near));
    let far_center = eye.add(basis.forward.scale(far));
    let near_half_height = tangent * near;
    let near_half_width = near_half_height * aspect;
    let far_half_height = tangent * far;
    let far_half_width = far_half_height * aspect;

    Some([
        frustum_corner(
            near_center,
            basis.right,
            basis.up,
            -near_half_width,
            -near_half_height,
        ),
        frustum_corner(
            near_center,
            basis.right,
            basis.up,
            near_half_width,
            -near_half_height,
        ),
        frustum_corner(
            near_center,
            basis.right,
            basis.up,
            -near_half_width,
            near_half_height,
        ),
        frustum_corner(
            near_center,
            basis.right,
            basis.up,
            near_half_width,
            near_half_height,
        ),
        frustum_corner(
            far_center,
            basis.right,
            basis.up,
            -far_half_width,
            -far_half_height,
        ),
        frustum_corner(
            far_center,
            basis.right,
            basis.up,
            far_half_width,
            -far_half_height,
        ),
        frustum_corner(
            far_center,
            basis.right,
            basis.up,
            -far_half_width,
            far_half_height,
        ),
        frustum_corner(
            far_center,
            basis.right,
            basis.up,
            far_half_width,
            far_half_height,
        ),
    ])
}

/// Builds stable directional-light matrices for all configured cascades.
pub fn build_shadow_cascades(
    eye: RenderVec3,
    target: RenderVec3,
    fov_y_radians: f32,
    aspect: f32,
    camera_near: f32,
    camera_far: f32,
    light_direction: RenderVec3,
) -> Option<ShadowCascadeSet> {
    build_shadow_cascades_with_max_distance(
        eye,
        target,
        fov_y_radians,
        aspect,
        camera_near,
        camera_far,
        SHADOW_MAX_DISTANCE,
        light_direction,
    )
}

/// Builds stable directional-light matrices with an explicit shadow receiver range.
pub fn build_shadow_cascades_with_max_distance(
    eye: RenderVec3,
    target: RenderVec3,
    fov_y_radians: f32,
    aspect: f32,
    camera_near: f32,
    camera_far: f32,
    max_shadow_distance: f32,
    light_direction: RenderVec3,
) -> Option<ShadowCascadeSet> {
    let light_direction = light_direction.normalize()?;
    let splits = compute_cascade_splits(
        camera_near,
        camera_far,
        max_shadow_distance,
        SHADOW_SPLIT_LAMBDA,
    )?;
    let mut cascades = [empty_cascade(); SHADOW_CASCADE_COUNT];
    let mut cascade_near = camera_near;

    for index in 0..SHADOW_CASCADE_COUNT {
        let cascade_far = splits[index];
        let corners = camera_frustum_corners_world(
            eye,
            target,
            fov_y_radians,
            aspect,
            cascade_near,
            cascade_far,
        )?;
        cascades[index] =
            build_shadow_cascade(cascade_near, cascade_far, corners, light_direction)?;
        cascade_near = cascade_far;
    }

    Some(ShadowCascadeSet {
        cascades,
        split_depths: splits,
    })
}

/// Returns the deterministic sun direction used by a shadow diagnostic mode.
pub fn shadow_sun_mode_direction(mode: ShadowSunMode, production: RenderVec3) -> Option<RenderVec3> {
    match mode {
        ShadowSunMode::Production => production.normalize(),
        ShadowSunMode::Overhead => Some(RenderVec3::UP),
        ShadowSunMode::Angled => RenderVec3::new(0.62, 0.62, 0.48).normalize(),
        ShadowSunMode::Low => RenderVec3::new(0.996, 0.05, 0.06).normalize(),
    }
}

/// Returns the 0..1 strength applied to sampled shadows for a sun elevation.
pub fn shadow_strength_for_sun_elevation(sun_elevation: f32) -> f32 {
    if !sun_elevation.is_finite() {
        return 0.0;
    }

    smoothstep(
        SHADOW_DISABLED_SUN_ELEVATION,
        SHADOW_FULL_STRENGTH_SUN_ELEVATION,
        sun_elevation,
    )
}

/// Clamps the cascade-building direction so near-horizon shadows stay bounded.
pub fn clamp_shadow_light_direction(direction: RenderVec3) -> Option<RenderVec3> {
    let normalized = direction.normalize()?;
    if normalized.y >= SHADOW_MIN_EFFECTIVE_SUN_ELEVATION {
        return Some(normalized);
    }

    let horizontal = RenderVec3::new(normalized.x, 0.0, normalized.z);
    let horizontal_direction = horizontal
        .normalize()
        .unwrap_or(RenderVec3::new(1.0, 0.0, 0.0));
    let horizontal_scale = (1.0
        - SHADOW_MIN_EFFECTIVE_SUN_ELEVATION * SHADOW_MIN_EFFECTIVE_SUN_ELEVATION)
        .sqrt();
    RenderVec3::new(
        horizontal_direction.x * horizontal_scale,
        SHADOW_MIN_EFFECTIVE_SUN_ELEVATION,
        horizontal_direction.z * horizontal_scale,
    )
    .normalize()
}

/// Returns true when a caster AABB intersects the cascade light clip volume.
pub fn shadow_caster_intersects_cascade(cascade: ShadowCascade, caster_bounds: Aabb) -> bool {
    if !aabb_is_finite(caster_bounds) {
        return false;
    }
    let Some(light_frustum) = frustum_from_view_projection(&cascade.light_view_projection) else {
        return false;
    };

    frustum_intersects_aabb(light_frustum, caster_bounds)
}

fn build_shadow_cascade(
    near_depth: f32,
    far_depth: f32,
    corners: [RenderVec3; 8],
    light_direction: RenderVec3,
) -> Option<ShadowCascade> {
    let center = point_average(&corners)?;
    let mut radius = 0.0_f32;
    for corner in corners {
        radius = radius.max(corner.sub(center).length());
    }
    if !radius.is_finite() || radius <= f32::EPSILON {
        return None;
    }

    let half_extent = radius * SHADOW_EXTENT_PADDING;
    let light_eye = center.add(light_direction.scale(half_extent + SHADOW_CASTER_MARGIN));
    let light_view = look_at_mat4(light_eye, center, light_up_vector(light_direction))?;
    let center_light = transform_point(&light_view, center);
    let texel_size = (half_extent * 2.0) / SHADOW_MAP_SIZE as f32;
    if !texel_size.is_finite() || texel_size <= 0.0 {
        return None;
    }

    let left = snap_down(center_light.x - half_extent, texel_size);
    let bottom = snap_down(center_light.y - half_extent, texel_size);
    let right = left + half_extent * 2.0;
    let top = bottom + half_extent * 2.0;

    let mut min_z = f32::INFINITY;
    let mut max_z = f32::NEG_INFINITY;
    for corner in corners {
        let light_corner = transform_point(&light_view, corner);
        min_z = min_z.min(light_corner.z);
        max_z = max_z.max(light_corner.z);
    }
    if !min_z.is_finite() || !max_z.is_finite() || max_z < min_z {
        return None;
    }

    let near_distance = (-max_z - SHADOW_CASTER_MARGIN).max(MIN_SHADOW_NEAR_DISTANCE);
    let far_distance = (-min_z + SHADOW_CASTER_MARGIN).max(near_distance + 1.0);
    let light_projection =
        orthographic_mat4(left, right, bottom, top, near_distance, far_distance)?;
    let light_view_projection = multiply_mat4(&light_projection, &light_view);
    let world_bounds = aabb_from_points(&corners)?;

    if !matrix_is_finite(&light_view_projection)
        || !matrix_is_finite(&light_view)
        || !matrix_is_finite(&light_projection)
    {
        return None;
    }

    Some(ShadowCascade {
        near_depth,
        far_depth,
        light_view_projection,
        light_view,
        light_projection,
        world_bounds,
    })
}

fn camera_basis(eye: RenderVec3, target: RenderVec3) -> Option<CameraBasis> {
    if !eye.is_finite() || !target.is_finite() {
        return None;
    }

    let forward = target.sub(eye).normalize()?;
    let fallback_up = if forward.dot(RenderVec3::UP).abs() > 0.95 {
        RenderVec3::new(0.0, 0.0, 1.0)
    } else {
        RenderVec3::UP
    };
    let right = forward.cross(fallback_up).normalize()?;
    let up = right.cross(forward).normalize()?;

    Some(CameraBasis { forward, right, up })
}

fn frustum_corner(
    center: RenderVec3,
    right: RenderVec3,
    up: RenderVec3,
    right_distance: f32,
    up_distance: f32,
) -> RenderVec3 {
    center
        .add(right.scale(right_distance))
        .add(up.scale(up_distance))
}

fn point_average(points: &[RenderVec3]) -> Option<RenderVec3> {
    if points.is_empty() {
        return None;
    }

    let mut sum = RenderVec3::ZERO;
    for point in points {
        if !point.is_finite() {
            return None;
        }
        sum = sum.add(*point);
    }

    Some(sum.scale(1.0 / points.len() as f32))
}

fn aabb_from_points(points: &[RenderVec3]) -> Option<Aabb> {
    if points.is_empty() {
        return None;
    }

    let mut min = RenderVec3::new(f32::INFINITY, f32::INFINITY, f32::INFINITY);
    let mut max = RenderVec3::new(f32::NEG_INFINITY, f32::NEG_INFINITY, f32::NEG_INFINITY);
    for point in points {
        if !point.is_finite() {
            return None;
        }

        min.x = min.x.min(point.x);
        min.y = min.y.min(point.y);
        min.z = min.z.min(point.z);
        max.x = max.x.max(point.x);
        max.y = max.y.max(point.y);
        max.z = max.z.max(point.z);
    }

    Some(Aabb { min, max })
}

fn light_up_vector(light_direction_to_sun: RenderVec3) -> RenderVec3 {
    if light_direction_to_sun.dot(RenderVec3::UP).abs() > 0.95 {
        RenderVec3::new(0.0, 0.0, 1.0)
    } else {
        RenderVec3::UP
    }
}

fn snap_down(value: f32, step: f32) -> f32 {
    (value / step).floor() * step
}

fn matrix_is_finite(matrix: &[f32; MATRIX_FLOATS]) -> bool {
    matrix.iter().all(|value| value.is_finite())
}

fn aabb_is_finite(aabb: Aabb) -> bool {
    aabb.min.is_finite()
        && aabb.max.is_finite()
        && aabb.min.x <= aabb.max.x
        && aabb.min.y <= aabb.max.y
        && aabb.min.z <= aabb.max.z
}

fn smoothstep(edge0: f32, edge1: f32, value: f32) -> f32 {
    let t = ((value - edge0) / (edge1 - edge0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

const fn empty_cascade() -> ShadowCascade {
    ShadowCascade {
        near_depth: 0.0,
        far_depth: 0.0,
        light_view_projection: [0.0; MATRIX_FLOATS],
        light_view: [0.0; MATRIX_FLOATS],
        light_projection: [0.0; MATRIX_FLOATS],
        world_bounds: Aabb {
            min: RenderVec3::ZERO,
            max: RenderVec3::ZERO,
        },
    }
}
