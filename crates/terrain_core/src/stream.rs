// Terrain stream scheduling for the rootless multi-resolution LOD grid.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::{Mutex, OnceLock};

use crate::*;

#[allow(dead_code)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainChunkStage {
    NotPresent,
    DensityInFlight { generation: u64 },
    DensityReady,
    MeshInFlight { lod: u8, generation: u64 },
    MeshReady { lod: u8 },
    MeshEmpty { lod: u8 },
}

#[derive(Default)]
pub struct TerrainStreamScheduler {
    config: TerrainStreamConfig,
    generation: u64,
    center_coord: Option<TerrainChunkCoord>,
    desired_density: BTreeSet<TerrainNodeKey>,
    desired_mesh: BTreeSet<TerrainNodeKey>,
    nodes: BTreeMap<TerrainNodeKey, TerrainNodeRecord>,
}

pub(crate) static TERRAIN_STREAM_SCHEDULER: OnceLock<Mutex<TerrainStreamScheduler>> =
    OnceLock::new();

pub(crate) fn terrain_stream_scheduler() -> &'static Mutex<TerrainStreamScheduler> {
    TERRAIN_STREAM_SCHEDULER.get_or_init(|| Mutex::new(TerrainStreamScheduler::default()))
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TerrainNodeRecord {
    density: DensityStage,
    mesh: MeshStage,
}

