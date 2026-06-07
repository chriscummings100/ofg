// Deterministic terrain scenarios for the native Rust image smoke harness.

use std::collections::BTreeMap;

use engine_core::Vec3;
use engine_web::{BrowserTerrainStream, TERRAIN_VERTEX_FLOATS};
use terrain_core::{
    height_at, terrain_chunk_key, MeshData, TerrainChunkCoord, DEFAULT_TERRAIN_PRESET,
};

use super::error::{harness_error, HarnessResult};
use super::renderer::CameraSetup;
use super::report::ScenarioDebug;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScenarioFilter {
    All,
    Boot,
    Presets,
    Seams,
}

#[derive(Clone, Copy)]
pub struct Scenario {
    pub name: &'static str,
    pub file_name: &'static str,
    pub group: ScenarioFilter,
    pub seed: u32,
    pub preset: u32,
    pub center_x: f32,
    pub center_z: f32,
    pub camera_offset: Vec3,
    pub target_height_offset: f32,
    pub coverage: Option<ScenarioCoverage>,
    pub shadow_debug: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScenarioCoverage {
    Axis {
        axis: ChunkAxis,
        low: i32,
        high: i32,
    },
    Corner {
        x_low: i32,
        x_high: i32,
        z_low: i32,
        z_high: i32,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ChunkAxis {
    X,
    Z,
}

pub struct ScenarioTerrain {
    pub meshes: Vec<MeshData>,
    pub camera: CameraSetup,
    pub debug: ScenarioDebug,
}

impl ScenarioFilter {
    /// Parses a scenario filter from CLI text.
    pub fn parse(value: &str) -> HarnessResult<Self> {
        match value {
            "all" => Ok(Self::All),
            "boot" => Ok(Self::Boot),
            "presets" => Ok(Self::Presets),
            "seams" => Ok(Self::Seams),
            _ => Err(harness_error(format!(
                "Unknown --scenario '{value}'. Use all, boot, presets, or seams."
            ))),
        }
    }

    /// Returns true when this filter should run a scenario group.
    pub fn matches(self, group: ScenarioFilter) -> bool {
        matches!(self, Self::All) || std::mem::discriminant(&self) == std::mem::discriminant(&group)
    }
}

/// Returns deterministic scenario definitions for Rust image smoke.
pub fn scenarios() -> Vec<Scenario> {
    vec![
        Scenario {
            name: "boot-frame",
            file_name: "boot-frame.png",
            group: ScenarioFilter::Boot,
            seed: 246,
            preset: DEFAULT_TERRAIN_PRESET,
            center_x: 0.0,
            center_z: 0.0,
            camera_offset: Vec3::new(48.0, 36.0, 62.0),
            target_height_offset: 4.0,
            coverage: None,
            shadow_debug: true,
        },
        Scenario {
            name: "preset-seed",
            file_name: "preset-seed.png",
            group: ScenarioFilter::Presets,
            seed: 246,
            preset: 0,
            center_x: 0.0,
            center_z: 0.0,
            camera_offset: Vec3::new(44.0, 34.0, 58.0),
            target_height_offset: 4.0,
            coverage: None,
            shadow_debug: false,
        },
        Scenario {
            name: "preset-rollingHills",
            file_name: "preset-rollingHills.png",
            group: ScenarioFilter::Presets,
            seed: 246,
            preset: 1,
            center_x: 64.0,
            center_z: -64.0,
            camera_offset: Vec3::new(48.0, 34.0, 58.0),
            target_height_offset: 5.0,
            coverage: None,
            shadow_debug: false,
        },
        Scenario {
            name: "preset-mountainValley",
            file_name: "preset-mountainValley.png",
            group: ScenarioFilter::Presets,
            seed: 246,
            preset: 2,
            center_x: 96.0,
            center_z: -32.0,
            camera_offset: Vec3::new(60.0, 52.0, 78.0),
            target_height_offset: 7.0,
            coverage: None,
            shadow_debug: false,
        },
        Scenario {
            name: "preset-rockyHighland",
            file_name: "preset-rockyHighland.png",
            group: ScenarioFilter::Presets,
            seed: 246,
            preset: 3,
            center_x: -64.0,
            center_z: 64.0,
            camera_offset: Vec3::new(58.0, 44.0, 62.0),
            target_height_offset: 6.0,
            coverage: None,
            shadow_debug: false,
        },
        Scenario {
            name: "x-seam-grazing",
            file_name: "x-seam-grazing.png",
            group: ScenarioFilter::Seams,
            seed: 246,
            preset: 3,
            center_x: 32.0,
            center_z: 0.0,
            camera_offset: Vec3::new(-24.0, 2.3, -18.0),
            target_height_offset: 1.1,
            coverage: Some(ScenarioCoverage::Axis {
                axis: ChunkAxis::X,
                low: 0,
                high: 1,
            }),
            shadow_debug: false,
        },
        Scenario {
            name: "z-seam-grazing",
            file_name: "z-seam-grazing.png",
            group: ScenarioFilter::Seams,
            seed: 246,
            preset: 3,
            center_x: 0.0,
            center_z: 32.0,
            camera_offset: Vec3::new(-18.0, 2.3, -24.0),
            target_height_offset: 1.1,
            coverage: Some(ScenarioCoverage::Axis {
                axis: ChunkAxis::Z,
                low: 0,
                high: 1,
            }),
            shadow_debug: false,
        },
        Scenario {
            name: "chunk-corner-oblique",
            file_name: "chunk-corner-oblique.png",
            group: ScenarioFilter::Seams,
            seed: 246,
            preset: 3,
            center_x: 32.0,
            center_z: 32.0,
            camera_offset: Vec3::new(-22.0, 2.0, -24.0),
            target_height_offset: 1.4,
            coverage: Some(ScenarioCoverage::Corner {
                x_low: 0,
                x_high: 1,
                z_low: 0,
                z_high: 1,
            }),
            shadow_debug: false,
        },
    ]
}

/// Builds Rust terrain stream meshes for a scenario.
pub fn build_scenario_terrain(scenario: Scenario) -> HarnessResult<ScenarioTerrain> {
    let center_height = height_at(
        scenario.seed,
        scenario.preset,
        f64::from(scenario.center_x),
        f64::from(scenario.center_z),
    ) as f32;
    if !center_height.is_finite() {
        return Err(harness_error(format!(
            "Scenario '{}' produced a non-finite terrain center height.",
            scenario.name
        )));
    }
    let center = Vec3::new(scenario.center_x, center_height, scenario.center_z);
    let target = Vec3::new(
        scenario.center_x,
        center_height + scenario.target_height_offset,
        scenario.center_z,
    );
    let eye = Vec3::new(
        target.x + scenario.camera_offset.x,
        target.y + scenario.camera_offset.y,
        target.z + scenario.camera_offset.z,
    );

    let mut stream = BrowserTerrainStream::new(scenario.seed, scenario.preset)
        .map_err(|error| harness_error(format!("Could not create terrain stream: {error:?}")))?;
    stream.reset_around(center);
    let mut meshes_by_coord = BTreeMap::<TerrainChunkCoord, MeshData>::new();
    for _ in 0..64 {
        let update = stream.tick(center);
        for coord in update.removed_coords {
            meshes_by_coord.remove(&coord);
        }
        for mesh_update in update.upserted_meshes {
            meshes_by_coord.insert(mesh_update.coord, mesh_update.mesh);
        }
        if !stream.status().pending {
            break;
        }
    }
    let status = stream.status();
    if status.pending {
        return Err(harness_error(format!(
            "Scenario '{}' terrain stream did not settle.",
            scenario.name
        )));
    }
    if meshes_by_coord.is_empty() {
        return Err(harness_error(format!(
            "Scenario '{}' produced no renderable terrain meshes.",
            scenario.name
        )));
    }
    assert_coverage(scenario, &meshes_by_coord)?;

    let rendered_chunk_keys = meshes_by_coord
        .keys()
        .copied()
        .map(terrain_chunk_key)
        .collect::<Vec<_>>();
    let loaded_chunk_count = stream.loaded_chunk_keys().len();
    let vertex_count = meshes_by_coord
        .values()
        .map(|mesh| mesh.vertices.len() / TERRAIN_VERTEX_FLOATS as usize)
        .sum();
    let index_count = meshes_by_coord
        .values()
        .map(|mesh| mesh.indices.len())
        .sum();
    let meshes = meshes_by_coord.into_values().collect::<Vec<_>>();

    Ok(ScenarioTerrain {
        meshes,
        camera: CameraSetup { eye, target },
        debug: ScenarioDebug {
            terrain_seed: scenario.seed,
            terrain_preset: terrain_preset_name(scenario.preset),
            terrain_preset_code: scenario.preset,
            center: [center.x, center.y, center.z],
            camera_eye: [eye.x, eye.y, eye.z],
            camera_target: [target.x, target.y, target.z],
            rendered_chunk_count: rendered_chunk_keys.len(),
            loaded_chunk_count,
            vertex_count,
            index_count,
            rendered_chunk_keys,
        },
    })
}

/// Validates seam scenarios render chunks on both sides of the target boundary.
fn assert_coverage(
    scenario: Scenario,
    meshes_by_coord: &BTreeMap<TerrainChunkCoord, MeshData>,
) -> HarnessResult<()> {
    let Some(coverage) = scenario.coverage else {
        return Ok(());
    };

    match coverage {
        ScenarioCoverage::Axis { axis, low, high } => {
            let has_low = meshes_by_coord
                .keys()
                .any(|coord| coord_axis(*coord, axis) == low);
            let has_high = meshes_by_coord
                .keys()
                .any(|coord| coord_axis(*coord, axis) == high);
            if !has_low || !has_high {
                return Err(harness_error(format!(
                    "Scenario '{}' did not render both sides of the seam.",
                    scenario.name
                )));
            }
        }
        ScenarioCoverage::Corner {
            x_low,
            x_high,
            z_low,
            z_high,
        } => {
            let has_low_x = meshes_by_coord.keys().any(|coord| coord.x == x_low);
            let has_high_x = meshes_by_coord.keys().any(|coord| coord.x == x_high);
            let has_low_z = meshes_by_coord.keys().any(|coord| coord.z == z_low);
            let has_high_z = meshes_by_coord.keys().any(|coord| coord.z == z_high);
            if !has_low_x || !has_high_x || !has_low_z || !has_high_z {
                return Err(harness_error(format!(
                    "Scenario '{}' did not render both sides of the chunk corner.",
                    scenario.name
                )));
            }
        }
    }

    Ok(())
}

/// Returns the requested horizontal coordinate axis.
fn coord_axis(coord: TerrainChunkCoord, axis: ChunkAxis) -> i32 {
    match axis {
        ChunkAxis::X => coord.x,
        ChunkAxis::Z => coord.z,
    }
}

/// Returns a stable display name for terrain preset codes.
fn terrain_preset_name(preset: u32) -> &'static str {
    match preset {
        0 => "seed",
        1 => "rollingHills",
        2 => "mountainValley",
        3 => "rockyHighland",
        _ => "rollingHills",
    }
}

#[cfg(test)]
mod tests {
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
        assert!(ScenarioFilter::parse("terrain").is_err());

        assert!(ScenarioFilter::All.matches(ScenarioFilter::Boot));
        assert!(ScenarioFilter::All.matches(ScenarioFilter::Presets));
        assert!(ScenarioFilter::Boot.matches(ScenarioFilter::Boot));
        assert!(!ScenarioFilter::Boot.matches(ScenarioFilter::Seams));
    }

    #[test]
    fn scenarios_cover_boot_preset_and_seam_groups() {
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
        assert!(terrain.debug.loaded_chunk_count >= terrain.debug.rendered_chunk_count);
        assert!(terrain.debug.vertex_count > 0);
        assert!(terrain.debug.index_count > 0);
        assert!(terrain.camera.eye.x.is_finite());
        assert!(terrain.camera.target.y.is_finite());
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
}
