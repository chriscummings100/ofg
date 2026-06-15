// Tests for vertical terrain band range math and conservative column bounds.

use super::*;

#[test]
fn finite_world_range_validation_rejects_invalid_bounds() {
    assert_eq!(TerrainWorldYRange::new(-1.0, 1.0).unwrap().min_y, -1.0);
    assert!(TerrainWorldYRange::new(f64::NAN, 1.0).is_none());
    assert!(TerrainWorldYRange::new(-1.0, f64::INFINITY).is_none());
    assert!(TerrainWorldYRange::new(2.0, 1.0).is_none());
}

#[test]
fn node_range_validation_rejects_inverted_ranges() {
    assert_eq!(TerrainNodeYRange::new(-2, 3).unwrap().len(), 6);
    assert!(TerrainNodeYRange::new(3, -2).is_none());
}

#[test]
fn world_to_node_conversion_includes_nodes_touched_by_boundaries() {
    let world_range = TerrainWorldYRange::new(0.0, 32.0).unwrap();

    let node_range = terrain_world_y_range_to_node_y_range(world_range, 0, 1.0).unwrap();

    assert_eq!(node_range, TerrainNodeYRange { min_y: 0, max_y: 1 });
}

#[test]
fn world_to_node_conversion_handles_negative_coordinates() {
    let world_range = TerrainWorldYRange::new(-33.0, -1.0).unwrap();

    let node_range = terrain_world_y_range_to_node_y_range(world_range, 0, 1.0).unwrap();

    assert_eq!(
        node_range,
        TerrainNodeYRange {
            min_y: -2,
            max_y: -1,
        }
    );
}

#[test]
fn world_range_contains_finite_heights_inside_bounds() {
    let range = TerrainWorldYRange::new(-2.0, 4.0).unwrap();

    assert!(range.contains(-2.0));
    assert!(range.contains(4.0));
    assert!(!range.contains(f64::NAN));
    assert_eq!(range.span_m(), 6.0);
}

#[test]
fn world_to_node_conversion_scales_by_lod() {
    let world_range = TerrainWorldYRange::new(64.0, 128.0).unwrap();

    let node_range = terrain_world_y_range_to_node_y_range(world_range, 1, 1.0).unwrap();

    assert_eq!(node_range, TerrainNodeYRange { min_y: 1, max_y: 2 });
}

#[test]
fn intersection_returns_empty_when_ranges_do_not_overlap() {
    let left = TerrainNodeYRange::new(-3, -1).unwrap();
    let right = TerrainNodeYRange::new(0, 2).unwrap();

    assert_eq!(left.intersect(right), None);
    assert_eq!(
        TerrainWorldYRange::new(-3.0, 2.0)
            .unwrap()
            .intersect(TerrainWorldYRange::new(1.0, 4.0).unwrap())
            .unwrap(),
        TerrainWorldYRange {
            min_y: 1.0,
            max_y: 2.0,
        }
    );
}

#[test]
fn player_vertical_windows_can_be_asymmetric() {
    let window = TerrainLodVerticalWindow::new(2, 5).unwrap();

    assert_eq!(
        window.node_range_around(10),
        TerrainNodeYRange {
            min_y: 8,
            max_y: 15,
        }
    );
}

#[test]
fn expansion_saturates_safely_and_rejects_negative_margins() {
    let range = TerrainNodeYRange::new(i32::MIN + 1, i32::MAX - 1).unwrap();

    assert_eq!(
        range.expanded(4, 4).unwrap(),
        TerrainNodeYRange {
            min_y: i32::MIN,
            max_y: i32::MAX,
        }
    );
    assert!(range.expanded(-1, 0).is_none());
    assert!(TerrainWorldYRange::new(2.0, 4.0)
        .unwrap()
        .expanded(-0.5, 0.0)
        .is_none());
}

#[test]
fn node_range_iterator_visits_inclusive_coordinates_once() {
    let range = TerrainNodeYRange::new(-1, 1).unwrap();

    assert_eq!(range.iter().collect::<Vec<_>>(), vec![-1, 0, 1]);
}

#[test]
fn column_key_round_trips_to_full_node_key() {
    let key = TerrainNodeKey {
        lod: 2,
        coord: TerrainChunkCoord { x: -3, y: 7, z: 4 },
    };

    let column = TerrainNodeColumnKey::from_node(key);

    assert_eq!(column.with_y(-9).lod, 2);
    assert_eq!(
        column.with_y(-9).coord,
        TerrainChunkCoord { x: -3, y: -9, z: 4 }
    );
}

#[test]
fn node_world_spans_match_chunk_coordinate_semantics() {
    assert_eq!(terrain_node_world_span_y(0, 1.0), Some(32.0));
    assert_eq!(
        terrain_node_world_y_span(1, -2, 1.0).unwrap(),
        TerrainWorldYRange {
            min_y: -128.0,
            max_y: -64.0,
        }
    );
    assert_eq!(
        terrain_node_column_xz_bounds(
            TerrainNodeColumnKey {
                lod: 1,
                x: -1,
                z: 2,
            },
            1.0,
        )
        .unwrap(),
        (-64.0, 128.0, 0.0, 192.0)
    );
}

