use std::collections::BTreeMap;

use terrain_core::{MeshData, TerrainChunkCoord};

use super::*;

#[test]
fn scenario_filter_parses_and_matches_expected_groups() {
    assert_eq!(ScenarioFilter::parse("all").unwrap(), ScenarioFilter::All);
    assert_eq!(ScenarioFilter::parse("boot").unwrap(), ScenarioFilter::Boot);
    assert_eq!(
        ScenarioFilter::parse("presets").unwrap(),
        ScenarioFilter::Presets
    );
    assert_eq!(
        ScenarioFilter::parse("seams").unwrap(),
        ScenarioFilter::Seams
    );
    assert_eq!(ScenarioFilter::parse("lods").unwrap(), ScenarioFilter::Lods);
    assert!(ScenarioFilter::parse("terrain").is_err());

    assert!(ScenarioFilter::All.matches(ScenarioFilter::Boot));
    assert!(ScenarioFilter::All.matches(ScenarioFilter::Presets));
    assert!(ScenarioFilter::All.matches(ScenarioFilter::Lods));
    assert!(ScenarioFilter::Boot.matches(ScenarioFilter::Boot));
    assert!(!ScenarioFilter::Boot.matches(ScenarioFilter::Seams));
}

#[test]
fn scenarios_cover_boot_preset_seam_and_lod_groups() {
    let scenarios = scenarios();

    assert_eq!(
        scenarios
            .iter()
            .filter(|scenario| ScenarioFilter::Boot.matches(scenario.group))
            .count(),
        1
    );
    assert_eq!(
        scenarios
            .iter()
            .filter(|scenario| ScenarioFilter::Presets.matches(scenario.group))
            .count(),
        4
    );
    assert_eq!(
        scenarios
            .iter()
            .filter(|scenario| ScenarioFilter::Seams.matches(scenario.group))
            .count(),
        3
    );
    assert_eq!(
        scenarios
            .iter()
            .filter(|scenario| ScenarioFilter::Lods.matches(scenario.group))
            .count(),
        3
    );

    let mut file_names = scenarios
        .iter()
        .map(|scenario| scenario.file_name)
        .collect::<Vec<_>>();
    file_names.sort_unstable();
    file_names.dedup();
    assert_eq!(file_names.len(), scenarios.len());
    assert!(scenarios
        .iter()
        .filter(|scenario| ScenarioFilter::Seams.matches(scenario.group))
        .all(|scenario| scenario.coverage.is_some()));
    assert!(scenarios
        .iter()
        .all(|scenario| scenario.file_name.ends_with(".png")));
    assert!(scenarios
        .iter()
        .any(|scenario| scenario.name == "running-stream-delta" && scenario.movement.is_some()));
    assert_eq!(
        scenarios
            .iter()
            .filter(|scenario| scenario.shadow_debug)
            .count(),
        1
    );
}

#[test]
fn builds_boot_scenario_terrain_with_debug_metadata() {
    let scenario = scenarios()
        .into_iter()
        .find(|scenario| scenario.group == ScenarioFilter::Boot)
        .expect("boot scenario should exist");

    let terrain = build_scenario_terrain(scenario).expect("boot scenario should build terrain");

    assert!(!terrain.meshes.is_empty());
    assert_eq!(terrain.debug.terrain_seed, scenario.seed);
    assert_eq!(terrain.debug.terrain_preset_code, scenario.preset);
    assert_eq!(terrain.debug.terrain_preset, "rollingHills");
    assert_eq!(terrain.debug.rendered_chunk_count, terrain.meshes.len());
    assert_eq!(terrain.debug.rendered_node_count, terrain.meshes.len());
    assert!(terrain.debug.loaded_chunk_count >= terrain.debug.rendered_chunk_count);
    assert!(terrain.debug.loaded_node_count >= terrain.debug.rendered_node_count);
    assert_eq!(terrain.debug.max_rendered_lod, 0);
    assert!(terrain.debug.vertex_count > 0);
    assert!(terrain.debug.index_count > 0);
    assert!(terrain
        .debug
        .rendered_node_keys
        .iter()
        .all(|key| key.starts_with("lod0:")));
    assert!(terrain.camera.eye.x.is_finite());
    assert!(terrain.camera.target.y.is_finite());
}

#[test]
fn multi_lod_scenario_terrain_reports_lod_counts() {
    let scenario = scenarios()
        .into_iter()
        .find(|scenario| scenario.name == "far-view-multi-lod")
        .expect("far-view scenario should exist");

    let terrain = build_scenario_terrain(scenario).expect("far-view scenario should build terrain");

    assert!(terrain.debug.rendered_chunk_count > 0);
    assert!(terrain.debug.rendered_node_count > terrain.debug.rendered_chunk_count);
    assert!(terrain.debug.max_rendered_lod >= 1);
    assert!(terrain.debug.rendered_lod_counts.len() >= 2);
    assert!(terrain
        .debug
        .rendered_node_keys
        .iter()
        .any(|key| key.starts_with("lod1:") || key.starts_with("lod2:")));
}

#[test]
fn seam_scenario_terrain_covers_both_sides_of_boundary() {
    let scenario = scenarios()
        .into_iter()
        .find(|scenario| scenario.name == "x-seam-grazing")
        .expect("x seam scenario should exist");

    let terrain = build_scenario_terrain(scenario).expect("seam scenario should build terrain");

    assert!(terrain
        .debug
        .rendered_chunk_keys
        .iter()
        .any(|key| key.starts_with("0,")));
    assert!(terrain
        .debug
        .rendered_chunk_keys
        .iter()
        .any(|key| key.starts_with("1,")));
}

#[test]
fn coverage_assertion_reports_missing_axis_or_corner_chunks() {
    let mut scenario = scenarios()[0];
    let meshes_by_coord = BTreeMap::<TerrainChunkCoord, MeshData>::new();

    scenario.coverage = None;
    assert!(assert_coverage(scenario, &meshes_by_coord).is_ok());

    scenario.coverage = Some(ScenarioCoverage::Axis {
        axis: ChunkAxis::X,
        low: 0,
        high: 1,
    });
    assert!(assert_coverage(scenario, &meshes_by_coord).is_err());

    scenario.coverage = Some(ScenarioCoverage::Corner {
        x_low: 0,
        x_high: 1,
        z_low: 0,
        z_high: 1,
    });
    assert!(assert_coverage(scenario, &meshes_by_coord).is_err());
}

#[test]
fn preset_names_fall_back_to_rolling_hills_for_unknown_codes() {
    assert_eq!(terrain_preset_name(0), "seed");
    assert_eq!(terrain_preset_name(1), "rollingHills");
    assert_eq!(terrain_preset_name(2), "mountainValley");
    assert_eq!(terrain_preset_name(3), "rockyHighland");
    assert_eq!(terrain_preset_name(999), "rollingHills");
}