impl Default for TerrainNodeRecord {
    fn default() -> Self {
        Self {
            density: DensityStage::Missing,
            mesh: MeshStage::Missing,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DensityStage {
    Missing,
    InFlight { generation: u64 },
    Ready,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MeshStage {
    Missing,
    InFlight { generation: u64 },
    Ready,
    Empty,
}

impl TerrainStreamScheduler {
    pub fn new(config: TerrainStreamConfig) -> Result<Self, TerrainStreamError> {
        validate_stream_config(&config)?;

        Ok(Self {
            config,
            generation: 0,
            center_coord: None,
            desired_density: BTreeSet::new(),
            desired_mesh: BTreeSet::new(),
            nodes: BTreeMap::new(),
        })
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    pub fn sync_center(&mut self, center_coord: TerrainChunkCoord) {
        self.center_coord = Some(center_coord);
        self.desired_mesh = self.build_desired_mesh_nodes(center_coord);
        self.desired_density = self
            .desired_mesh
            .iter()
            .flat_map(|key| self.density_dependencies(*key))
            .collect();
        self.prune_outside_desired_sets();
    }

    pub fn reset(&mut self, center_coord: TerrainChunkCoord) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_density.clear();
        self.desired_mesh.clear();
        self.nodes.clear();
        self.sync_center(center_coord);
    }

    pub fn invalidate_all(&mut self) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_density.clear();
        self.desired_mesh.clear();
        self.nodes.clear();
    }

    pub fn tick(&mut self) -> Vec<TerrainStreamJob> {
        let mut jobs = Vec::new();

        while self.active_job_count() < self.config.max_in_flight_jobs {
            if let Some(key) = self.next_density_job_key() {
                self.record_mut(key).density = DensityStage::InFlight {
                    generation: self.generation,
                };
                jobs.push(TerrainStreamJob::Density {
                    generation: self.generation,
                    key,
                });
                continue;
            }

            if let Some(key) = self.next_mesh_job_key() {
                self.record_mut(key).mesh = MeshStage::InFlight {
                    generation: self.generation,
                };
                jobs.push(TerrainStreamJob::Mesh {
                    generation: self.generation,
                    key,
                });
                continue;
            }

            break;
        }

        jobs
    }

    pub fn complete_density(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        if generation != self.generation || !self.desired_density.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.density != (DensityStage::InFlight { generation }) {
            return false;
        }

        record.density = DensityStage::Ready;
        true
    }

    pub fn fail_density(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        if generation != self.generation || !self.desired_density.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.density != (DensityStage::InFlight { generation }) {
            return false;
        }

        record.density = DensityStage::Missing;
        true
    }

    pub fn complete_mesh(&mut self, generation: u64, key: TerrainNodeKey, empty: bool) -> bool {
        if generation != self.generation || !self.desired_mesh.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.mesh != (MeshStage::InFlight { generation }) {
            return false;
        }

        record.mesh = if empty {
            MeshStage::Empty
        } else {
            MeshStage::Ready
        };
        true
    }

    pub fn fail_mesh(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        if generation != self.generation || !self.desired_mesh.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.mesh != (MeshStage::InFlight { generation }) {
            return false;
        }

        record.mesh = MeshStage::Missing;
        true
    }

    pub fn complete_lod0(
        &mut self,
        generation: u64,
        coord: TerrainChunkCoord,
        empty: bool,
    ) -> bool {
        self.complete_mesh(generation, TerrainNodeKey::lod0(coord), empty)
    }

    pub fn fail_lod0(&mut self, generation: u64, coord: TerrainChunkCoord) -> bool {
        self.fail_mesh(generation, TerrainNodeKey::lod0(coord))
    }

    #[allow(dead_code)]
    pub(crate) fn node_stage(&self, key: TerrainNodeKey) -> TerrainChunkStage {
        let Some(record) = self.nodes.get(&key) else {
            return TerrainChunkStage::NotPresent;
        };

        match record.mesh {
            MeshStage::Ready => return TerrainChunkStage::MeshReady { lod: key.lod },
            MeshStage::Empty => return TerrainChunkStage::MeshEmpty { lod: key.lod },
            MeshStage::InFlight { generation } => {
                return TerrainChunkStage::MeshInFlight {
                    lod: key.lod,
                    generation,
                };
            }
            MeshStage::Missing => {}
        }

        match record.density {
            DensityStage::Missing => TerrainChunkStage::NotPresent,
            DensityStage::InFlight { generation } => {
                TerrainChunkStage::DensityInFlight { generation }
            }
            DensityStage::Ready => TerrainChunkStage::DensityReady,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn chunk_stage(&self, coord: TerrainChunkCoord) -> TerrainChunkStage {
        self.node_stage(TerrainNodeKey::lod0(coord))
    }

    pub fn desired_density_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_density.iter().copied().collect()
    }

    pub fn desired_mesh_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_mesh.iter().copied().collect()
    }

    pub fn desired_density_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_density
            .iter()
            .filter(|key| key.lod == 0)
            .map(|key| key.coord)
            .collect()
    }

    pub fn desired_lod0_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_mesh
            .iter()
            .filter(|key| key.lod == 0)
            .map(|key| key.coord)
            .collect()
    }

    pub fn density_dependencies(&self, key: TerrainNodeKey) -> Vec<TerrainNodeKey> {
        let mut keys = Vec::with_capacity(8);
        for z in key.coord.z..=key.coord.z + 1 {
            for y in key.coord.y..=key.coord.y + 1 {
                for x in key.coord.x..=key.coord.x + 1 {
                    keys.push(TerrainNodeKey {
                        lod: key.lod,
                        coord: TerrainChunkCoord { x, y, z },
                    });
                }
            }
        }

        keys
    }

    pub fn lod0_density_dependencies(&self, coord: TerrainChunkCoord) -> Vec<TerrainChunkCoord> {
        self.density_dependencies(TerrainNodeKey::lod0(coord))
            .into_iter()
            .map(|key| key.coord)
            .collect()
    }

    pub fn visible_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_mesh
            .iter()
            .copied()
            .filter(|key| {
                self.nodes
                    .get(key)
                    .is_some_and(|record| record.mesh == MeshStage::Ready)
            })
            .collect()
    }

    pub fn mesh_generated(&self, key: TerrainNodeKey) -> bool {
        matches!(
            self.mesh_stage(key),
            Some(MeshStage::Ready | MeshStage::Empty)
        )
    }

