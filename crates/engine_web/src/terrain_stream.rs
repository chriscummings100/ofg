// Owns the browser game's terrain stream state inside Rust. This module keeps
// TypeScript out of terrain scheduling and mesh packet semantics; the wasm
// facade only exposes debug snapshots after meshes have been generated and
// uploaded by Rust.

use std::collections::{BTreeMap, BTreeSet};

use engine_core::Vec3;
use terrain_core::{
    build_node_mesh, terrain_chunk_coord_containing_position, terrain_chunk_key,
    terrain_node_children, terrain_node_key, terrain_node_parent, MeshData, TerrainChunkCoord,
    TerrainLodBand, TerrainLodStatus as CoreTerrainLodStatus, TerrainNodeKey, TerrainStreamConfig,
    TerrainStreamError, TerrainStreamJob, TerrainStreamScheduler,
    TerrainStreamStatus as CoreTerrainStreamStatus,
};

const DEFAULT_TERRAIN_HORIZONTAL_RADIUS: i32 = 1;
const DEFAULT_TERRAIN_VERTICAL_OFFSETS: [i32; 4] = [-2, -1, 0, 1];
const DEFAULT_TERRAIN_CELL_SIZE: f64 = 1.0;
const DEFAULT_TERRAIN_MAX_JOBS_PER_TICK: usize = 6;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainJobStats {
    pub total_ms: f64,
    pub vertex_count: usize,
    pub index_count: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrowserTerrainLodStatus {
    pub lod: u8,
    pub desired_node_count: usize,
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
    pub lod_summaries: Vec<BrowserTerrainLodStatus>,
    pub max_concurrent_chunk_jobs: usize,
    pub last_density_job_stats: Option<TerrainJobStats>,
    pub last_chunk_job_stats: Option<TerrainJobStats>,
}

pub struct BrowserTerrainMeshUpdate {
    pub key: TerrainNodeKey,
    pub mesh: MeshData,
}

#[derive(Default)]
pub struct BrowserTerrainStreamUpdate {
    pub removed_nodes: Vec<TerrainNodeKey>,
    pub upserted_meshes: Vec<BrowserTerrainMeshUpdate>,
}

pub struct BrowserTerrainStream {
    seed: u32,
    preset: u32,
    cell_size: f64,
    scheduler: TerrainStreamScheduler,
    last_center_coord: Option<TerrainChunkCoord>,
    mesh_cache: BTreeMap<TerrainNodeKey, MeshData>,
    visible_nodes: BTreeSet<TerrainNodeKey>,
    last_density_job_stats: Option<TerrainJobStats>,
    last_chunk_job_stats: Option<TerrainJobStats>,
}

impl BrowserTerrainStream {
    pub fn new(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        Self::new_with_lod_bands(seed, preset, default_terrain_lod_bands())
    }

    pub fn new_lod0(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        Self::new_with_lod_bands(seed, preset, lod0_terrain_lod_bands())
    }

    pub fn new_with_lod_bands(
        seed: u32,
        preset: u32,
        lod_bands: Vec<TerrainLodBand>,
    ) -> Result<Self, TerrainStreamError> {
        let scheduler = TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands,
            max_in_flight_jobs: DEFAULT_TERRAIN_MAX_JOBS_PER_TICK,
        })?;

        Ok(Self {
            seed,
            preset,
            cell_size: DEFAULT_TERRAIN_CELL_SIZE,
            scheduler,
            last_center_coord: None,
            mesh_cache: BTreeMap::new(),
            visible_nodes: BTreeSet::new(),
            last_density_job_stats: None,
            last_chunk_job_stats: None,
        })
    }

    pub fn reset_game(&mut self, seed: u32, preset: u32, center: Vec3) -> Vec<TerrainNodeKey> {
        self.seed = seed;
        self.preset = preset;
        self.reset_around(center)
    }

    pub fn reset_around(&mut self, center: Vec3) -> Vec<TerrainNodeKey> {
        let removed_nodes = self.visible_nodes.iter().copied().collect::<Vec<_>>();
        self.mesh_cache.clear();
        self.visible_nodes.clear();
        self.last_density_job_stats = None;
        self.last_chunk_job_stats = None;

        let center_coord = self.coord_containing_position(center);
        self.scheduler.reset(center_coord);
        self.last_center_coord = Some(center_coord);

        removed_nodes
    }

    pub fn tick(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        let mut update = BrowserTerrainStreamUpdate::default();
        self.sync_around(center, &mut update);

        for job in self.scheduler.tick() {
            match job {
                TerrainStreamJob::Density { generation, key } => {
                    if self.scheduler.complete_density(generation, key) {
                        self.last_density_job_stats = Some(TerrainJobStats {
                            total_ms: 0.0,
                            vertex_count: 0,
                            index_count: 0,
                        });
                    }
                }
                TerrainStreamJob::Mesh { generation, key } => {
                    self.complete_mesh_job(generation, key);
                }
            }
        }

        self.sync_visible_meshes(&mut update);

        update
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

        BrowserTerrainStreamStatus {
            generation: status.generation,
            pending: is_stream_pending(&status),
            loaded_chunk_count: status.desired_density_count,
            density_ready_chunk_count: status.density_ready_count,
            shared_density_chunk_count: status.density_ready_count,
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
            lod_summaries: status
                .lod_summaries
                .into_iter()
                .map(|summary| browser_lod_status(summary, &self.visible_nodes))
                .collect(),
            max_concurrent_chunk_jobs: status.max_in_flight_jobs,
            last_density_job_stats: self.last_density_job_stats,
            last_chunk_job_stats: self.last_chunk_job_stats,
        }
    }

    pub fn worker_count(&self) -> usize {
        self.scheduler.status().max_in_flight_jobs
    }

    fn sync_around(&mut self, center: Vec3, update: &mut BrowserTerrainStreamUpdate) {
        let center_coord = self.coord_containing_position(center);
        if self.last_center_coord != Some(center_coord) {
            self.scheduler.sync_center(center_coord);
            self.last_center_coord = Some(center_coord);
        }

        self.retain_desired_meshes(update);
    }

    fn retain_desired_meshes(&mut self, update: &mut BrowserTerrainStreamUpdate) {
        let desired = self
            .scheduler
            .desired_mesh_nodes()
            .into_iter()
            .collect::<BTreeSet<_>>();
        let removed = self
            .visible_nodes
            .iter()
            .filter(|key| !desired.contains(key))
            .copied()
            .collect::<Vec<_>>();

        for key in &removed {
            self.visible_nodes.remove(key);
            update.removed_nodes.push(*key);
        }

        self.mesh_cache.retain(|key, _mesh| desired.contains(key));
    }

    fn complete_mesh_job(&mut self, generation: u64, key: TerrainNodeKey) {
        let mesh = build_node_mesh(self.seed, self.preset, key, self.cell_size);
        let empty = mesh.indices.is_empty();
        if !self.scheduler.complete_mesh(generation, key, empty) {
            return;
        }

        self.last_chunk_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: mesh.vertices.len(),
            index_count: mesh.indices.len(),
        });

        if empty {
            self.mesh_cache.remove(&key);
            return;
        }

        self.mesh_cache.insert(key, mesh);
    }

    fn sync_visible_meshes(&mut self, update: &mut BrowserTerrainStreamUpdate) {
        let desired_visible = self.select_visible_nodes();
        let removed = self
            .visible_nodes
            .iter()
            .filter(|key| !desired_visible.contains(key))
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
                mesh: mesh.clone(),
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
}

