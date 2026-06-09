// Deterministic terrain scenarios for the native Rust image smoke harness.

use std::collections::{BTreeMap, BTreeSet};

use engine_core::Vec3;
use engine_web::{BrowserTerrainStream, TERRAIN_VERTEX_FLOATS};
use terrain_core::{
    height_at, terrain_chunk_key, terrain_node_cell_size, terrain_node_key, terrain_node_parent,
    MeshData, TerrainChunkCoord, TerrainNodeKey, DEFAULT_TERRAIN_PRESET,
    TERRAIN_CHUNK_CELLS_PER_AXIS,
};

use super::error::{harness_error, HarnessResult};
use super::renderer::CameraSetup;
use super::report::{LodCountReport, ScenarioDebug};

const MIN_MULTI_KM_TERRAIN_SPAN_METERS: f64 = 4096.0;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScenarioFilter {
    All,
    Boot,
    Presets,
    Seams,
    Lods,
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
    pub stream_mode: ScenarioStreamMode,
    pub max_stream_ticks: usize,
    pub movement: Option<ScenarioMovement>,
}

#[derive(Clone, Copy)]
pub struct ScenarioMovement {
    pub step_count: usize,
    pub step_x: f32,
    pub step_z: f32,
    pub ticks_per_step: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScenarioStreamMode {
    Lod0,
    MultiLod,
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
            "lods" => Ok(Self::Lods),
            _ => Err(harness_error(format!(
                "Unknown --scenario '{value}'. Use all, boot, presets, seams, or lods."
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
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
            stream_mode: ScenarioStreamMode::Lod0,
            max_stream_ticks: 64,
            movement: None,
        },
        Scenario {
            name: "far-view-multi-lod",
            file_name: "far-view-multi-lod.png",
            group: ScenarioFilter::Lods,
            seed: 246,
            preset: 2,
            center_x: 0.0,
            center_z: 0.0,
            camera_offset: Vec3::new(220.0, 118.0, 260.0),
            target_height_offset: 18.0,
            coverage: None,
            shadow_debug: false,
            stream_mode: ScenarioStreamMode::MultiLod,
            max_stream_ticks: 1600,
            movement: None,
        },
        Scenario {
            name: "lod-boundary-oblique",
            file_name: "lod-boundary-oblique.png",
            group: ScenarioFilter::Lods,
            seed: 246,
            preset: 3,
            center_x: 64.0,
            center_z: 64.0,
            camera_offset: Vec3::new(150.0, 62.0, 34.0),
            target_height_offset: 8.0,
            coverage: None,
            shadow_debug: false,
            stream_mode: ScenarioStreamMode::MultiLod,
            max_stream_ticks: 1600,
            movement: None,
        },
        Scenario {
            name: "running-stream-delta",
            file_name: "running-stream-delta.png",
            group: ScenarioFilter::Lods,
            seed: 246,
            preset: 2,
            center_x: 0.0,
            center_z: 0.0,
            camera_offset: Vec3::new(150.0, 72.0, 170.0),
            target_height_offset: 10.0,
            coverage: None,
            shadow_debug: false,
            stream_mode: ScenarioStreamMode::MultiLod,
            max_stream_ticks: 2000,
            movement: Some(ScenarioMovement {
                step_count: 48,
                step_x: 4.0,
                step_z: 1.75,
                ticks_per_step: 2,
            }),
        },
    ]
}

/// Builds Rust terrain stream meshes for a scenario.
pub fn build_scenario_terrain(scenario: Scenario) -> HarnessResult<ScenarioTerrain> {
    let mut center = terrain_position(
        scenario.seed,
        scenario.preset,
        scenario.center_x,
        scenario.center_z,
        scenario.name,
    )?;

    let mut stream = match scenario.stream_mode {
        ScenarioStreamMode::Lod0 => BrowserTerrainStream::new_lod0(scenario.seed, scenario.preset),
        ScenarioStreamMode::MultiLod => BrowserTerrainStream::new(scenario.seed, scenario.preset),
    }
    .map_err(|error| harness_error(format!("Could not create terrain stream: {error:?}")))?;
    stream.reset_around(center);
    let mut meshes_by_node = BTreeMap::<TerrainNodeKey, MeshData>::new();

    settle_stream(scenario, &mut stream, center, &mut meshes_by_node)?;
    assert_visible_stream_cover(scenario, &stream, center)?;

    if let Some(movement) = scenario.movement {
        for step in 1..=movement.step_count {
            center = terrain_position(
                scenario.seed,
                scenario.preset,
                scenario.center_x + movement.step_x * step as f32,
                scenario.center_z + movement.step_z * step as f32,
                scenario.name,
            )?;
            for _ in 0..movement.ticks_per_step {
                apply_stream_update(&mut stream, center, &mut meshes_by_node);
                assert_visible_stream_cover(scenario, &stream, center)?;
            }
        }

        settle_stream(scenario, &mut stream, center, &mut meshes_by_node)?;
        assert_visible_stream_cover(scenario, &stream, center)?;
    }

    if meshes_by_node.is_empty() {
        return Err(harness_error(format!(
            "Scenario '{}' produced no renderable terrain meshes.",
            scenario.name
        )));
    }
    let meshes_by_coord = lod0_meshes_by_coord(&meshes_by_node);
    assert_coverage(scenario, &meshes_by_coord)?;

    let rendered_chunk_keys = meshes_by_coord
        .keys()
        .copied()
        .map(terrain_chunk_key)
        .collect::<Vec<_>>();
    let loaded_chunk_count = stream.loaded_chunk_keys().len();
    let rendered_node_keys = meshes_by_node
        .keys()
        .copied()
        .map(terrain_node_key)
        .collect::<Vec<_>>();
    let loaded_node_count = stream.loaded_node_keys().len();
    let rendered_lod_counts = rendered_lod_counts(&meshes_by_node);
    let max_rendered_lod = meshes_by_node.keys().map(|key| key.lod).max().unwrap_or(0);
    let status = stream.status();
    let vertex_count = meshes_by_node
        .values()
        .map(|mesh| mesh.vertices.len() / TERRAIN_VERTEX_FLOATS as usize)
        .sum();
    let index_count = meshes_by_node.values().map(|mesh| mesh.indices.len()).sum();
    let rendered_node_count = rendered_node_keys.len();
    let meshes = meshes_by_node.into_values().collect::<Vec<_>>();
    let target = Vec3::new(center.x, center.y + scenario.target_height_offset, center.z);
    let eye = Vec3::new(
        target.x + scenario.camera_offset.x,
        target.y + scenario.camera_offset.y,
        target.z + scenario.camera_offset.z,
    );

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
            rendered_node_count,
            loaded_node_count,
            stream_pending: status.pending,
            desired_render_node_count: status.desired_render_node_count,
            empty_node_count: status.empty_node_count,
            missing_node_count: status.missing_node_count,
            max_rendered_lod,
            visible_world_span_x_meters: status.visible_world_span_x_meters,
            visible_world_span_z_meters: status.visible_world_span_z_meters,
            rendered_lod_counts,
            vertex_count,
            index_count,
            rendered_chunk_keys,
            rendered_node_keys,
        },
    })
}