    pub fn status(&self) -> TerrainStreamStatus {
        let in_flight_density_count = self
            .nodes
            .values()
            .filter(|record| matches!(record.density, DensityStage::InFlight { .. }))
            .count();
        let in_flight_lod_count = self
            .nodes
            .values()
            .filter(|record| matches!(record.mesh, MeshStage::InFlight { .. }))
            .count();
        let density_ready_count = self
            .desired_density
            .iter()
            .filter(|key| self.density_stage(**key) == Some(DensityStage::Ready))
            .count();
        let lod0_ready_count = self
            .desired_mesh
            .iter()
            .filter(|key| key.lod == 0 && self.mesh_stage(**key) == Some(MeshStage::Ready))
            .count();
        let lod0_empty_count = self
            .desired_mesh
            .iter()
            .filter(|key| key.lod == 0 && self.mesh_stage(**key) == Some(MeshStage::Empty))
            .count();
        let mesh_ready_count = self
            .desired_mesh
            .iter()
            .filter(|key| self.mesh_stage(**key) == Some(MeshStage::Ready))
            .count();
        let mesh_empty_count = self
            .desired_mesh
            .iter()
            .filter(|key| self.mesh_stage(**key) == Some(MeshStage::Empty))
            .count();
        let missing_density_count = self
            .desired_density
            .iter()
            .filter(|key| self.should_submit_density(**key))
            .count();
        let missing_lod0_count = self
            .desired_mesh
            .iter()
            .filter(|key| key.lod == 0 && self.should_submit_mesh(**key))
            .count();
        let missing_mesh_count = self
            .desired_mesh
            .iter()
            .filter(|key| self.should_submit_mesh(**key))
            .count();

        TerrainStreamStatus {
            generation: self.generation,
            desired_density_count: self.desired_density.len(),
            desired_lod0_count: self.desired_lod0_coords().len(),
            desired_mesh_count: self.desired_mesh.len(),
            density_ready_count,
            lod0_ready_count,
            lod0_empty_count,
            mesh_ready_count,
            mesh_empty_count,
            in_flight_density_count,
            in_flight_lod_count,
            missing_density_count,
            missing_lod0_count,
            missing_mesh_count,
            max_in_flight_jobs: self.config.max_in_flight_jobs,
            lod_summaries: self.lod_summaries(),
        }
    }

    fn build_desired_mesh_nodes(
        &self,
        center_coord: TerrainChunkCoord,
    ) -> BTreeSet<TerrainNodeKey> {
        let mut nodes = BTreeSet::new();

        for band in &self.config.lod_bands {
            let lod_center = terrain_node_coord_for_lod(center_coord, band.lod);
            for z in lod_center.z - band.horizontal_radius..=lod_center.z + band.horizontal_radius {
                for x in
                    lod_center.x - band.horizontal_radius..=lod_center.x + band.horizontal_radius
                {
                    for offset in &band.vertical_chunk_offsets {
                        nodes.insert(TerrainNodeKey {
                            lod: band.lod,
                            coord: TerrainChunkCoord {
                                x,
                                y: lod_center.y + offset,
                                z,
                            },
                        });
                    }
                }
            }
        }

        let base_nodes = nodes.iter().copied().collect::<Vec<_>>();
        let max_lod = self.max_configured_lod();
        for key in base_nodes {
            let mut current = key;
            while current.lod < max_lod {
                let Some(parent) = terrain_node_parent(current) else {
                    break;
                };
                nodes.insert(parent);
                current = parent;
            }
        }

        nodes
    }

    fn next_density_job_key(&self) -> Option<TerrainNodeKey> {
        let center_coord = self.center_coord?;

        self.desired_density
            .iter()
            .copied()
            .filter(|key| self.should_submit_density(*key))
            .min_by_key(|key| node_priority(*key, center_coord))
    }

    fn next_mesh_job_key(&self) -> Option<TerrainNodeKey> {
        let center_coord = self.center_coord?;

        self.desired_mesh
            .iter()
            .copied()
            .filter(|key| self.should_submit_mesh(*key))
            .min_by_key(|key| node_priority(*key, center_coord))
    }

