// Multi-LOD terrain benchmark probes used by the terrain benchmark report.

use std::time::Instant;

use engine_core::Vec3;
use engine_web::{BrowserTerrainStream, TERRAIN_VERTEX_FLOATS};
use serde::Serialize;
use terrain_core::benchmark::reset_density_store;
use terrain_core::{
    build_node_mesh, build_parent_lod_transition_edge_mesh, height_at, terrain_node_cell_size,
    TerrainChunkCoord, TerrainNodeKey, TerrainSurfaceIndex, TerrainTransitionFace,
    TerrainTransitionMeshConfig, TerrainTransitionMeshInput, TerrainVerticalQuery,
};

const BASE_CELL_SIZE: f64 = 1.0;
const STREAM_PROBE_TICKS: usize = 1600;
const MIN_REAL_SCALE_SPAN_METERS: f64 = 7_000.0;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MultiLodBenchmarkReport {
    pub stream_ticks: usize,
    pub loaded_node_count: usize,
    pub rendered_node_count: usize,
    pub rendered_chunk_count: usize,
    pub max_rendered_lod: u8,
    pub visible_world_span_x_meters: f64,
    pub visible_world_span_z_meters: f64,
    pub transition_face_count: usize,
    pub transition_mesh_count: usize,
    pub transition_vertex_float_count: usize,
    pub transition_index_count: usize,
    pub lod_counts: Vec<LodStreamCountReport>,
    pub mesh_builds_by_lod: Vec<LodMeshBuildReport>,
    pub transition_mesh_builds: TransitionMeshBuildSummaryReport,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LodStreamCountReport {
    pub lod: u8,
    pub desired_node_count: usize,
    pub min_desired_node_y: Option<i32>,
    pub max_desired_node_y: Option<i32>,
    pub density_ready_node_count: usize,
    pub rendered_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LodMeshBuildReport {
    pub lod: u8,
    pub duration_ms: f64,
    pub vertex_count: usize,
    pub index_count: usize,
    pub surface_index_ms: f64,
    pub surface_triangle_count: usize,
    pub surface_bin_reference_count: usize,
    pub surface_max_bin_occupancy: usize,
    pub surface_query_sample_count: usize,
    pub surface_query_hit_count: usize,
    pub surface_query_mean_ms: f64,
    pub surface_query_p95_ms: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TransitionMeshBuildSummaryReport {
    pub attempted_count: usize,
    pub build_count: usize,
    pub total_vertex_count: usize,
    pub total_index_count: usize,
    pub mean_ms: f64,
    pub median_ms: f64,
    pub p95_ms: f64,
    pub builds: Vec<TransitionMeshBuildReport>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TransitionMeshBuildReport {
    pub face: &'static str,
    pub duration_ms: f64,
    pub vertex_count: usize,
    pub index_count: usize,
}

pub fn run_multi_lod_probe(seed: u32, preset: u32) -> MultiLodBenchmarkReport {
    let center_height = height_at(seed, preset, 0.0, 0.0) as f32;
    let center = Vec3::new(0.0, center_height, 0.0);
    let mut stream =
        BrowserTerrainStream::new(seed, preset).expect("default terrain LOD bands should be valid");
    stream.reset_around(center);

    let mut stream_ticks = 0;
    for tick in 1..=STREAM_PROBE_TICKS {
        stream.tick(center);
        stream_ticks = tick;
        let status = stream.status();
        if status.rendered_chunk_count > 0
            && status.rendered_node_count > status.rendered_chunk_count
            && status.max_rendered_lod >= 5
            && status.visible_world_span_x_meters >= MIN_REAL_SCALE_SPAN_METERS
            && status.visible_world_span_z_meters >= MIN_REAL_SCALE_SPAN_METERS
        {
            break;
        }
    }

    let status = stream.status();
    let max_lod_to_measure = status.max_rendered_lod.max(5);

    MultiLodBenchmarkReport {
        stream_ticks,
        loaded_node_count: status.loaded_node_count,
        rendered_node_count: status.rendered_node_count,
        rendered_chunk_count: status.rendered_chunk_count,
        max_rendered_lod: status.max_rendered_lod,
        visible_world_span_x_meters: status.visible_world_span_x_meters,
        visible_world_span_z_meters: status.visible_world_span_z_meters,
        transition_face_count: status.transition_face_count,
        transition_mesh_count: status.transition_mesh_count,
        transition_vertex_float_count: status.transition_vertex_float_count,
        transition_index_count: status.transition_index_count,
        lod_counts: status
            .lod_summaries
            .into_iter()
            .map(|summary| LodStreamCountReport {
                lod: summary.lod,
                desired_node_count: summary.desired_node_count,
                min_desired_node_y: summary.min_desired_node_y,
                max_desired_node_y: summary.max_desired_node_y,
                density_ready_node_count: summary.density_ready_node_count,
                rendered_node_count: summary.rendered_node_count,
                empty_node_count: summary.empty_node_count,
                missing_node_count: summary.missing_node_count,
            })
            .collect(),
        mesh_builds_by_lod: (0..=max_lod_to_measure)
            .map(|lod| measure_node_mesh_build(seed, preset, lod))
            .collect(),
        transition_mesh_builds: measure_transition_mesh_builds(seed, preset),
    }
}

fn measure_node_mesh_build(seed: u32, preset: u32, lod: u8) -> LodMeshBuildReport {
    reset_density_store();
    let key = TerrainNodeKey {
        lod,
        coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
    };
    let started_at = Instant::now();
    let mesh = build_node_mesh(seed, preset, key, BASE_CELL_SIZE);
    let duration_ms = started_at.elapsed().as_secs_f64() * 1000.0;
    let surface_started_at = Instant::now();
    let surface =
        TerrainSurfaceIndex::from_mesh(key, terrain_node_cell_size(BASE_CELL_SIZE, key.lod), &mesh);
    let surface_index_ms = surface_started_at.elapsed().as_secs_f64() * 1000.0;
    let surface_query = surface
        .as_ref()
        .map(|surface| measure_surface_queries(surface, key))
        .unwrap_or_default();

    LodMeshBuildReport {
        lod,
        duration_ms,
        vertex_count: mesh.vertices.len() / TERRAIN_VERTEX_FLOATS as usize,
        index_count: mesh.indices.len(),
        surface_index_ms,
        surface_triangle_count: surface
            .as_ref()
            .map(TerrainSurfaceIndex::triangle_count)
            .unwrap_or(0),
        surface_bin_reference_count: surface
            .as_ref()
            .map(TerrainSurfaceIndex::bin_reference_count)
            .unwrap_or(0),
        surface_max_bin_occupancy: surface
            .as_ref()
            .map(TerrainSurfaceIndex::max_bin_occupancy)
            .unwrap_or(0),
        surface_query_sample_count: surface_query.sample_count,
        surface_query_hit_count: surface_query.hit_count,
        surface_query_mean_ms: surface_query.mean_ms,
        surface_query_p95_ms: surface_query.p95_ms,
    }
}

fn measure_transition_mesh_builds(seed: u32, preset: u32) -> TransitionMeshBuildSummaryReport {
    let parent_key = TerrainNodeKey {
        lod: 1,
        coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
    };
    let parent_mesh = build_node_mesh(seed, preset, parent_key, BASE_CELL_SIZE);
    let parent_node_cell_size = terrain_node_cell_size(BASE_CELL_SIZE, parent_key.lod);
    let cases = [
        (
            "negX",
            TerrainTransitionFace::NegX,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
            },
        ),
        (
            "posX",
            TerrainTransitionFace::PosX,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 1, y: 0, z: 0 },
            },
        ),
        (
            "negZ",
            TerrainTransitionFace::NegZ,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
            },
        ),
        (
            "posZ",
            TerrainTransitionFace::PosZ,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 0, z: 1 },
            },
        ),
    ];

    let mut builds = Vec::with_capacity(cases.len());
    for (face_name, face, fine_key) in cases {
        let fine_mesh = build_node_mesh(seed, preset, fine_key, BASE_CELL_SIZE);
        let fine_node_cell_size = terrain_node_cell_size(BASE_CELL_SIZE, fine_key.lod);
        let started_at = Instant::now();
        let mesh = build_parent_lod_transition_edge_mesh(TerrainTransitionMeshInput {
            fine_key,
            parent_key,
            face,
            fine_node_cell_size,
            parent_node_cell_size,
            fine_mesh: &fine_mesh,
            parent_mesh: &parent_mesh,
            config: TerrainTransitionMeshConfig::default(),
        });
        let duration_ms = started_at.elapsed().as_secs_f64() * 1000.0;
        if let Some(mesh) = mesh {
            builds.push(TransitionMeshBuildReport {
                face: face_name,
                duration_ms,
                vertex_count: mesh.vertices.len() / TERRAIN_VERTEX_FLOATS as usize,
                index_count: mesh.indices.len(),
            });
        }
    }

    let mut durations = builds
        .iter()
        .map(|build| build.duration_ms)
        .collect::<Vec<_>>();
    durations.sort_by(|left, right| left.total_cmp(right));
    let mean_ms = if durations.is_empty() {
        0.0
    } else {
        durations.iter().sum::<f64>() / durations.len() as f64
    };

    TransitionMeshBuildSummaryReport {
        attempted_count: cases.len(),
        build_count: builds.len(),
        total_vertex_count: builds.iter().map(|build| build.vertex_count).sum(),
        total_index_count: builds.iter().map(|build| build.index_count).sum(),
        mean_ms,
        median_ms: percentile_or_zero(&durations, 0.5),
        p95_ms: percentile_or_zero(&durations, 0.95),
        builds,
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct SurfaceQueryTiming {
    sample_count: usize,
    hit_count: usize,
    mean_ms: f64,
    p95_ms: f64,
}

fn measure_surface_queries(
    surface: &TerrainSurfaceIndex,
    key: TerrainNodeKey,
) -> SurfaceQueryTiming {
    let node_cell_size = terrain_node_cell_size(BASE_CELL_SIZE, key.lod);
    let node_span = node_cell_size * terrain_core::TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let origin_x = key.coord.x as f64 * node_span;
    let origin_z = key.coord.z as f64 * node_span;
    let fractions = [0.125, 0.375, 0.625, 0.875];
    let mut durations = Vec::with_capacity(fractions.len() * fractions.len());
    let mut hit_count = 0;

    for z_fraction in fractions {
        for x_fraction in fractions {
            let started_at = Instant::now();
            if surface
                .highest_vertical_hit(TerrainVerticalQuery {
                    x: origin_x + node_span * x_fraction,
                    z: origin_z + node_span * z_fraction,
                    min_y: -100_000.0,
                    max_y: 100_000.0,
                    min_normal_y: -1.0,
                })
                .is_some()
            {
                hit_count += 1;
            }
            durations.push(started_at.elapsed().as_secs_f64() * 1000.0);
        }
    }

    durations.sort_by(|left, right| left.total_cmp(right));
    let mean_ms = durations.iter().sum::<f64>() / durations.len() as f64;
    SurfaceQueryTiming {
        sample_count: durations.len(),
        hit_count,
        mean_ms,
        p95_ms: percentile(&durations, 0.95),
    }
}

fn percentile(sorted: &[f64], fraction: f64) -> f64 {
    let index = ((sorted.len() as f64 * fraction).ceil() as usize)
        .saturating_sub(1)
        .min(sorted.len() - 1);
    sorted[index]
}

fn percentile_or_zero(sorted: &[f64], fraction: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }

    percentile(sorted, fraction)
}