fn scenario_stream_ready(scenario: Scenario, stream: &BrowserTerrainStream) -> bool {
    let status = stream.status();
    match scenario.stream_mode {
        ScenarioStreamMode::Lod0 => !status.pending,
        ScenarioStreamMode::MultiLod => {
            !status.pending
                && status.rendered_chunk_count > 0
                && status.rendered_node_count > status.rendered_chunk_count
                && status.max_rendered_lod >= 3
                && status.visible_world_span_x_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS
                && status.visible_world_span_z_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS
        }
    }
}

fn terrain_position(
    seed: u32,
    preset: u32,
    x: f32,
    z: f32,
    scenario_name: &str,
) -> HarnessResult<Vec3> {
    let y = height_at(seed, preset, f64::from(x), f64::from(z)) as f32;
    if !y.is_finite() {
        return Err(harness_error(format!(
            "Scenario '{scenario_name}' produced a non-finite terrain center height.",
        )));
    }

    Ok(Vec3::new(x, y, z))
}

fn settle_stream(
    scenario: Scenario,
    stream: &mut BrowserTerrainStream,
    center: Vec3,
    meshes_by_node: &mut BTreeMap<TerrainNodeKey, MeshData>,
) -> HarnessResult<()> {
    for _ in 0..scenario.max_stream_ticks {
        apply_stream_update(stream, center, meshes_by_node);
        if scenario_stream_ready(scenario, stream) {
            return Ok(());
        }
    }

    Err(harness_error(format!(
        "Scenario '{}' terrain stream did not reach its readiness target.",
        scenario.name
    )))
}

