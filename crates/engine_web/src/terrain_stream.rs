// Owns the browser game's terrain stream state inside Rust. This module keeps
// TypeScript out of terrain scheduling and mesh packet semantics; the wasm
// facade only exposes debug snapshots after meshes have been generated and
// uploaded by Rust.

use std::collections::BTreeSet;

use engine_core::Vec3;
use terrain_core::{
    build_chunk_mesh, terrain_chunk_coord_containing_position, terrain_chunk_key, MeshData,
    TerrainChunkCoord, TerrainStreamConfig, TerrainStreamError, TerrainStreamJob,
    TerrainStreamScheduler, TerrainStreamStatus as CoreTerrainStreamStatus,
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
    pub max_concurrent_chunk_jobs: usize,
    pub last_density_job_stats: Option<TerrainJobStats>,
    pub last_chunk_job_stats: Option<TerrainJobStats>,
}

pub struct BrowserTerrainMeshUpdate {
    pub coord: TerrainChunkCoord,
    pub mesh: MeshData,
}

#[derive(Default)]
pub struct BrowserTerrainStreamUpdate {
    pub removed_coords: Vec<TerrainChunkCoord>,
    pub upserted_meshes: Vec<BrowserTerrainMeshUpdate>,
}

pub struct BrowserTerrainStream {
    seed: u32,
    preset: u32,
    cell_size: f64,
    scheduler: TerrainStreamScheduler,
    last_center_coord: Option<TerrainChunkCoord>,
    mesh_coords: BTreeSet<TerrainChunkCoord>,
    last_density_job_stats: Option<TerrainJobStats>,
    last_chunk_job_stats: Option<TerrainJobStats>,
}

impl BrowserTerrainStream {
    pub fn new(seed: u32, preset: u32) -> Result<Self, TerrainStreamError> {
        let scheduler = TerrainStreamScheduler::new(TerrainStreamConfig {
            horizontal_radius: DEFAULT_TERRAIN_HORIZONTAL_RADIUS,
            vertical_chunk_offsets: DEFAULT_TERRAIN_VERTICAL_OFFSETS.to_vec(),
            max_in_flight_jobs: DEFAULT_TERRAIN_MAX_JOBS_PER_TICK,
        })?;

        Ok(Self {
            seed,
            preset,
            cell_size: DEFAULT_TERRAIN_CELL_SIZE,
            scheduler,
            last_center_coord: None,
            mesh_coords: BTreeSet::new(),
            last_density_job_stats: None,
            last_chunk_job_stats: None,
        })
    }

    pub fn reset_game(&mut self, seed: u32, preset: u32, center: Vec3) -> Vec<TerrainChunkCoord> {
        self.seed = seed;
        self.preset = preset;
        self.reset_around(center)
    }

    pub fn reset_around(&mut self, center: Vec3) -> Vec<TerrainChunkCoord> {
        let removed_coords = self.mesh_coords.iter().copied().collect::<Vec<_>>();
        self.mesh_coords.clear();
        self.last_density_job_stats = None;
        self.last_chunk_job_stats = None;

        let center_coord = self.coord_containing_position(center);
        self.scheduler.reset(center_coord);
        self.last_center_coord = Some(center_coord);

        removed_coords
    }

    pub fn tick(&mut self, center: Vec3) -> BrowserTerrainStreamUpdate {
        let mut update = BrowserTerrainStreamUpdate::default();
        update
            .removed_coords
            .extend(self.sync_around(center).into_iter());

        for job in self.scheduler.tick() {
            match job {
                TerrainStreamJob::Density { generation, coord } => {
                    if self.scheduler.complete_density(generation, coord) {
                        self.last_density_job_stats = Some(TerrainJobStats {
                            total_ms: 0.0,
                            vertex_count: 0,
                            index_count: 0,
                        });
                    }
                }
                TerrainStreamJob::Lod {
                    generation,
                    lod,
                    coord,
                } => {
                    if lod == 0 {
                        self.complete_lod0_job(generation, coord, &mut update);
                    }
                }
            }
        }

        update
    }

    pub fn loaded_chunk_keys(&self) -> Vec<String> {
        self.scheduler
            .desired_density_coords()
            .into_iter()
            .map(terrain_chunk_key)
            .collect()
    }

    pub fn render_chunk_keys(&self) -> Vec<String> {
        self.mesh_coords
            .iter()
            .copied()
            .map(terrain_chunk_key)
            .collect()
    }

    pub fn status(&self) -> BrowserTerrainStreamStatus {
        let status = self.scheduler.status();

        BrowserTerrainStreamStatus {
            generation: status.generation,
            pending: is_stream_pending(status),
            loaded_chunk_count: status.desired_density_count,
            density_ready_chunk_count: status.density_ready_count,
            shared_density_chunk_count: status.density_ready_count,
            in_flight_density_count: status.in_flight_density_count,
            missing_density_count: status.missing_density_count,
            desired_render_chunk_count: status.desired_lod0_count,
            rendered_chunk_count: status.lod0_ready_count,
            empty_chunk_count: status.lod0_empty_count,
            in_flight_chunk_count: status.in_flight_lod_count,
            missing_chunk_count: status.missing_lod0_count,
            max_concurrent_chunk_jobs: status.max_in_flight_jobs,
            last_density_job_stats: self.last_density_job_stats,
            last_chunk_job_stats: self.last_chunk_job_stats,
        }
    }

    pub fn worker_count(&self) -> usize {
        self.scheduler.status().max_in_flight_jobs
    }

    fn sync_around(&mut self, center: Vec3) -> Vec<TerrainChunkCoord> {
        let center_coord = self.coord_containing_position(center);
        if self.last_center_coord != Some(center_coord) {
            self.scheduler.sync_center(center_coord);
            self.last_center_coord = Some(center_coord);
        }

        self.retain_desired_meshes()
    }

    fn retain_desired_meshes(&mut self) -> Vec<TerrainChunkCoord> {
        let desired = self
            .scheduler
            .desired_lod0_coords()
            .into_iter()
            .collect::<BTreeSet<_>>();
        let removed = self
            .mesh_coords
            .iter()
            .filter(|coord| !desired.contains(coord))
            .copied()
            .collect::<Vec<_>>();

        for coord in &removed {
            self.mesh_coords.remove(coord);
        }

        removed
    }

    fn complete_lod0_job(
        &mut self,
        generation: u64,
        coord: TerrainChunkCoord,
        update: &mut BrowserTerrainStreamUpdate,
    ) {
        let mesh = build_chunk_mesh(self.seed, self.preset, coord, self.cell_size);
        let empty = mesh.indices.is_empty();
        if !self.scheduler.complete_lod0(generation, coord, empty) {
            return;
        }

        self.last_chunk_job_stats = Some(TerrainJobStats {
            total_ms: 0.0,
            vertex_count: mesh.vertices.len(),
            index_count: mesh.indices.len(),
        });

        if empty {
            if self.mesh_coords.remove(&coord) {
                update.removed_coords.push(coord);
            }
            return;
        }

        self.mesh_coords.insert(coord);
        update
            .upserted_meshes
            .push(BrowserTerrainMeshUpdate { coord, mesh });
    }

    fn coord_containing_position(&self, position: Vec3) -> TerrainChunkCoord {
        terrain_chunk_coord_containing_position(position.x, position.y, position.z, self.cell_size)
    }
}

fn is_stream_pending(status: CoreTerrainStreamStatus) -> bool {
    status.in_flight_density_count > 0
        || status.in_flight_lod_count > 0
        || status.missing_density_count > 0
        || status.missing_lod0_count > 0
}