#[cfg(test)]
fn visible_world_span(nodes: &[TerrainNodeKey]) -> (f64, f64) {
    if nodes.is_empty() {
        return (0.0, 0.0);
    }

    let mut min_x = f64::INFINITY;
    let mut max_x = f64::NEG_INFINITY;
    let mut min_z = f64::INFINITY;
    let mut max_z = f64::NEG_INFINITY;

    for key in nodes {
        let node_size = terrain_core::terrain_node_cell_size(BASE_CELL_SIZE, key.lod)
            * terrain_core::TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
        let x0 = key.coord.x as f64 * node_size;
        let z0 = key.coord.z as f64 * node_size;
        min_x = min_x.min(x0);
        max_x = max_x.max(x0 + node_size);
        min_z = min_z.min(z0);
        max_z = max_z.max(z0 + node_size);
    }

    (max_x - min_x, max_z - min_z)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn visible_world_span_covers_mixed_lod_nodes() {
        let nodes = [
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
            },
            TerrainNodeKey {
                lod: 2,
                coord: TerrainChunkCoord { x: 1, y: 0, z: 0 },
            },
        ];

        let (span_x, span_z) = visible_world_span(&nodes);

        assert_eq!(span_x, 256.0);
        assert_eq!(span_z, 128.0);
    }

    #[test]
    fn mesh_build_report_records_lod_and_buffer_counts() {
        let report = measure_node_mesh_build(0x0F6, 1, 1);

        assert_eq!(report.lod, 1);
        assert!(report.duration_ms >= 0.0);
        assert!(report.vertex_count > 0);
        assert!(report.index_count > 0);
        assert!(report.surface_index_ms >= 0.0);
        assert!(report.surface_triangle_count > 0);
        assert!(report.surface_bin_reference_count >= report.surface_triangle_count);
        assert!(report.surface_max_bin_occupancy > 0);
        assert_eq!(report.surface_query_sample_count, 16);
        assert!(report.surface_query_hit_count <= report.surface_query_sample_count);
        assert!(report.surface_query_mean_ms >= 0.0);
        assert!(report.surface_query_p95_ms >= 0.0);
    }

    #[test]
    fn transition_mesh_build_summary_records_counts_and_timings() {
        let report = measure_transition_mesh_builds(0x0F6, 1);

        assert_eq!(report.attempted_count, 4);
        assert!(report.build_count > 0);
        assert_eq!(report.builds.len(), report.build_count);
        assert!(report.total_vertex_count > 0);
        assert!(report.total_index_count > 0);
        assert!(report.mean_ms >= 0.0);
        assert!(report.median_ms >= 0.0);
        assert!(report.p95_ms >= 0.0);
        assert!(report
            .builds
            .iter()
            .all(|build| build.vertex_count > 0 && build.index_count > 0));
    }
}