fn apply_stream_update(
    stream: &mut BrowserTerrainStream,
    center: Vec3,
    meshes_by_node: &mut BTreeMap<TerrainNodeKey, MeshData>,
) {
    let update = stream.tick(center);
    for key in update.removed_nodes {
        meshes_by_node.remove(&key);
    }
    for mesh_update in update.upserted_meshes {
        meshes_by_node.insert(mesh_update.key, (*mesh_update.mesh).clone());
    }
}

fn assert_visible_stream_cover(
    scenario: Scenario,
    stream: &BrowserTerrainStream,
    position: Vec3,
) -> HarnessResult<()> {
    let visible_nodes = stream.render_nodes();
    if visible_nodes.is_empty() {
        return Err(harness_error(format!(
            "Scenario '{}' has no visible terrain nodes at position {:?}.",
            scenario.name, position
        )));
    }
    assert_no_visible_parent_child_overlap(scenario, &visible_nodes)?;
    if !visible_nodes
        .iter()
        .any(|key| node_covers_position(*key, position))
    {
        return Err(harness_error(format!(
            "Scenario '{}' has no visible terrain node covering position {:?}.",
            scenario.name, position
        )));
    }

    Ok(())
}

fn assert_no_visible_parent_child_overlap(
    scenario: Scenario,
    visible_nodes: &[TerrainNodeKey],
) -> HarnessResult<()> {
    let visible = visible_nodes.iter().copied().collect::<BTreeSet<_>>();
    for key in visible_nodes {
        let mut ancestor = terrain_node_parent(*key);
        while let Some(parent) = ancestor {
            if visible.contains(&parent) {
                return Err(harness_error(format!(
                    "Scenario '{}' rendered overlapping terrain parent {:?} and child {:?}.",
                    scenario.name, parent, key
                )));
            }
            ancestor = terrain_node_parent(parent);
        }
    }

    Ok(())
}

fn node_covers_position(key: TerrainNodeKey, position: Vec3) -> bool {
    let node_size = terrain_node_cell_size(1.0, key.lod) * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let min_x = key.coord.x as f64 * node_size;
    let min_y = key.coord.y as f64 * node_size;
    let min_z = key.coord.z as f64 * node_size;
    let x = f64::from(position.x);
    let y = f64::from(position.y);
    let z = f64::from(position.z);

    x >= min_x
        && x < min_x + node_size
        && y >= min_y
        && y < min_y + node_size
        && z >= min_z
        && z < min_z + node_size
}

fn lod0_meshes_by_coord(
    meshes_by_node: &BTreeMap<TerrainNodeKey, MeshData>,
) -> BTreeMap<TerrainChunkCoord, MeshData> {
    meshes_by_node
        .iter()
        .filter(|(key, _mesh)| key.lod == 0)
        .map(|(key, mesh)| (key.coord, mesh.clone()))
        .collect()
}

fn rendered_lod_counts(meshes_by_node: &BTreeMap<TerrainNodeKey, MeshData>) -> Vec<LodCountReport> {
    let mut counts = BTreeMap::<u8, usize>::new();
    for key in meshes_by_node.keys() {
        *counts.entry(key.lod).or_insert(0) += 1;
    }

    counts
        .into_iter()
        .map(|(lod, node_count)| LodCountReport { lod, node_count })
        .collect()
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
#[path = "scenarios_tests.rs"]
mod tests;
