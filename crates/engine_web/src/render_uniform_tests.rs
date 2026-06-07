// Tests for renderer uniform packing that is shared by browser and smoke paths.

use crate::{
    build_shadow_cascades, build_shadow_uniform_values, RenderUniformError, RenderVec3,
    ENGINE_RENDER_SNAPSHOT_FLOATS, SHADOW_CASCADE_COUNT, SHADOW_UNIFORM_FLOATS,
    WORLD_MATRIX_FLOATS,
};

#[test]
fn shadow_uniforms_pack_four_cascade_matrices_and_splits() {
    let cascades = sample_shadow_cascades();
    let uniforms =
        build_shadow_uniform_values(&cascades, true, 0.0015, 0.02, 1.0 / 1024.0).unwrap();

    assert_eq!(uniforms.len(), SHADOW_UNIFORM_FLOATS);
    for index in 0..SHADOW_CASCADE_COUNT {
        let matrix_offset = index * WORLD_MATRIX_FLOATS;
        assert_eq!(
            &uniforms[matrix_offset..matrix_offset + WORLD_MATRIX_FLOATS],
            &cascades.cascades[index].light_view_projection
        );
        assert_close(uniforms[64 + index], cascades.split_depths[index]);
    }
    assert_close(uniforms[68], 1.0);
    assert_close(uniforms[69], 0.0015);
    assert_close(uniforms[70], 0.02);
    assert_close(uniforms[71], 1.0 / 1024.0);
    assert_eq!(&uniforms[72..76], &[0.0, 0.0, 0.0, 0.0]);
}

#[test]
fn shadow_uniforms_disable_cleanly_when_no_shadow_pass_runs() {
    let cascades = sample_shadow_cascades();
    let uniforms = build_shadow_uniform_values(&cascades, false, 0.0, 0.0, 0.0).unwrap();

    assert_close(uniforms[68], 0.0);
    assert_close(uniforms[69], 0.0);
    assert_close(uniforms[70], 0.0);
    assert_close(uniforms[71], 0.0);
    assert!(uniforms[0..64].iter().all(|value| value.is_finite()));
}

#[test]
fn shadow_uniforms_reject_invalid_options_and_cascades() {
    let cascades = sample_shadow_cascades();

    assert_eq!(
        build_shadow_uniform_values(&cascades, true, -0.001, 0.0, 1.0 / 1024.0),
        Err(RenderUniformError::InvalidShadowPacket)
    );
    assert_eq!(
        build_shadow_uniform_values(&cascades, true, 0.0, 0.0, 0.0),
        Err(RenderUniformError::InvalidShadowPacket)
    );

    let mut repeated_split = cascades;
    repeated_split.split_depths[1] = repeated_split.split_depths[0];
    assert_eq!(
        build_shadow_uniform_values(&repeated_split, true, 0.0, 0.0, 1.0 / 1024.0),
        Err(RenderUniformError::InvalidShadowPacket)
    );

    let mut invalid_matrix = cascades;
    invalid_matrix.cascades[0].light_view_projection[0] = f32::NAN;
    assert_eq!(
        build_shadow_uniform_values(&invalid_matrix, true, 0.0, 0.0, 1.0 / 1024.0),
        Err(RenderUniformError::InvalidShadowPacket)
    );
}

#[test]
fn render_uniform_errors_format_stable_browser_diagnostics() {
    assert_eq!(
        RenderUniformError::InvalidFramePacket.to_string(),
        "invalid Rust WebGPU frame packet"
    );
    assert_eq!(
        RenderUniformError::InvalidObjectPacket.to_string(),
        "invalid Rust WebGPU object packet"
    );
    assert_eq!(
        RenderUniformError::InvalidShadowPacket.to_string(),
        "invalid Rust WebGPU shadow packet"
    );
    assert_eq!(
        RenderUniformError::SingularWorldMatrix.to_string(),
        "singular Rust WebGPU world matrix"
    );
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

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
