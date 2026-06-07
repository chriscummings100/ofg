// Tests for render-space math, render packets, and CPU-side shadow cascades.

use crate::{
    aabb_from_vertex_positions, build_frame_packet_from_engine_snapshot, build_shadow_cascades,
    camera_frustum_corners_world, compute_cascade_splits, frustum_from_view_projection,
    frustum_intersects_aabb, look_at_mat4, multiply_mat4, orthographic_mat4, perspective_mat4,
    transform_aabb, transform_point, Aabb, RenderPacketError, RenderVec3,
    ENGINE_RENDER_SNAPSHOT_FLOATS, MODEL_VERTEX_FLOATS, SHADOW_CASCADE_COUNT, SHADOW_MAP_SIZE,
    SHADOW_MAX_DISTANCE, SHADOW_SPLIT_LAMBDA, TERRAIN_VERTEX_FLOATS, WORLD_MATRIX_FLOATS,
};

#[test]
fn render_packet_errors_format_stable_browser_diagnostics() {
    assert_eq!(
        RenderPacketError::InvalidEngineSnapshot.to_string(),
        "invalid Rust engine render snapshot"
    );
    assert_eq!(
        RenderPacketError::InvalidAspect.to_string(),
        "invalid Rust WebGPU frame aspect ratio"
    );
    assert_eq!(
        RenderPacketError::InvalidCamera.to_string(),
        "invalid Rust engine camera render packet"
    );
}

#[test]
fn render_packet_builder_uses_shared_matrix_helpers() {
    let snapshot = sample_engine_render_snapshot();
    let frame = build_frame_packet_from_engine_snapshot(&snapshot, 16.0 / 9.0).unwrap();
    let eye = RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]);
    let target = RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]);
    let projection = perspective_mat4(snapshot[8], 16.0 / 9.0, snapshot[9], snapshot[10]).unwrap();
    let view = look_at_mat4(eye, target, RenderVec3::UP).unwrap();
    let expected = multiply_mat4(&projection, &view);

    for index in 0..WORLD_MATRIX_FLOATS {
        assert_close(frame[index], expected[index]);
    }
}

#[test]
fn aabb_from_vertex_positions_reads_terrain_and_model_layouts() {
    let mut terrain_vertices = vec![0.0; TERRAIN_VERTEX_FLOATS as usize * 3];
    terrain_vertices[0..3].copy_from_slice(&[-2.0, 3.0, 4.0]);
    terrain_vertices[TERRAIN_VERTEX_FLOATS as usize..TERRAIN_VERTEX_FLOATS as usize + 3]
        .copy_from_slice(&[5.0, -1.0, 8.0]);
    terrain_vertices[TERRAIN_VERTEX_FLOATS as usize * 2..TERRAIN_VERTEX_FLOATS as usize * 2 + 3]
        .copy_from_slice(&[1.0, 7.0, -6.0]);

    let terrain_bounds =
        aabb_from_vertex_positions(&terrain_vertices, TERRAIN_VERTEX_FLOATS, 0).unwrap();

    assert_eq!(terrain_bounds.min, RenderVec3::new(-2.0, -1.0, -6.0));
    assert_eq!(terrain_bounds.max, RenderVec3::new(5.0, 7.0, 8.0));

    let mut model_vertices = vec![0.0; MODEL_VERTEX_FLOATS as usize * 2];
    model_vertices[0..3].copy_from_slice(&[-1.0, -2.0, -3.0]);
    model_vertices[MODEL_VERTEX_FLOATS as usize..MODEL_VERTEX_FLOATS as usize + 3]
        .copy_from_slice(&[4.0, 5.0, 6.0]);

    let model_bounds = aabb_from_vertex_positions(&model_vertices, MODEL_VERTEX_FLOATS, 0).unwrap();

    assert_eq!(model_bounds.min, RenderVec3::new(-1.0, -2.0, -3.0));
    assert_eq!(model_bounds.max, RenderVec3::new(4.0, 5.0, 6.0));
}

#[test]
fn transform_aabb_tracks_translation_and_scale() {
    let bounds = Aabb {
        min: RenderVec3::new(-1.0, -2.0, -3.0),
        max: RenderVec3::new(1.0, 2.0, 3.0),
    };
    let world = [
        2.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 0.0, 4.0, 0.0, 10.0, 20.0, 30.0, 1.0,
    ];

    let transformed = transform_aabb(bounds, &world);

    assert_eq!(transformed.min, RenderVec3::new(8.0, 14.0, 18.0));
    assert_eq!(transformed.max, RenderVec3::new(12.0, 26.0, 42.0));
}

