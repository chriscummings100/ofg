// Owns the browser game's minimal terrain stream state inside Rust.
//
// The terrain rebuild starts from grass-only sine terrain. This wrapper keeps
// TypeScript as an opaque worker transport while Rust owns LOD desired sets,
// request ids, completion validation, visible cover, and mesh packets.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::sync::Arc;
#[cfg(not(target_arch = "wasm32"))]
use std::sync::OnceLock;
#[cfg(not(target_arch = "wasm32"))]
use std::time::Instant;

use crate::terrain_transitions::BrowserTerrainTransitionMeshUpdate;
use engine_core::Vec3;
use terrain_core::{
    build_node_mesh_for_variant, terrain_chunk_coord_containing_position, terrain_chunk_key,
    terrain_node_cell_size, terrain_node_children, terrain_node_key, terrain_node_parent,
    terrain_variant_for_preset, MeshData, TerrainChunkCoord, TerrainLodBand,
    TerrainLodBoundedVerticalPolicy, TerrainLodStatus as CoreTerrainLodStatus, TerrainNodeKey,
    TerrainStreamConfig, TerrainStreamError, TerrainStreamJob, TerrainStreamScheduler,
    TerrainStreamStatus as CoreTerrainStreamStatus, TerrainTransitionMeshKey,
    TerrainVariantDescriptor, TerrainVariantValidationError, WaterNodePacket,
    TERRAIN_CHUNK_CELLS_PER_AXIS,
};

const DEFAULT_TERRAIN_CELL_SIZE: f64 = 1.0;
const DEFAULT_TERRAIN_MAX_JOBS_PER_TICK: usize = 6;
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

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserTerrainHeightSample {
    pub key: TerrainNodeKey,
    pub height: f32,
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
    pub timings: BrowserTerrainStreamTickTimings,
}

pub struct BrowserTerrainStream {
    seed: u32,
    terrain_variant: TerrainVariantDescriptor,
    terrain_variant_revision: u64,
    cell_size: f64,
    scheduler: TerrainStreamScheduler,
    last_center_coord: Option<TerrainChunkCoord>,
    mesh_cache: BTreeMap<TerrainNodeKey, Arc<MeshData>>,
    visible_nodes: BTreeSet<TerrainNodeKey>,
    visibility_dirty: bool,
    next_worker_request_id: u64,
    queued_worker_requests: VecDeque<BrowserTerrainBuildRequest>,
    in_flight_worker_requests: BTreeMap<u64, BrowserTerrainBuildRequest>,
    terrain_worker_count: usize,
    terrain_worker_completed_count: u64,
    terrain_worker_stale_completion_count: u64,
    terrain_worker_failed_count: u64,
    synchronous_build_count: u64,
    last_density_job_stats: Option<TerrainJobStats>,
    last_chunk_job_stats: Option<TerrainJobStats>,
}

