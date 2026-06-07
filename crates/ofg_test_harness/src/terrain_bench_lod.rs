// Multi-LOD terrain benchmark probes used by the terrain benchmark report.

use std::time::Instant;

use engine_core::Vec3;
use engine_web::{BrowserTerrainStream, TERRAIN_VERTEX_FLOATS};
use serde::Serialize;
use terrain_core::benchmark::reset_density_store;
use terrain_core::{build_node_mesh, height_at, TerrainChunkCoord, TerrainNodeKey};

const BASE_CELL_SIZE: f64 = 1.0;
const STREAM_PROBE_TICKS: usize = 1600;
const MIN_MULTI_KM_SPAN_METERS: f64 = 4096.0;

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
    pub lod_counts: Vec<LodStreamCountReport>,
    pub mesh_builds_by_lod: Vec<LodMeshBuildReport>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LodStreamCountReport {
    pub lod: u8,
    pub desired_node_count: usize,
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
            && status.max_rendered_lod >= 3
            && status.visible_world_span_x_meters >= MIN_MULTI_KM_SPAN_METERS
            && status.visible_world_span_z_meters >= MIN_MULTI_KM_SPAN_METERS
        {
            break;
        }
    }

    let status = stream.status();
    let max_lod_to_measure = status.max_rendered_lod.max(4);

    MultiLodBenchmarkReport {
        stream_ticks,
        loaded_node_count: status.loaded_node_count,
        rendered_node_count: status.rendered_node_count,
        rendered_chunk_count: status.rendered_chunk_count,
        max_rendered_lod: status.max_rendered_lod,
        visible_world_span_x_meters: status.visible_world_span_x_meters,
        visible_world_span_z_meters: status.visible_world_span_z_meters,
        lod_counts: status
            .lod_summaries
            .into_iter()
            .map(|summary| LodStreamCountReport {
                lod: summary.lod,
                desired_node_count: summary.desired_node_count,
                density_ready_node_count: summary.density_ready_node_count,
                rendered_node_count: summary.rendered_node_count,
                empty_node_count: summary.empty_node_count,
                missing_node_count: summary.missing_node_count,
            })
            .collect(),
        mesh_builds_by_lod: (0..=max_lod_to_measure)
            .map(|lod| measure_node_mesh_build(seed, preset, lod))
            .collect(),
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

    LodMeshBuildReport {
        lod,
        duration_ms,
        vertex_count: mesh.vertices.len() / TERRAIN_VERTEX_FLOATS as usize,
        index_count: mesh.indices.len(),
    }
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
    }
}