#[test]
fn frustum_intersects_aabb_accepts_intersecting_bounds() {
    let frustum = sample_origin_frustum();
    let bounds = Aabb {
        min: RenderVec3::new(-0.5, -0.5, -2.5),
        max: RenderVec3::new(0.5, 0.5, -1.5),
    };

    assert!(frustum_intersects_aabb(frustum, bounds));
}

#[test]
fn frustum_intersects_aabb_rejects_fully_outside_bounds() {
    let frustum = sample_origin_frustum();
    let behind_camera = Aabb {
        min: RenderVec3::new(-0.5, -0.5, 1.0),
        max: RenderVec3::new(0.5, 0.5, 2.0),
    };
    let beyond_far_plane = Aabb {
        min: RenderVec3::new(-0.5, -0.5, -14.0),
        max: RenderVec3::new(0.5, 0.5, -12.0),
    };

    assert!(!frustum_intersects_aabb(frustum, behind_camera));
    assert!(!frustum_intersects_aabb(frustum, beyond_far_plane));
}

#[test]
fn render_vec3_arithmetic_supports_camera_basis() {
    let right = RenderVec3::new(1.0, 0.0, 0.0);
    let up = RenderVec3::new(0.0, 1.0, 0.0);
    let forward = RenderVec3::new(0.0, 0.0, 1.0);

    assert_eq!(RenderVec3::ZERO.add(right), right);
    assert_eq!(right.sub(RenderVec3::ZERO), right);
    assert_eq!(right.cross(up), forward);
    assert_close(right.dot(up), 0.0);
    assert_close(right.scale(3.0).length(), 3.0);
    assert_eq!(right.scale(3.0).normalize(), Some(right));
}

#[test]
fn orthographic_projection_maps_camera_depth_to_webgpu_clip_space() {
    let projection = orthographic_mat4(-2.0, 2.0, -4.0, 4.0, 1.0, 11.0).unwrap();
    let left_near = transform_point(&projection, RenderVec3::new(-2.0, -4.0, -1.0));
    let right_far = transform_point(&projection, RenderVec3::new(2.0, 4.0, -11.0));

    assert_close(left_near.x, -1.0);
    assert_close(left_near.y, -1.0);
    assert_close(left_near.z, 0.0);
    assert_close(right_far.x, 1.0);
    assert_close(right_far.y, 1.0);
    assert_close(right_far.z, 1.0);
}

#[test]
fn render_math_rejects_invalid_inputs() {
    assert_eq!(
        aabb_from_vertex_positions(&[], TERRAIN_VERTEX_FLOATS, 0),
        None
    );
    assert_eq!(aabb_from_vertex_positions(&[0.0, 1.0], 0, 0), None);
    assert_eq!(
        aabb_from_vertex_positions(&[0.0, 1.0, 2.0, 3.0], 3, 0),
        None
    );
    assert_eq!(
        aabb_from_vertex_positions(&[0.0, f32::NAN, 2.0], 3, 0),
        None
    );

    assert_eq!(perspective_mat4(0.0, 1.0, 0.1, 10.0), None);
    assert_eq!(
        perspective_mat4(70.0_f32.to_radians(), 0.0, 0.1, 10.0),
        None
    );
    assert_eq!(
        perspective_mat4(70.0_f32.to_radians(), 1.0, 10.0, 1.0),
        None
    );
    assert_eq!(
        look_at_mat4(RenderVec3::ZERO, RenderVec3::ZERO, RenderVec3::UP),
        None
    );
    assert_eq!(
        look_at_mat4(
            RenderVec3::ZERO,
            RenderVec3::new(0.0, -1.0, 0.0),
            RenderVec3::UP,
        ),
        None
    );
    assert_eq!(orthographic_mat4(1.0, 1.0, -1.0, 1.0, 0.1, 10.0), None);
    assert_eq!(orthographic_mat4(-1.0, 1.0, -1.0, 1.0, 10.0, 0.1), None);
    assert_eq!(
        frustum_from_view_projection(&[0.0; WORLD_MATRIX_FLOATS]),
        None
    );
}

