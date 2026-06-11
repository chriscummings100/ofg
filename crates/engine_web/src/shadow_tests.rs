// Tests for shadow cascade range, culling, and low-sun bounding math.

use crate::{
    build_shadow_cascades_with_max_distance, clamp_shadow_light_direction, compute_cascade_splits,
    shadow_caster_intersects_cascade, shadow_strength_for_sun_elevation, shadow_sun_mode_direction,
    Aabb, RenderVec3, ShadowSunMode, SHADOW_CASCADE_COUNT, SHADOW_DISABLED_SUN_ELEVATION,
    SHADOW_FULL_STRENGTH_SUN_ELEVATION, SHADOW_MIN_EFFECTIVE_SUN_ELEVATION,
};

#[test]
fn shadow_splits_respect_shorter_production_distance() {
    let splits = compute_cascade_splits(0.05, 500.0, 100.0, 0.65).unwrap();

    assert_eq!(splits.len(), SHADOW_CASCADE_COUNT);
    assert_close(splits[SHADOW_CASCADE_COUNT - 1], 100.0);
    for window in splits.windows(2) {
        assert!(window[0] < window[1]);
    }
}

#[test]
fn overhead_sun_culls_casters_outside_the_light_clip_volume() {
    let cascades = sample_cascades(RenderVec3::UP);
    let first = cascades.cascades[0];

    assert!(shadow_caster_intersects_cascade(
        first,
        aabb(
            RenderVec3::new(-0.5, 9.0, -3.0),
            RenderVec3::new(0.5, 11.0, -2.0)
        )
    ));
    assert!(!shadow_caster_intersects_cascade(
        first,
        aabb(
            RenderVec3::new(500.0, 9.0, -3.0),
            RenderVec3::new(501.0, 11.0, -2.0)
        )
    ));
}

#[test]
fn angled_sun_includes_near_casters_but_rejects_unbounded_far_casters() {
    let angled = shadow_sun_mode_direction(ShadowSunMode::Angled, RenderVec3::UP).unwrap();
    let cascades = sample_cascades(angled);
    let first = cascades.cascades[0];

    let near_to_sun = angled.scale(24.0);
    let far_to_sun = angled.scale(240.0);
    assert!(shadow_caster_intersects_cascade(
        first,
        aabb(
            RenderVec3::new(-0.5, 9.0, -3.0).add(near_to_sun),
            RenderVec3::new(0.5, 11.0, -2.0).add(near_to_sun)
        )
    ));
    assert!(!shadow_caster_intersects_cascade(
        first,
        aabb(
            RenderVec3::new(-0.5, 9.0, -3.0).add(far_to_sun),
            RenderVec3::new(0.5, 11.0, -2.0).add(far_to_sun)
        )
    ));
}

#[test]
fn low_sun_strength_fades_to_zero_and_clamps_cascade_direction() {
    let low = shadow_sun_mode_direction(ShadowSunMode::Low, RenderVec3::UP).unwrap();
    let clamped = clamp_shadow_light_direction(low).unwrap();

    assert!(low.y < SHADOW_DISABLED_SUN_ELEVATION);
    assert_close(shadow_strength_for_sun_elevation(low.y), 0.0);
    assert!(clamped.y >= SHADOW_MIN_EFFECTIVE_SUN_ELEVATION - 0.00001);
    assert_close(
        shadow_strength_for_sun_elevation(SHADOW_FULL_STRENGTH_SUN_ELEVATION),
        1.0,
    );
}

#[test]
fn invalid_caster_bounds_do_not_intersect_shadow_cascades() {
    let cascades = sample_cascades(RenderVec3::UP);

    assert!(!shadow_caster_intersects_cascade(
        cascades.cascades[0],
        aabb(
            RenderVec3::new(1.0, 1.0, 1.0),
            RenderVec3::new(0.0, 0.0, 0.0)
        )
    ));
    assert!(!shadow_caster_intersects_cascade(
        cascades.cascades[0],
        aabb(
            RenderVec3::new(f32::NAN, 0.0, 0.0),
            RenderVec3::new(1.0, 1.0, 1.0)
        )
    ));
}

fn sample_cascades(light_direction: RenderVec3) -> crate::ShadowCascadeSet {
    build_shadow_cascades_with_max_distance(
        RenderVec3::new(0.0, 10.0, 0.0),
        RenderVec3::new(0.0, 10.0, -1.0),
        70.0_f32.to_radians(),
        16.0 / 9.0,
        0.05,
        500.0,
        100.0,
        light_direction,
    )
    .unwrap()
}

fn aabb(min: RenderVec3, max: RenderVec3) -> Aabb {
    Aabb { min, max }
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