impl BrowserTerrainStream {
    pub fn new(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        Self::new_with_lod_bands(seed, preset, default_terrain_lod_bands())
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
        let scheduler = TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands,
            max_in_flight_jobs: DEFAULT_TERRAIN_MAX_JOBS_PER_TICK,
            terrain_seed: seed,
            terrain_variant,
            base_cell_size: DEFAULT_TERRAIN_CELL_SIZE,
        })?;

        Ok(Self {
            seed,
            terrain_variant,
            terrain_variant_revision: 1,
            cell_size: DEFAULT_TERRAIN_CELL_SIZE,
            scheduler,
            last_center_coord: None,
            mesh_cache: BTreeMap::new(),
            visible_nodes: BTreeSet::new(),
            visibility_dirty: true,
            next_worker_request_id: 1,
            queued_worker_requests: VecDeque::new(),
            in_flight_worker_requests: BTreeMap::new(),
            terrain_worker_count: 0,
            terrain_worker_completed_count: 0,
            terrain_worker_stale_completion_count: 0,
            terrain_worker_failed_count: 0,
            synchronous_build_count: 0,
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
        self.scheduler
            .set_terrain_context(seed, terrain_variant, self.cell_size)
            .expect("validated terrain stream context should be accepted");
        self.terrain_variant_revision = self.terrain_variant_revision.wrapping_add(1);
        Ok(self.reset_around(center))
    }

    pub fn reset_around(&mut self, center: Vec3) -> Vec<TerrainNodeKey> {
        let removed_nodes = self.visible_nodes.iter().copied().collect::<Vec<_>>();
        self.mesh_cache.clear();
        self.visible_nodes.clear();
        self.queued_worker_requests.clear();
        self.in_flight_worker_requests.clear();
        self.last_density_job_stats = None;
        self.last_chunk_job_stats = None;

        let center_coord = self.coord_containing_position(center);
        self.scheduler.reset(center_coord);
        self.last_center_coord = Some(center_coord);
        self.visibility_dirty = true;
        removed_nodes
    }

    pub fn tick(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        self.tick_internal(center, false)
    }

    pub fn tick_for_workers(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        self.tick_internal(center, true)
    }

    pub fn configure_worker_runtime(
        &mut self,
        worker_count: usize,
    ) -> Result<(), TerrainStreamError> {
        self.scheduler.set_max_in_flight_jobs(worker_count.max(1))?;
        self.terrain_worker_count = worker_count;
        Ok(())
    }

    pub fn take_worker_build_requests(&mut self) -> Vec<BrowserTerrainBuildRequest> {
        self.queued_worker_requests.drain(..).collect()
    }

    pub fn complete_worker_build(&mut self, completion: BrowserTerrainBuildCompletion) -> bool {
        let Some(request) = self
            .in_flight_worker_requests
            .remove(&completion.request_id)
        else {
            self.terrain_worker_stale_completion_count += 1;
            return false;
        };

        if request.generation != completion.generation
            || request.key != completion.key
            || request.variant_revision != completion.variant_revision
        {
            self.terrain_worker_stale_completion_count += 1;
            let _ = self.scheduler.fail_node(request.generation, request.key);
            self.visibility_dirty = true;
            return false;
        }

        if completion.failed {
            self.terrain_worker_failed_count += 1;
            let failed = self
                .scheduler
                .fail_node(completion.generation, completion.key);
            self.visibility_dirty |= failed;
            return failed;
        }

        let empty = completion.indices.is_empty();
        if !self
            .scheduler
            .complete_node(completion.generation, completion.key, empty)
        {
            self.terrain_worker_stale_completion_count += 1;
            return false;
        }

        self.terrain_worker_completed_count += 1;
        self.visibility_dirty = true;
        self.cache_completed_mesh(completion.key, completion.vertices, completion.indices);
        true
    }

    pub fn loaded_chunk_keys(&self) -> Vec<String> {
        self.scheduler
            .desired_density_coords()
            .into_iter()
            .map(terrain_chunk_key)
            .collect()
    }

    pub fn loaded_node_keys(&self) -> Vec<String> {
        self.scheduler
            .desired_density_nodes()
            .into_iter()
            .map(terrain_node_key)
            .collect()
    }

    pub fn render_chunk_keys(&self) -> Vec<String> {
        self.visible_nodes
            .iter()
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
        self.visible_nodes.iter().copied().collect()
    }

    pub fn status(&self) -> BrowserTerrainStreamStatus {
        let status = self.scheduler.status();
        let max_rendered_lod = self
            .visible_nodes
            .iter()
            .map(|key| key.lod)
            .max()
            .unwrap_or(0);
        let rendered_chunk_count = self.visible_nodes.iter().filter(|key| key.lod == 0).count();
        let (visible_world_span_x_meters, visible_world_span_z_meters) =
            visible_world_span(&self.visible_nodes, self.cell_size);

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
            rendered_node_count: self.visible_nodes.len(),
            empty_node_count: status.mesh_empty_count,
            missing_node_count: status.missing_mesh_count,
            max_rendered_lod,
            visible_world_span_x_meters,
            visible_world_span_z_meters,
            lod_summaries: status
                .lod_summaries
                .into_iter()
                .map(|summary| browser_lod_status(summary, &self.visible_nodes))
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
            terrain_worker_runtime: if self.terrain_worker_count == 0 {
                "rust-sync"
            } else {
                "browser-worker"
            },
            terrain_worker_count: self.terrain_worker_count,
            terrain_worker_in_flight_count: self.in_flight_worker_requests.len(),
            terrain_worker_queued_request_count: self.queued_worker_requests.len(),
            terrain_worker_completed_count: self.terrain_worker_completed_count,
            terrain_worker_stale_completion_count: self.terrain_worker_stale_completion_count,
            terrain_worker_failed_count: self.terrain_worker_failed_count,
            synchronous_build_count: self.synchronous_build_count,
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
        self.height_at_from_nodes(x, z, self.visible_nodes.iter().copied())
    }

    fn tick_internal(&mut self, center: Vec3, use_workers: bool) -> BrowserTerrainStreamUpdate {
        let mut update = BrowserTerrainStreamUpdate::default();
        let sync_started_at_ms = terrain_stream_now_ms();
        self.sync_around(center);
        update.timings.sync_around_ms = terrain_stream_now_ms() - sync_started_at_ms;

        let scheduler_started_at_ms = terrain_stream_now_ms();
        let jobs = self.scheduler.tick();
        update.timings.scheduler_tick_ms = terrain_stream_now_ms() - scheduler_started_at_ms;

        let queue_started_at_ms = terrain_stream_now_ms();
        for job in jobs {
            match job {
                TerrainStreamJob::BuildNode { generation, key } if use_workers => {
                    self.queue_worker_build_request(generation, key);
                }
                TerrainStreamJob::BuildNode { generation, key } => {
                    self.complete_node_job(generation, key);
                }
            }
        }
        update.timings.worker_request_queue_ms = terrain_stream_now_ms() - queue_started_at_ms;

        if self.visibility_dirty {
            let visibility_started_at_ms = terrain_stream_now_ms();
            self.sync_visible_meshes(&mut update);
            update.timings.visibility_sync_ms = terrain_stream_now_ms() - visibility_started_at_ms;
            self.visibility_dirty = false;
        }

        update
    }

    fn sync_around(&mut self, center: Vec3) {
        let center_coord = self.coord_containing_position(center);
        if self.last_center_coord != Some(center_coord) {
            self.scheduler.sync_center(center_coord);
            self.last_center_coord = Some(center_coord);
            self.mesh_cache.retain(|key, _mesh| {
                self.visible_nodes.contains(key)
                    || self.scheduler.desired_mesh_nodes().contains(key)
            });
            self.visibility_dirty = true;
        }
    }

    fn queue_worker_build_request(&mut self, generation: u64, key: TerrainNodeKey) {
        let request_id = self.next_worker_request_id;
        self.next_worker_request_id =
            if self.next_worker_request_id >= MAX_SAFE_TERRAIN_WORKER_REQUEST_ID {
                1
            } else {
                self.next_worker_request_id + 1
            };
        let request = BrowserTerrainBuildRequest {
            request_id,
            generation,
            key,
            seed: self.seed,
            preset: self.terrain_variant.preset,
            variant_revision: self.terrain_variant_revision,
            terrain_variant: self.terrain_variant,
            cell_size: terrain_node_cell_size(self.cell_size, key.lod),
        };

        self.in_flight_worker_requests.insert(request_id, request);
        self.queued_worker_requests.push_back(request);
    }

    fn complete_node_job(&mut self, generation: u64, key: TerrainNodeKey) {
        let mesh =
            build_node_mesh_for_variant(self.seed, self.terrain_variant, key, self.cell_size);
        let empty = mesh.indices.is_empty();
        if !self.scheduler.complete_node(generation, key, empty) {
            return;
        }

        self.synchronous_build_count += 1;
        self.visibility_dirty = true;
        self.last_density_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: 0,
            index_count: 0,
        });
        self.last_chunk_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: mesh.vertices.len(),
            index_count: mesh.indices.len(),
        });

        self.cache_mesh(key, mesh);
    }

    fn cache_completed_mesh(&mut self, key: TerrainNodeKey, vertices: Vec<f32>, indices: Vec<u32>) {
        self.last_density_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: 0,
            index_count: 0,
        });
        self.last_chunk_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: vertices.len(),
            index_count: indices.len(),
        });
        self.cache_mesh(key, MeshData { vertices, indices });
    }

    fn cache_mesh(&mut self, key: TerrainNodeKey, mesh: MeshData) {
        if mesh.indices.is_empty() {
            self.mesh_cache.remove(&key);
        } else {
            self.mesh_cache.insert(key, Arc::new(mesh));
        }
    }

    fn sync_visible_meshes(&mut self, update: &mut BrowserTerrainStreamUpdate) {
        let select_started_at_ms = terrain_stream_now_ms();
        let desired_visible = self.select_visible_nodes();
        update.timings.visibility_select_ms = terrain_stream_now_ms() - select_started_at_ms;

        let status_started_at_ms = terrain_stream_now_ms();
        let stream_pending = self.scheduler.pending();
        update.timings.visibility_status_ms = terrain_stream_now_ms() - status_started_at_ms;

        let apply_started_at_ms = terrain_stream_now_ms();
        if desired_visible != self.visible_nodes {
            self.apply_visible_mesh_nodes(update, &desired_visible, stream_pending);
        }
        update.timings.visibility_apply_ms = terrain_stream_now_ms() - apply_started_at_ms;
    }

    fn apply_visible_mesh_nodes(
        &mut self,
        update: &mut BrowserTerrainStreamUpdate,
        desired_visible: &BTreeSet<TerrainNodeKey>,
        stream_pending: bool,
    ) {
        let desired_visible_ancestors = visible_ancestor_set(desired_visible);
        let removed = self
            .visible_nodes
            .iter()
            .filter(|key| {
                !desired_visible.contains(key)
                    && (!stream_pending
                        || hierarchy_conflicts_with_visible(
                            **key,
                            desired_visible,
                            &desired_visible_ancestors,
                        ))
            })
            .copied()
            .collect::<Vec<_>>();
        let added = desired_visible
            .iter()
            .filter(|key| !self.visible_nodes.contains(key))
            .copied()
            .collect::<Vec<_>>();

        for key in removed {
            self.visible_nodes.remove(&key);
            update.removed_nodes.push(key);
        }

        for key in added {
            let Some(mesh) = self.mesh_cache.get(&key) else {
                continue;
            };
            update.upserted_meshes.push(BrowserTerrainMeshUpdate {
                key,
                mesh: Arc::clone(mesh),
            });
            self.visible_nodes.insert(key);
        }
    }

    fn select_visible_nodes(&self) -> BTreeSet<TerrainNodeKey> {
        let desired = self
            .scheduler
            .desired_mesh_nodes()
            .into_iter()
            .collect::<BTreeSet<_>>();
        let roots = desired
            .iter()
            .filter(|key| match terrain_node_parent(**key) {
                Some(parent) => !desired.contains(&parent),
                None => true,
            })
            .copied()
            .collect::<Vec<_>>();
        let mut visible = BTreeSet::new();

        for root in roots {
            self.select_visible_node(root, &desired, &mut visible);
        }

        visible
    }

    fn select_visible_node(
        &self,
        key: TerrainNodeKey,
        desired: &BTreeSet<TerrainNodeKey>,
        visible: &mut BTreeSet<TerrainNodeKey>,
    ) {
        if !self.scheduler.mesh_generated(key) {
            return;
        }

        if let Some(children) = terrain_node_children(key) {
            let children_cover_parent = children
                .iter()
                .all(|child| desired.contains(child) && self.scheduler.mesh_generated(*child));

            if children_cover_parent {
                for child in children {
                    self.select_visible_node(child, desired, visible);
                }
                return;
            }
        }

        if self.mesh_cache.contains_key(&key) {
            visible.insert(key);
        }
    }

    fn coord_containing_position(&self, position: Vec3) -> TerrainChunkCoord {
        terrain_chunk_coord_containing_position(position.x, position.y, position.z, self.cell_size)
    }

    fn height_at_from_nodes(
        &self,
        x: f32,
        z: f32,
        nodes: impl Iterator<Item = TerrainNodeKey>,
    ) -> Option<BrowserTerrainHeightSample> {
        if !x.is_finite() || !z.is_finite() {
            return None;
        }

        let mut best: Option<BrowserTerrainHeightSample> = None;
        for key in nodes {
            if !node_contains_xz(key, self.cell_size, x, z) {
                continue;
            }
            let Some(mesh) = self.mesh_cache.get(&key) else {
                continue;
            };
            let Some(height) = mesh.height_at(x, z) else {
                continue;
            };
            let sample = BrowserTerrainHeightSample { key, height };
            best = match best {
                Some(previous) if previous.key.lod <= key.lod => Some(previous),
                _ => Some(sample),
            };
        }

        best
    }
}