#[test]
fn cascade_splits_are_monotonic_and_clamped_to_shadow_distance() {
    let splits =
        compute_cascade_splits(0.05, 500.0, SHADOW_MAX_DISTANCE, SHADOW_SPLIT_LAMBDA).unwrap();
    let mut previous = 0.05;
    for split in splits {
        assert!(split > previous, "split {split} should exceed {previous}");
        previous = split;
    }
    assert_close(splits[SHADOW_CASCADE_COUNT - 1], SHADOW_MAX_DISTANCE);

    let camera_far_splits =
        compute_cascade_splits(0.1, 80.0, SHADOW_MAX_DISTANCE, SHADOW_SPLIT_LAMBDA).unwrap();
    assert_close(camera_far_splits[SHADOW_CASCADE_COUNT - 1], 80.0);

    assert_eq!(
        compute_cascade_splits(0.0, 500.0, SHADOW_MAX_DISTANCE, SHADOW_SPLIT_LAMBDA),
        None
    );
    assert_eq!(
        compute_cascade_splits(0.1, 500.0, 0.05, SHADOW_SPLIT_LAMBDA),
        None
    );
    assert_eq!(
        compute_cascade_splits(0.1, 500.0, SHADOW_MAX_DISTANCE, -0.1),
        None
    );
}

#[test]
fn camera_frustum_corners_world_places_near_and_far_planes() {
    let corners = camera_frustum_corners_world(
        RenderVec3::ZERO,
        RenderVec3::new(0.0, 0.0, -1.0),
        90.0_f32.to_radians(),
        1.0,
        1.0,
        2.0,
    )
    .unwrap();

    assert_eq!(corners[0], RenderVec3::new(-1.0, -1.0, -1.0));
    assert_eq!(corners[1], RenderVec3::new(1.0, -1.0, -1.0));
    assert_eq!(corners[2], RenderVec3::new(-1.0, 1.0, -1.0));
    assert_eq!(corners[3], RenderVec3::new(1.0, 1.0, -1.0));
    assert_eq!(corners[4], RenderVec3::new(-2.0, -2.0, -2.0));
    assert_eq!(corners[7], RenderVec3::new(2.0, 2.0, -2.0));
}

#[test]
fn cascade_corners_fit_inside_light_projection() {
    let snapshot = sample_engine_render_snapshot();
    let cascades = sample_shadow_cascades();
    let mut near_depth = snapshot[9];

    for index in 0..SHADOW_CASCADE_COUNT {
        let cascade = cascades.cascades[index];
        assert_close(cascade.near_depth, near_depth);
        assert_close(cascade.far_depth, cascades.split_depths[index]);
        let corners = camera_frustum_corners_world(
            RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]),
            RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]),
            snapshot[8],
            16.0 / 9.0,
            near_depth,
            cascades.split_depths[index],
        )
        .unwrap();

        for corner in corners {
            assert_point_inside_aabb(corner, cascade.world_bounds);
            let clip = transform_point(&cascade.light_view_projection, corner);
            assert!(
                clip.x >= -1.0001 && clip.x <= 1.0001,
                "cascade {index} x clip coordinate {clip:?} should fit"
            );
            assert!(
                clip.y >= -1.0001 && clip.y <= 1.0001,
                "cascade {index} y clip coordinate {clip:?} should fit"
            );
            assert!(
                clip.z >= -0.0001 && clip.z <= 1.0001,
                "cascade {index} z clip coordinate {clip:?} should fit"
            );
        }

        near_depth = cascades.split_depths[index];
    }
}

#[test]
fn cascade_projection_snaps_to_shadow_texels() {
    let cascades = sample_shadow_cascades();

    for cascade in cascades.cascades {
        let width = 2.0 / cascade.light_projection[0];
        let height = 2.0 / cascade.light_projection[5];
        let center_x = -cascade.light_projection[12] / cascade.light_projection[0];
        let center_y = -cascade.light_projection[13] / cascade.light_projection[5];
        let left = center_x - width * 0.5;
        let bottom = center_y - height * 0.5;
        let texel_width = width / SHADOW_MAP_SIZE as f32;
        let texel_height = height / SHADOW_MAP_SIZE as f32;

        assert_snapped_to_texels(left, texel_width);
        assert_snapped_to_texels(bottom, texel_height);
    }
}

