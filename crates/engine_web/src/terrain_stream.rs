// Mirrors terrain_core stream events into browser-facing terrain update packets.
//
// Terrain policy lives in terrain_core. This adapter keeps browser/WGPU code on
// simple cache operations: create this mesh, destroy that mesh, render this
// visible node list, and ask terrain_core for mesh-backed height samples.

use std::sync::Arc;
#[cfg(not(target_arch = "wasm32"))]
use std::sync::OnceLock;
#[cfg(not(target_arch = "wasm32"))]
use std::time::Instant;

use crate::terrain_transitions::BrowserTerrainTransitionMeshUpdate;
use engine_core::Vec3;
use terrain_core::{
    terrain_chunk_key, terrain_node_cell_size, terrain_node_key, terrain_variant_for_preset,
    MeshData, TerrainLodBand, TerrainLodStatus as CoreTerrainLodStatus, TerrainNodeKey,
    TerrainStreamConfig, TerrainStreamError, TerrainStreamHeightSample, TerrainStreamScheduler,
    TerrainStreamStatus as CoreTerrainStreamStatus, TerrainTransitionMeshKey,
    TerrainVariantDescriptor, TerrainVariantValidationError, WaterNodePacket,
    TERRAIN_CHUNK_CELLS_PER_AXIS,
};