fn default_terrain_lod_bands() -> Vec<TerrainLodBand> {
    vec![
        TerrainLodBand::bounded(5, 1, bounded_vertical_policy(1, 1)),
        TerrainLodBand::bounded(4, 1, bounded_vertical_policy(1, 1)),
        TerrainLodBand::bounded(3, 1, bounded_vertical_policy(1, 1)),
        TerrainLodBand::bounded(2, 1, bounded_vertical_policy(1, 1)),
        TerrainLodBand::bounded(1, 1, bounded_vertical_policy(1, 1)),
        TerrainLodBand::bounded(0, 1, bounded_vertical_policy(1, 1)),
    ]
}

fn bounded_vertical_policy(
    below_player_nodes: i32,
    above_player_nodes: i32,
) -> TerrainLodBoundedVerticalPolicy {
    TerrainLodBoundedVerticalPolicy::new(below_player_nodes, above_player_nodes)
        .expect("default terrain vertical windows should be valid")
}

fn browser_lod_status(
    status: CoreTerrainLodStatus,
    visible_nodes: &BTreeSet<TerrainNodeKey>,
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

fn visible_world_span(nodes: &BTreeSet<TerrainNodeKey>, base_cell_size: f64) -> (f64, f64) {
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

fn node_contains_xz(key: TerrainNodeKey, base_cell_size: f64, x: f32, z: f32) -> bool {
    let node_size =
        terrain_node_cell_size(base_cell_size, key.lod) * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let min_x = key.coord.x as f64 * node_size;
    let min_z = key.coord.z as f64 * node_size;
    let max_x = min_x + node_size;
    let max_z = min_z + node_size;
    let x = f64::from(x);
    let z = f64::from(z);
    x >= min_x - 0.001 && x <= max_x + 0.001 && z >= min_z - 0.001 && z <= max_z + 0.001
}

fn hierarchy_conflicts_with_visible(
    key: TerrainNodeKey,
    desired_visible: &BTreeSet<TerrainNodeKey>,
    desired_visible_ancestors: &BTreeSet<TerrainNodeKey>,
) -> bool {
    let mut ancestor = terrain_node_parent(key);
    while let Some(parent) = ancestor {
        if desired_visible.contains(&parent) {
            return true;
        }
        ancestor = terrain_node_parent(parent);
    }

    desired_visible_ancestors.contains(&key)
}

fn visible_ancestor_set(visible_nodes: &BTreeSet<TerrainNodeKey>) -> BTreeSet<TerrainNodeKey> {
    let mut ancestors = BTreeSet::new();

    for key in visible_nodes {
        let mut ancestor = terrain_node_parent(*key);
        while let Some(parent) = ancestor {
            ancestors.insert(parent);
            ancestor = terrain_node_parent(parent);
        }
    }

    ancestors
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