#[test]
fn vertical_bounds_estimator_returns_compact_range_for_flat_shape() {
    let descriptor = flat_descriptor(12.0);
    let config = TerrainVerticalBoundsConfig {
        surface_padding_below_m: 1.0,
        surface_padding_above_m: 2.0,
        ..TerrainVerticalBoundsConfig::default()
    };
    let expected_surface = height_at_with_shape(7, descriptor.shape, 0.0, 0.0);

    let range = estimate_terrain_column_world_y_range(
        7,
        descriptor,
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        config,
    )
    .unwrap();

    assert!((range.min_y - (expected_surface - 1.0)).abs() < 0.001);
    assert!((range.max_y - (expected_surface + 2.0)).abs() < 0.001);
}

#[test]
fn vertical_bounds_estimator_expands_for_high_relief_shape() {
    let flat_range = estimate_terrain_column_world_y_range(
        11,
        flat_descriptor(0.0),
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();
    let mut high_relief = flat_descriptor(0.0);
    high_relief.shape.height_scale = 120.0;

    let high_relief_range = estimate_terrain_column_world_y_range(
        11,
        high_relief,
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();

    assert!(high_relief_range.span_m() > flat_range.span_m() + 200.0);
}

#[test]
fn vertical_bounds_estimator_contains_representative_sample_heights() {
    let seed = 0x0F6;
    let descriptor = crate::terrain_variant_for_preset(crate::DEFAULT_TERRAIN_PRESET);
    let column = TerrainNodeColumnKey {
        lod: 1,
        x: -1,
        z: 2,
    };
    let (min_x, min_z, max_x, max_z) = terrain_node_column_xz_bounds(column, 1.0).unwrap();

    let range = estimate_terrain_column_world_y_range(
        seed,
        descriptor,
        column,
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();

    for (x, z) in [
        (min_x, min_z),
        ((min_x + max_x) * 0.5, (min_z + max_z) * 0.5),
        (max_x, max_z),
    ] {
        let height = height_at_with_shape(seed, descriptor.shape, x, z);
        assert!(range.contains(height));
    }
}

#[test]
fn vertical_bounds_estimator_applies_future_feature_depth_padding() {
    let descriptor = flat_descriptor(24.0);
    let baseline = estimate_terrain_column_world_y_range(
        3,
        descriptor,
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();
    let deeper = estimate_terrain_column_world_y_range(
        3,
        descriptor,
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        TerrainVerticalBoundsConfig {
            feature_padding_below_m: 20.0,
            ..TerrainVerticalBoundsConfig::default()
        },
    )
    .unwrap();

    assert!((baseline.min_y - deeper.min_y - 20.0).abs() < 0.001);
    assert!((baseline.max_y - deeper.max_y).abs() < 0.001);
}

#[test]
fn vertical_bounds_estimator_rejects_invalid_variants() {
    let mut descriptor = flat_descriptor(0.0);
    descriptor.shape.detail_amplitude = f64::NAN;

    let result = estimate_terrain_column_world_y_range(
        1,
        descriptor,
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        1.0,
        TerrainVerticalBoundsConfig::default(),
    );

    assert!(matches!(
        result,
        Err(TerrainVerticalBoundsError::InvalidTerrainVariant(
            TerrainVariantValidationError::InvalidDetailAmplitude
        ))
    ));
}

#[test]
fn vertical_bounds_estimator_rejects_invalid_base_cell_size() {
    let result = estimate_terrain_column_world_y_range(
        1,
        flat_descriptor(0.0),
        TerrainNodeColumnKey { lod: 0, x: 0, z: 0 },
        0.0,
        TerrainVerticalBoundsConfig::default(),
    );

    assert_eq!(result, Err(TerrainVerticalBoundsError::InvalidBaseCellSize));
}

#[test]
fn vertical_bounds_estimator_is_deterministic_for_same_inputs() {
    let descriptor = crate::terrain_variant_for_preset(3);
    let column = TerrainNodeColumnKey {
        lod: 2,
        x: 4,
        z: -3,
    };

    let first = estimate_terrain_column_world_y_range(
        0xABCD,
        descriptor,
        column,
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();
    let second = estimate_terrain_column_world_y_range(
        0xABCD,
        descriptor,
        column,
        1.0,
        TerrainVerticalBoundsConfig::default(),
    )
    .unwrap();

    assert_eq!(first, second);
}

#[test]
fn vertical_bounds_config_rejects_invalid_padding_and_sample_counts() {
    assert_eq!(
        TerrainVerticalBoundsConfig {
            surface_padding_below_m: f64::NAN,
            ..TerrainVerticalBoundsConfig::default()
        }
        .validate(),
        Err(TerrainVerticalBoundsError::InvalidPadding)
    );
    assert_eq!(
        TerrainVerticalBoundsConfig {
            sample_steps_per_axis: 1,
            ..TerrainVerticalBoundsConfig::default()
        }
        .validate(),
        Err(TerrainVerticalBoundsError::InvalidSampleGrid)
    );
}

fn flat_descriptor(base_height: f64) -> TerrainVariantDescriptor {
    let mut descriptor = crate::terrain_variant_for_preset(crate::DEFAULT_TERRAIN_PRESET);
    descriptor.shape.base_height = base_height;
    descriptor.shape.height_scale = 0.0;
    descriptor.shape.ridge_height_scale = 0.0;
    descriptor.shape.warp.amplitude = 0.0;
    descriptor.shape.cellular_height_scale = 0.0;
    descriptor.shape.detail_amplitude = 0.0;
    descriptor
}