#[test]
fn cascade_builder_rejects_invalid_camera_or_light() {
    let eye = RenderVec3::new(0.0, 2.0, 0.0);
    let target = RenderVec3::new(0.0, 2.0, -1.0);
    let light = RenderVec3::new(0.89, 0.25, 0.38);

    assert_eq!(
        build_shadow_cascades(
            eye,
            eye,
            70.0_f32.to_radians(),
            16.0 / 9.0,
            0.05,
            500.0,
            light
        ),
        None
    );
    assert_eq!(
        build_shadow_cascades(eye, target, 0.0, 16.0 / 9.0, 0.05, 500.0, light),
        None
    );
    assert_eq!(
        build_shadow_cascades(eye, target, 70.0_f32.to_radians(), 0.0, 0.05, 500.0, light),
        None
    );
    assert_eq!(
        build_shadow_cascades(
            eye,
            target,
            70.0_f32.to_radians(),
            16.0 / 9.0,
            500.0,
            0.05,
            light,
        ),
        None
    );
    assert_eq!(
        build_shadow_cascades(
            eye,
            target,
            70.0_f32.to_radians(),
            16.0 / 9.0,
            0.05,
            500.0,
            RenderVec3::ZERO,
        ),
        None
    );
}

#[test]
fn cascade_matrices_remain_finite_for_sun_near_up_vector() {
    let snapshot = sample_engine_render_snapshot();
    let cascades = build_shadow_cascades(
        RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]),
        RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]),
        snapshot[8],
        16.0 / 9.0,
        snapshot[9],
        snapshot[10],
        RenderVec3::UP,
    )
    .unwrap();

    for cascade in cascades.cascades {
        assert!(cascade.far_depth > cascade.near_depth);
        assert_matrix_is_finite(&cascade.light_view_projection);
        assert_matrix_is_finite(&cascade.light_view);
        assert_matrix_is_finite(&cascade.light_projection);
    }
}

fn sample_engine_render_snapshot() -> [f32; ENGINE_RENDER_SNAPSHOT_FLOATS] {
    [
        1.0,
        2.0,
        3.0,
        1.0,
        2.0,
        2.0,
        0.0,
        0.0,
        70.0_f32.to_radians(),
        0.05,
        500.0,
        0.89,
        0.25,
        0.38,
        1.0,
        0.96,
        0.88,
        1.25,
        0.4,
    ]
}

fn sample_origin_frustum() -> crate::Frustum {
    let projection = perspective_mat4(70.0_f32.to_radians(), 16.0 / 9.0, 0.1, 10.0).unwrap();
    let view = look_at_mat4(
        RenderVec3::new(0.0, 0.0, 0.0),
        RenderVec3::new(0.0, 0.0, -1.0),
        RenderVec3::UP,
    )
    .unwrap();
    let view_projection = multiply_mat4(&projection, &view);
    frustum_from_view_projection(&view_projection).unwrap()
}

fn sample_shadow_cascades() -> crate::ShadowCascadeSet {
    let snapshot = sample_engine_render_snapshot();
    build_shadow_cascades(
        RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]),
        RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]),
        snapshot[8],
        16.0 / 9.0,
        snapshot[9],
        snapshot[10],
        RenderVec3::new(snapshot[11], snapshot[12], snapshot[13]),
    )
    .unwrap()
}

fn assert_point_inside_aabb(point: RenderVec3, bounds: Aabb) {
    assert!(
        point.x >= bounds.min.x - 0.0001 && point.x <= bounds.max.x + 0.0001,
        "x coordinate {point:?} should be inside {bounds:?}"
    );
    assert!(
        point.y >= bounds.min.y - 0.0001 && point.y <= bounds.max.y + 0.0001,
        "y coordinate {point:?} should be inside {bounds:?}"
    );
    assert!(
        point.z >= bounds.min.z - 0.0001 && point.z <= bounds.max.z + 0.0001,
        "z coordinate {point:?} should be inside {bounds:?}"
    );
}

fn assert_snapped_to_texels(value: f32, texel_size: f32) {
    let texel_coordinate = value / texel_size;
    assert!(
        (texel_coordinate - texel_coordinate.round()).abs() <= 0.001,
        "{value} should land on a {texel_size} texel grid"
    );
}

fn assert_matrix_is_finite(matrix: &[f32; WORLD_MATRIX_FLOATS]) {
    assert!(matrix.iter().all(|value| value.is_finite()));
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
