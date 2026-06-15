// Terrain stream scheduling for the rootless multi-resolution LOD grid.
//
// The scheduler owns generated node lifetime. It deliberately treats one terrain
// node build as the unit of work: density sampling is an internal meshing detail,
// while the stream only cares whether a node is missing, building, generated
// with mesh, or generated empty. This keeps the parent/child hierarchy explicit.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::{Mutex, OnceLock};

use crate::*;

#[allow(dead_code)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainChunkStage {
    NotPresent,
    BuildInFlight { lod: u8, generation: u64 },
    MeshReady { lod: u8 },
    MeshEmpty { lod: u8 },
}

#[derive(Default)]
pub struct TerrainStreamScheduler {
    config: TerrainStreamConfig,
    generation: u64,
    center_coord: Option<TerrainChunkCoord>,
    desired_nodes: BTreeSet<TerrainNodeKey>,
    nodes: BTreeMap<TerrainNodeKey, TerrainNodeRecord>,
}

pub(crate) static TERRAIN_STREAM_SCHEDULER: OnceLock<Mutex<TerrainStreamScheduler>> =
    OnceLock::new();

pub(crate) fn terrain_stream_scheduler() -> &'static Mutex<TerrainStreamScheduler> {
    TERRAIN_STREAM_SCHEDULER.get_or_init(|| Mutex::new(TerrainStreamScheduler::default()))
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TerrainNodeRecord {
    stage: NodeStage,
}

impl Default for TerrainNodeRecord {
    fn default() -> Self {
        Self {
            stage: NodeStage::Missing,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum NodeStage {
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
            desired_nodes: BTreeSet::new(),
            nodes: BTreeMap::new(),
        })
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    pub fn set_max_in_flight_jobs(
        &mut self,
        max_in_flight_jobs: usize,
    ) -> Result<(), TerrainStreamError> {
        if max_in_flight_jobs == 0 {
            return Err(TerrainStreamError::ZeroMaxInFlightJobs);
        }

        self.config.max_in_flight_jobs = max_in_flight_jobs;
        Ok(())
    }

    /// Updates the terrain data used by bounded vertical policies.
    pub fn set_terrain_context(
        &mut self,
        terrain_seed: u32,
        terrain_variant: TerrainVariantDescriptor,
        base_cell_size: f64,
    ) -> Result<(), TerrainStreamError> {
        let mut config = self.config.clone();
        config.terrain_seed = terrain_seed;
        config.terrain_variant = terrain_variant;
        config.base_cell_size = base_cell_size;
        validate_stream_config(&config)?;
        self.config = config;
        Ok(())
    }

    pub fn sync_center(&mut self, center_coord: TerrainChunkCoord) {
        self.center_coord = Some(center_coord);
        self.desired_nodes = self.build_desired_nodes(center_coord);
        self.prune_outside_desired_hierarchy();
    }

    pub fn reset(&mut self, center_coord: TerrainChunkCoord) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_nodes.clear();
        self.nodes.clear();
        self.sync_center(center_coord);
    }