    fn should_submit_density(&self, key: TerrainNodeKey) -> bool {
        if !self.desired_density.contains(&key) || !self.parent_cover_generated(key) {
            return false;
        }

        !matches!(
            self.density_stage(key),
            Some(DensityStage::InFlight { .. } | DensityStage::Ready)
        )
    }

    fn should_submit_mesh(&self, key: TerrainNodeKey) -> bool {
        if !self.desired_mesh.contains(&key)
            || !self.parent_cover_generated(key)
            || !self.density_dependencies_ready(key)
        {
            return false;
        }

        !matches!(
            self.mesh_stage(key),
            Some(MeshStage::InFlight { .. } | MeshStage::Ready | MeshStage::Empty)
        )
    }

    fn density_dependencies_ready(&self, key: TerrainNodeKey) -> bool {
        self.density_dependencies(key)
            .iter()
            .all(|dependency| self.density_stage(*dependency) == Some(DensityStage::Ready))
    }

    fn parent_cover_generated(&self, key: TerrainNodeKey) -> bool {
        let Some(parent) = terrain_node_parent(key) else {
            return true;
        };
        if !self.desired_mesh.contains(&parent) {
            return true;
        }

        matches!(
            self.mesh_stage(parent),
            Some(MeshStage::Ready | MeshStage::Empty)
        )
    }

    fn active_job_count(&self) -> usize {
        self.nodes
            .values()
            .filter(|record| matches!(record.density, DensityStage::InFlight { .. }))
            .count()
            + self
                .nodes
                .values()
                .filter(|record| matches!(record.mesh, MeshStage::InFlight { .. }))
                .count()
    }

    fn density_stage(&self, key: TerrainNodeKey) -> Option<DensityStage> {
        self.nodes.get(&key).map(|record| record.density)
    }

    fn mesh_stage(&self, key: TerrainNodeKey) -> Option<MeshStage> {
        self.nodes.get(&key).map(|record| record.mesh)
    }

    fn record_mut(&mut self, key: TerrainNodeKey) -> &mut TerrainNodeRecord {
        self.nodes.entry(key).or_default()
    }

    fn prune_outside_desired_sets(&mut self) {
        let desired_density = &self.desired_density;
        let desired_mesh = &self.desired_mesh;

        self.nodes
            .retain(|key, _record| desired_density.contains(key) || desired_mesh.contains(key));
    }

    fn lod_summaries(&self) -> Vec<TerrainLodStatus> {
        let lods = self
            .desired_mesh
            .iter()
            .map(|key| key.lod)
            .collect::<BTreeSet<_>>();

        lods.into_iter()
            .map(|lod| TerrainLodStatus {
                lod,
                desired_node_count: self
                    .desired_mesh
                    .iter()
                    .filter(|key| key.lod == lod)
                    .count(),
                density_ready_node_count: self
                    .desired_density
                    .iter()
                    .filter(|key| key.lod == lod)
                    .filter(|key| self.density_stage(**key) == Some(DensityStage::Ready))
                    .count(),
                rendered_node_count: self
                    .desired_mesh
                    .iter()
                    .filter(|key| key.lod == lod)
                    .filter(|key| self.mesh_stage(**key) == Some(MeshStage::Ready))
                    .count(),
                empty_node_count: self
                    .desired_mesh
                    .iter()
                    .filter(|key| key.lod == lod)
                    .filter(|key| self.mesh_stage(**key) == Some(MeshStage::Empty))
                    .count(),
                missing_node_count: self
                    .desired_mesh
                    .iter()
                    .filter(|key| key.lod == lod)
                    .filter(|key| self.should_submit_mesh(**key))
                    .count(),
            })
            .collect()
    }

    fn max_configured_lod(&self) -> u8 {
        self.config
            .lod_bands
            .iter()
            .map(|band| band.lod)
            .max()
            .unwrap_or(0)
    }
}