fn default_terrain_lod_bands() -> Vec<TerrainLodBand> {
    vec![
        TerrainLodBand {
            lod: 0,
            horizontal_radius: DEFAULT_TERRAIN_HORIZONTAL_RADIUS,
            vertical_chunk_offsets: DEFAULT_TERRAIN_VERTICAL_OFFSETS.to_vec(),
        },
        TerrainLodBand {
            lod: 1,
            horizontal_radius: 2,
            vertical_chunk_offsets: vec![-1, 0, 1],
        },
        TerrainLodBand {
            lod: 2,
            horizontal_radius: 4,
            vertical_chunk_offsets: vec![0],
        },
    ]
}

fn lod0_terrain_lod_bands() -> Vec<TerrainLodBand> {
    vec![TerrainLodBand {
        lod: 0,
        horizontal_radius: DEFAULT_TERRAIN_HORIZONTAL_RADIUS,
        vertical_chunk_offsets: DEFAULT_TERRAIN_VERTICAL_OFFSETS.to_vec(),
    }]
}

fn browser_lod_status(
    status: CoreTerrainLodStatus,
    visible_nodes: &BTreeSet<TerrainNodeKey>,
) -> BrowserTerrainLodStatus {
    BrowserTerrainLodStatus {
        lod: status.lod,
        desired_node_count: status.desired_node_count,
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