    pub fn invalidate_all(&mut self) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_nodes.clear();
        self.nodes.clear();
    }

    pub fn tick(&mut self) -> Vec<TerrainStreamJob> {
        let mut jobs = Vec::new();

        while self.active_job_count() < self.config.max_in_flight_jobs {
            let Some(key) = self.next_build_job_key() else {
                break;
            };

            self.record_mut(key).stage = NodeStage::InFlight {
                generation: self.generation,
            };
            jobs.push(TerrainStreamJob::BuildNode {
                generation: self.generation,
                key,
            });
        }

        jobs
    }

    pub fn complete_node(&mut self, generation: u64, key: TerrainNodeKey, empty: bool) -> bool {
        if generation != self.generation || !self.desired_nodes.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.stage != (NodeStage::InFlight { generation }) {
            return false;
        }

        record.stage = if empty {
            NodeStage::Empty
        } else {
            NodeStage::Ready
        };
        true
    }

    pub fn fail_node(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        if generation != self.generation || !self.desired_nodes.contains(&key) {
            return false;
        }

        let record = self.record_mut(key);
        if record.stage != (NodeStage::InFlight { generation }) {
            return false;
        }

        record.stage = NodeStage::Missing;
        true
    }

    pub fn complete_density(&mut self, _generation: u64, _key: TerrainNodeKey) -> bool {
        false
    }

    pub fn fail_density(&mut self, _generation: u64, _key: TerrainNodeKey) -> bool {
        false
    }

    pub fn complete_mesh(&mut self, generation: u64, key: TerrainNodeKey, empty: bool) -> bool {
        self.complete_node(generation, key, empty)
    }

    pub fn fail_mesh(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        self.fail_node(generation, key)
    }

    pub fn complete_lod0(
        &mut self,
        generation: u64,
        coord: TerrainChunkCoord,
        empty: bool,
    ) -> bool {
        self.complete_node(generation, TerrainNodeKey::lod0(coord), empty)
    }

    pub fn fail_lod0(&mut self, generation: u64, coord: TerrainChunkCoord) -> bool {
        self.fail_node(generation, TerrainNodeKey::lod0(coord))
    }

    #[allow(dead_code)]
    pub(crate) fn node_stage(&self, key: TerrainNodeKey) -> TerrainChunkStage {
        let Some(record) = self.nodes.get(&key) else {
            return TerrainChunkStage::NotPresent;
        };

        match record.stage {
            NodeStage::Missing => TerrainChunkStage::NotPresent,
            NodeStage::InFlight { generation } => TerrainChunkStage::BuildInFlight {
                lod: key.lod,
                generation,
            },
            NodeStage::Ready => TerrainChunkStage::MeshReady { lod: key.lod },
            NodeStage::Empty => TerrainChunkStage::MeshEmpty { lod: key.lod },
        }
    }

    #[allow(dead_code)]
    pub(crate) fn chunk_stage(&self, coord: TerrainChunkCoord) -> TerrainChunkStage {
        self.node_stage(TerrainNodeKey::lod0(coord))
    }

    pub fn desired_density_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_mesh_nodes()
    }

    pub fn desired_mesh_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_nodes.iter().copied().collect()
    }

    pub fn desired_density_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_lod0_coords()
    }

    pub fn desired_lod0_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_nodes
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
        self.desired_nodes
            .iter()
            .copied()
            .filter(|key| self.node_ready(*key))
            .collect()
    }

    pub fn mesh_generated(&self, key: TerrainNodeKey) -> bool {
        matches!(
            self.node_stage_raw(key),
            Some(NodeStage::Ready | NodeStage::Empty)
        )
    }

    pub fn status(&self) -> TerrainStreamStatus {
        let in_flight_count = self
            .nodes
            .values()
            .filter(|record| matches!(record.stage, NodeStage::InFlight { .. }))
            .count();
        let ready_count = self
            .desired_nodes
            .iter()
            .filter(|key| self.node_stage_raw(**key) == Some(NodeStage::Ready))
            .count();
        let empty_count = self
            .desired_nodes
            .iter()
            .filter(|key| self.node_stage_raw(**key) == Some(NodeStage::Empty))
            .count();
        let generated_count = ready_count + empty_count;
        let lod0_ready_count = self
            .desired_nodes
            .iter()
            .filter(|key| key.lod == 0 && self.node_stage_raw(**key) == Some(NodeStage::Ready))
            .count();
        let lod0_empty_count = self
            .desired_nodes
            .iter()
            .filter(|key| key.lod == 0 && self.node_stage_raw(**key) == Some(NodeStage::Empty))
            .count();
        let missing_lod0_count = self
            .desired_nodes
            .iter()
            .filter(|key| key.lod == 0 && self.should_submit_node(**key))
            .count();
        let missing_mesh_count = self
            .desired_nodes
            .iter()
            .filter(|key| self.should_submit_node(**key))
            .count();

        TerrainStreamStatus {
            generation: self.generation,
            desired_density_count: self.desired_nodes.len(),
            desired_lod0_count: self.desired_lod0_coords().len(),
            desired_mesh_count: self.desired_nodes.len(),
            density_ready_count: generated_count,
            lod0_ready_count,
            lod0_empty_count,
            mesh_ready_count: ready_count,
            mesh_empty_count: empty_count,
            in_flight_density_count: 0,
            in_flight_lod_count: in_flight_count,
            missing_density_count: 0,
            missing_lod0_count,
            missing_mesh_count,
            max_in_flight_jobs: self.config.max_in_flight_jobs,
            lod_summaries: self.lod_summaries(),
        }
    }

    pub fn pending(&self) -> bool {
        self.nodes
            .values()
            .any(|record| matches!(record.stage, NodeStage::InFlight { .. }))
            || self
                .desired_nodes
                .iter()
                .any(|key| self.should_submit_node(*key))
    }

    fn build_desired_nodes(&self, center_coord: TerrainChunkCoord) -> BTreeSet<TerrainNodeKey> {
        let mut nodes = BTreeSet::new();

        for band in &self.config.lod_bands {
            let lod_center = terrain_node_coord_for_lod(center_coord, band.lod);
            for z in lod_center.z - band.horizontal_radius..=lod_center.z + band.horizontal_radius {
                for x in
                    lod_center.x - band.horizontal_radius..=lod_center.x + band.horizontal_radius
                {
                    match &band.vertical {
                        TerrainLodVerticalPolicy::FixedOffsets(vertical_offsets) => {
                            for offset in vertical_offsets {
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
                        TerrainLodVerticalPolicy::Bounded(policy) => {
                            let column = TerrainNodeColumnKey {
                                lod: band.lod,
                                x,
                                z,
                            };
                            if let Some(y_range) =
                                self.resolve_column_node_y_range(column, lod_center.y, *policy)
                            {
                                for y in y_range.iter() {
                                    nodes.insert(column.with_y(y));
                                }
                            }
                        }
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
        close_refined_sibling_groups(&mut nodes);

        nodes
    }

    fn resolve_column_node_y_range(
        &self,
        column: TerrainNodeColumnKey,
        player_node_y: i32,
        policy: TerrainLodBoundedVerticalPolicy,
    ) -> Option<TerrainNodeYRange> {
        let terrain_world_range = estimate_terrain_column_world_y_range(
            self.config.terrain_seed,
            self.config.terrain_variant,
            column,
            self.config.base_cell_size,
            policy.bounds_config(),
        )
        .ok()?;
        let terrain_node_range = terrain_world_y_range_to_node_y_range(
            terrain_world_range,
            column.lod,
            self.config.base_cell_size,
        )?;
        let player_window = policy.vertical_window()?.node_range_around(player_node_y);

        terrain_node_range.intersect(player_window)
    }

    fn next_build_job_key(&self) -> Option<TerrainNodeKey> {
        let center_coord = self.center_coord?;

        self.desired_nodes
            .iter()
            .copied()
            .filter(|key| self.should_submit_node(*key))
            .min_by_key(|key| node_priority(*key, center_coord))
    }

    fn should_submit_node(&self, key: TerrainNodeKey) -> bool {
        if !self.desired_nodes.contains(&key) || !self.parent_generated(key) {
            return false;
        }

        !matches!(
            self.node_stage_raw(key),
            Some(NodeStage::InFlight { .. } | NodeStage::Ready | NodeStage::Empty)
        )
    }

    fn parent_generated(&self, key: TerrainNodeKey) -> bool {
        let Some(parent) = terrain_node_parent(key) else {
            return true;
        };
        if !self.desired_nodes.contains(&parent) {
            return true;
        }

        self.mesh_generated(parent)
    }

    fn active_job_count(&self) -> usize {
        self.nodes
            .values()
            .filter(|record| matches!(record.stage, NodeStage::InFlight { .. }))
            .count()
    }

    fn node_ready(&self, key: TerrainNodeKey) -> bool {
        self.node_stage_raw(key) == Some(NodeStage::Ready)
    }

    fn node_stage_raw(&self, key: TerrainNodeKey) -> Option<NodeStage> {
        self.nodes.get(&key).map(|record| record.stage)
    }

    fn record_mut(&mut self, key: TerrainNodeKey) -> &mut TerrainNodeRecord {
        self.nodes.entry(key).or_default()
    }

    fn prune_outside_desired_hierarchy(&mut self) {
        let desired_nodes = &self.desired_nodes;

        self.nodes
            .retain(|key, _record| desired_nodes.contains(key));
    }

    fn lod_summaries(&self) -> Vec<TerrainLodStatus> {
        let lods = self
            .desired_nodes
            .iter()
            .map(|key| key.lod)
            .collect::<BTreeSet<_>>();

        lods.into_iter()
            .map(|lod| {
                let desired_nodes = self
                    .desired_nodes
                    .iter()
                    .filter(|key| key.lod == lod)
                    .collect::<Vec<_>>();
                let ready_count = desired_nodes
                    .iter()
                    .filter(|key| self.node_stage_raw(***key) == Some(NodeStage::Ready))
                    .count();
                let empty_count = desired_nodes
                    .iter()
                    .filter(|key| self.node_stage_raw(***key) == Some(NodeStage::Empty))
                    .count();

                TerrainLodStatus {
                    lod,
                    desired_node_count: desired_nodes.len(),
                    min_desired_node_y: desired_nodes.iter().map(|key| key.coord.y).min(),
                    max_desired_node_y: desired_nodes.iter().map(|key| key.coord.y).max(),
                    density_ready_node_count: ready_count + empty_count,
                    rendered_node_count: ready_count,
                    empty_node_count: empty_count,
                    missing_node_count: desired_nodes
                        .iter()
                        .filter(|key| self.should_submit_node(***key))
                        .count(),
                }
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

fn close_refined_sibling_groups(nodes: &mut BTreeSet<TerrainNodeKey>) {
    loop {
        let mut changed = false;
        let current = nodes.iter().copied().collect::<Vec<_>>();
        for key in current {
            let Some(parent) = terrain_node_parent(key) else {
                continue;
            };
            if !nodes.contains(&parent) {
                continue;
            }
            let Some(children) = terrain_node_children(parent) else {
                continue;
            };

            for child in children {
                changed |= nodes.insert(child);
            }
        }

        if !changed {
            break;
        }
    }
}