const DEFAULT_TERRAIN_CELL_SIZE: f64 = 1.0;
#[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
pub const MAX_SAFE_TERRAIN_WORKER_REQUEST_ID: u64 = 9_007_199_254_740_991;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainJobStats {
    pub total_ms: f64,
    pub vertex_count: usize,
    pub index_count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserTerrainBuildRequest {
    pub request_id: u64,
    pub generation: u64,
    pub key: TerrainNodeKey,
    pub seed: u32,
    pub preset: u32,
    pub variant_revision: u64,
    pub terrain_variant: TerrainVariantDescriptor,
    pub cell_size: f64,
}

#[derive(Clone, Debug, PartialEq)]
pub struct BrowserTerrainBuildCompletion {
    pub request_id: u64,
    pub generation: u64,
    pub key: TerrainNodeKey,
    pub variant_revision: u64,
    pub vertices: Vec<f32>,
    pub indices: Vec<u32>,
    pub water: Option<WaterNodePacket>,
    pub failed: bool,
}

pub type BrowserTerrainHeightSample = TerrainStreamHeightSample;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrowserTerrainLodStatus {
    pub lod: u8,
    pub desired_node_count: usize,
    pub min_desired_node_y: Option<i32>,
    pub max_desired_node_y: Option<i32>,
    pub density_ready_node_count: usize,
    pub rendered_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub struct BrowserTerrainStreamStatus {
    pub generation: u64,
    pub pending: bool,
    pub loaded_chunk_count: usize,
    pub density_ready_chunk_count: usize,
    pub shared_density_chunk_count: usize,
    pub in_flight_density_count: usize,
    pub missing_density_count: usize,
    pub desired_render_chunk_count: usize,
    pub rendered_chunk_count: usize,
    pub empty_chunk_count: usize,
    pub in_flight_chunk_count: usize,
    pub missing_chunk_count: usize,
    pub loaded_node_count: usize,
    pub desired_render_node_count: usize,
    pub rendered_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
    pub max_rendered_lod: u8,
    pub visible_world_span_x_meters: f64,
    pub visible_world_span_z_meters: f64,
    pub lod_summaries: Vec<BrowserTerrainLodStatus>,
    pub placement_candidate_count: usize,
    pub placement_sample_count: usize,
    pub placement_missed_surface_count: usize,
    pub placement_rejected_below_water_count: usize,
    pub placement_rejected_slope_count: usize,
    pub transition_face_count: usize,
    pub transition_mesh_count: usize,
    pub transition_vertex_float_count: usize,
    pub transition_index_count: usize,
    pub max_concurrent_chunk_jobs: usize,
    pub terrain_worker_runtime: &'static str,
    pub terrain_worker_count: usize,
    pub terrain_worker_in_flight_count: usize,
    pub terrain_worker_queued_request_count: usize,
    pub terrain_worker_completed_count: u64,
    pub terrain_worker_stale_completion_count: u64,
    pub terrain_worker_failed_count: u64,
    pub synchronous_build_count: u64,
    pub last_density_job_stats: Option<TerrainJobStats>,
    pub last_chunk_job_stats: Option<TerrainJobStats>,
}

pub struct BrowserTerrainMeshUpdate {
    pub key: TerrainNodeKey,
    pub mesh: Arc<MeshData>,
}

pub struct BrowserTerrainWaterUpdate {
    pub key: TerrainNodeKey,
    pub water: Arc<WaterNodePacket>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct BrowserTerrainStreamTickTimings {
    pub sync_around_ms: f64,
    pub scheduler_tick_ms: f64,
    pub worker_request_queue_ms: f64,
    pub visibility_sync_ms: f64,
    pub visibility_select_ms: f64,
    pub visibility_status_ms: f64,
    pub visibility_apply_ms: f64,
}

#[derive(Default)]
pub struct BrowserTerrainStreamUpdate {
    pub removed_nodes: Vec<TerrainNodeKey>,
    pub removed_transition_meshes: Vec<TerrainTransitionMeshKey>,
    pub removed_water_nodes: Vec<TerrainNodeKey>,
    pub upserted_meshes: Vec<BrowserTerrainMeshUpdate>,
    pub upserted_transition_meshes: Vec<BrowserTerrainTransitionMeshUpdate>,
    pub upserted_water: Vec<BrowserTerrainWaterUpdate>,
    pub visible_nodes: Vec<TerrainNodeKey>,
    pub timings: BrowserTerrainStreamTickTimings,
}

pub struct BrowserTerrainStream {
    seed: u32,
    terrain_variant: TerrainVariantDescriptor,
    terrain_variant_revision: u64,
    cell_size: f64,
    core: TerrainStreamScheduler,
    terrain_worker_count: usize,
    terrain_worker_stale_completion_count: u64,
    terrain_worker_failed_count: u64,
    last_density_job_stats: Option<TerrainJobStats>,
    last_chunk_job_stats: Option<TerrainJobStats>,
}

impl BrowserTerrainStream {
    pub fn new(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        Self::new_with_variant_lod_bands(
            seed,
            terrain_variant_for_preset(preset),
            TerrainStreamConfig::default().lod_bands,
        )
    }

    pub fn new_lod0(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        Self::new_with_lod_bands(
            seed,
            preset,
            vec![TerrainLodBand::fixed_offsets(0, 1, vec![-1, 0, 1])],
        )
    }

    pub fn new_with_lod_bands(
        seed: u32,
        preset: u32,
        lod_bands: Vec<TerrainLodBand>,
    ) -> Result<Self, TerrainStreamError> {
        Self::new_with_variant_lod_bands(seed, terrain_variant_for_preset(preset), lod_bands)
    }

    pub fn new_with_variant_lod_bands(
        seed: u32,
        terrain_variant: TerrainVariantDescriptor,
        lod_bands: Vec<TerrainLodBand>,
    ) -> Result<Self, TerrainStreamError> {
        let core = TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands,
            max_in_flight_jobs: 1,
            terrain_seed: seed,
            terrain_variant,
            base_cell_size: DEFAULT_TERRAIN_CELL_SIZE,
        })?;

        Ok(Self {
            seed,
            terrain_variant,
            terrain_variant_revision: 1,
            cell_size: DEFAULT_TERRAIN_CELL_SIZE,
            core,
            terrain_worker_count: 0,
            terrain_worker_stale_completion_count: 0,
            terrain_worker_failed_count: 0,
            last_density_job_stats: None,
            last_chunk_job_stats: None,
        })
    }

    pub fn reset_game(&mut self, seed: u32, preset: u32, center: Vec3) -> Vec<TerrainNodeKey> {
        self.reset_game_with_variant(seed, terrain_variant_for_preset(preset), center)
            .expect("catalog sine terrain variant should be valid")
    }

    pub fn reset_game_with_variant(
        &mut self,
        seed: u32,
        terrain_variant: TerrainVariantDescriptor,
        center: Vec3,
    ) -> Result<Vec<TerrainNodeKey>, TerrainVariantValidationError> {
        terrain_variant.validate()?;
        self.seed = seed;
        self.terrain_variant = terrain_variant;
        self.core
            .set_terrain_context(seed, terrain_variant, self.cell_size)
            .expect("validated terrain stream context should be accepted");
        self.terrain_variant_revision = self.terrain_variant_revision.wrapping_add(1);
        Ok(self.reset_around(center))
    }

    pub fn reset_around(&mut self, center: Vec3) -> Vec<TerrainNodeKey> {
        let removed_nodes = self.core.visible_nodes();
        self.core.invalidate_all();
        let _ = center;
        removed_nodes
    }

    pub fn tick(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        self.tick_internal(center)
    }

    pub fn tick_for_workers(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        self.tick_internal(center)
    }

    pub fn configure_worker_runtime(
        &mut self,
        worker_count: usize,
    ) -> Result<(), TerrainStreamError> {
        self.terrain_worker_count = worker_count;
        self.core.set_max_in_flight_jobs(worker_count.max(1))
    }

    pub fn take_worker_build_requests(&mut self) -> Vec<BrowserTerrainBuildRequest> {
        Vec::new()
    }

    pub fn complete_worker_build(&mut self, _completion: BrowserTerrainBuildCompletion) -> bool {
        self.terrain_worker_stale_completion_count =
            self.terrain_worker_stale_completion_count.saturating_add(1);
        false
    }

    pub fn loaded_chunk_keys(&self) -> Vec<String> {
        self.core
            .desired_density_coords()
            .into_iter()
            .map(terrain_chunk_key)
            .collect()
    }

    pub fn loaded_node_keys(&self) -> Vec<String> {
        self.core
            .desired_density_nodes()
            .into_iter()
            .map(terrain_node_key)
            .collect()
    }

    pub fn render_chunk_keys(&self) -> Vec<String> {
        self.core
            .visible_nodes()
            .into_iter()
            .filter(|key| key.lod == 0)
            .map(|key| terrain_chunk_key(key.coord))
            .collect()
    }

    pub fn render_node_keys(&self) -> Vec<String> {
        self.render_nodes()
            .into_iter()
            .map(terrain_node_key)
            .collect()
    }

    pub fn render_nodes(&self) -> Vec<TerrainNodeKey> {
        self.core.visible_nodes()
    }

    pub fn status(&self) -> BrowserTerrainStreamStatus {
        let status = self.core.status();
        let visible_nodes = self.core.visible_nodes();
        let max_rendered_lod = visible_nodes.iter().map(|key| key.lod).max().unwrap_or(0);
        let rendered_chunk_count = visible_nodes.iter().filter(|key| key.lod == 0).count();
        let (visible_world_span_x_meters, visible_world_span_z_meters) =
            visible_world_span(&visible_nodes, self.cell_size);

        BrowserTerrainStreamStatus {
            generation: status.generation,
            pending: is_stream_pending(&status),
            loaded_chunk_count: status.desired_density_count,
            density_ready_chunk_count: status.density_ready_count,
            shared_density_chunk_count: 0,
            in_flight_density_count: status.in_flight_density_count,
            missing_density_count: status.missing_density_count,
            desired_render_chunk_count: status.desired_lod0_count,
            rendered_chunk_count,
            empty_chunk_count: status.lod0_empty_count,
            in_flight_chunk_count: status.in_flight_lod_count,
            missing_chunk_count: status.missing_lod0_count,
            loaded_node_count: status.desired_density_count,
            desired_render_node_count: status.desired_mesh_count,
            rendered_node_count: visible_nodes.len(),
            empty_node_count: status.mesh_empty_count,
            missing_node_count: status.missing_mesh_count,
            max_rendered_lod,
            visible_world_span_x_meters,
            visible_world_span_z_meters,
            lod_summaries: status
                .lod_summaries
                .into_iter()
                .map(|summary| browser_lod_status(summary, &visible_nodes))
                .collect(),
            placement_candidate_count: 0,
            placement_sample_count: 0,
            placement_missed_surface_count: 0,
            placement_rejected_below_water_count: 0,
            placement_rejected_slope_count: 0,
            transition_face_count: 0,
            transition_mesh_count: 0,
            transition_vertex_float_count: 0,
            transition_index_count: 0,
            max_concurrent_chunk_jobs: status.max_in_flight_jobs,
            terrain_worker_runtime: "rust-sync",
            terrain_worker_count: self.terrain_worker_count,
            terrain_worker_in_flight_count: 0,
            terrain_worker_queued_request_count: 0,
            terrain_worker_completed_count: 0,
            terrain_worker_stale_completion_count: self.terrain_worker_stale_completion_count,
            terrain_worker_failed_count: self.terrain_worker_failed_count,
            synchronous_build_count: self.core.completed_build_count(),
            last_density_job_stats: self.last_density_job_stats,
            last_chunk_job_stats: self.last_chunk_job_stats,
        }
    }

    pub fn worker_count(&self) -> usize {
        self.terrain_worker_count
    }

    pub fn terrain_variant(&self) -> TerrainVariantDescriptor {
        self.terrain_variant
    }

    pub fn terrain_variant_revision(&self) -> u64 {
        self.terrain_variant_revision
    }

    pub fn height_at(&self, x: f32, z: f32) -> Option<BrowserTerrainHeightSample> {
        self.core.height_at(x, z)
    }

    pub fn height_at_in_nodes(
        &self,
        x: f32,
        z: f32,
        nodes: impl IntoIterator<Item = TerrainNodeKey>,
    ) -> Option<BrowserTerrainHeightSample> {
        if nodes.into_iter().next().is_none() {
            return None;
        }
        self.core.height_at(x, z)
    }

    pub fn height_at_below(
        &self,
        x: f32,
        z: f32,
        ray_start_y: f32,
    ) -> Option<BrowserTerrainHeightSample> {
        self.core.height_at_below(x, z, ray_start_y)
    }

    pub fn height_at_below_in_nodes(
        &self,
        x: f32,
        z: f32,
        ray_start_y: f32,
        nodes: impl IntoIterator<Item = TerrainNodeKey>,
    ) -> Option<BrowserTerrainHeightSample> {
        if nodes.into_iter().next().is_none() {
            return None;
        }
        self.core.height_at_below(x, z, ray_start_y)
    }

    fn tick_internal(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        let mut timings = BrowserTerrainStreamTickTimings::default();
        let started_at_ms = terrain_stream_now_ms();
        let update = self.core.sync_position(center.x, center.y, center.z);
        timings.sync_around_ms = terrain_stream_now_ms() - started_at_ms;
        timings.visibility_sync_ms = timings.sync_around_ms;

        if let Some(last) = update.created_nodes.last() {
            self.last_density_job_stats = Some(TerrainJobStats {
                total_ms: 0.0,
                vertex_count: 0,
                index_count: 0,
            });
            self.last_chunk_job_stats = Some(TerrainJobStats {
                total_ms: 0.0,
                vertex_count: last.mesh.vertices.len(),
                index_count: last.mesh.indices.len(),
            });
        }

        BrowserTerrainStreamUpdate {
            removed_nodes: update.destroyed_nodes,
            removed_transition_meshes: Vec::new(),
            removed_water_nodes: Vec::new(),
            upserted_meshes: update
                .created_nodes
                .into_iter()
                .map(|mesh_update| BrowserTerrainMeshUpdate {
                    key: mesh_update.key,
                    mesh: mesh_update.mesh,
                })
                .collect(),
            upserted_transition_meshes: Vec::new(),
            upserted_water: Vec::new(),
            visible_nodes: update.visible_nodes,
            timings,
        }
    }
}

fn browser_lod_status(
    status: CoreTerrainLodStatus,
    visible_nodes: &[TerrainNodeKey],
) -> BrowserTerrainLodStatus {
    BrowserTerrainLodStatus {
        lod: status.lod,
        desired_node_count: status.desired_node_count,
        min_desired_node_y: status.min_desired_node_y,
        max_desired_node_y: status.max_desired_node_y,
        density_ready_node_count: status.density_ready_node_count,
        rendered_node_count: visible_nodes
            .iter()
            .filter(|key| key.lod == status.lod)
            .count(),
        empty_node_count: status.empty_node_count,
        missing_node_count: status.missing_node_count,
    }
}

fn is_stream_pending(status: &CoreTerrainStreamStatus) -> bool {
    status.in_flight_density_count > 0
        || status.in_flight_lod_count > 0
        || status.missing_density_count > 0
        || status.missing_mesh_count > 0
}

fn visible_world_span(nodes: &[TerrainNodeKey], base_cell_size: f64) -> (f64, f64) {
    if nodes.is_empty() {
        return (0.0, 0.0);
    }

    let mut min_x = f64::INFINITY;
    let mut max_x = f64::NEG_INFINITY;
    let mut min_z = f64::INFINITY;
    let mut max_z = f64::NEG_INFINITY;

    for key in nodes {
        let node_size =
            terrain_node_cell_size(base_cell_size, key.lod) * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
        let x0 = key.coord.x as f64 * node_size;
        let z0 = key.coord.z as f64 * node_size;
        min_x = min_x.min(x0);
        max_x = max_x.max(x0 + node_size);
        min_z = min_z.min(z0);
        max_z = max_z.max(z0 + node_size);
    }

    (max_x - min_x, max_z - min_z)
}

#[cfg(target_arch = "wasm32")]
fn terrain_stream_now_ms() -> f64 {
    js_sys::Date::now()
}

#[cfg(not(target_arch = "wasm32"))]
fn terrain_stream_now_ms() -> f64 {
    static START: OnceLock<Instant> = OnceLock::new();
    START.get_or_init(Instant::now).elapsed().as_secs_f64() * 1000.0
}
