//! Minimal multi-LOD stream state for the terrain rebuild.
//!
//! The stream schedules whole-node jobs, keeps parent nodes visible until all
//! children are generated or empty, and records dissolve transitions as state.
//! It does not own water, collision, placement, aprons, or renderer resources.

use std::collections::{BTreeMap, BTreeSet};

use crate::node::{
    terrain_chunk_coord_containing_position, terrain_node_children, terrain_node_coord_for_lod,
    terrain_node_parent, TerrainChunkCoord, TerrainNodeKey, MAX_PLAYABLE_LOD,
};
use crate::variant::TerrainVariantDescriptor;

const DEFAULT_MAX_IN_FLIGHT_JOBS: usize = 4;
const DISSOLVE_TICKS: u8 = 8;

#[derive(Clone, Debug, PartialEq)]
pub struct TerrainStreamConfig {
    pub lod_bands: Vec<TerrainLodBand>,
    pub max_in_flight_jobs: usize,
    pub terrain_seed: u32,
    pub terrain_variant: TerrainVariantDescriptor,
    pub base_cell_size: f64,
}

impl Default for TerrainStreamConfig {
    fn default() -> Self {
        Self {
            lod_bands: default_lod_bands(),
            max_in_flight_jobs: DEFAULT_MAX_IN_FLIGHT_JOBS,
            terrain_seed: 0,
            terrain_variant: crate::variant::terrain_variant_for_preset(
                crate::DEFAULT_TERRAIN_PRESET,
            ),
            base_cell_size: 1.0,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainLodBand {
    pub lod: u8,
    pub horizontal_radius: i32,
    pub vertical: TerrainLodVerticalPolicy,
}

impl TerrainLodBand {
    /// Creates a band with explicit vertical offsets.
    pub fn fixed_offsets(lod: u8, horizontal_radius: i32, vertical_offsets: Vec<i32>) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::FixedOffsets(vertical_offsets),
        }
    }

    /// Creates a band with a simple player-centered vertical window.
    pub fn bounded(
        lod: u8,
        horizontal_radius: i32,
        vertical: TerrainLodBoundedVerticalPolicy,
    ) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::Bounded(vertical),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TerrainLodVerticalPolicy {
    FixedOffsets(Vec<i32>),
    Bounded(TerrainLodBoundedVerticalPolicy),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainLodBoundedVerticalPolicy {
    pub below_player_nodes: i32,
    pub above_player_nodes: i32,
}

impl TerrainLodBoundedVerticalPolicy {
    /// Creates a bounded vertical policy.
    pub fn new(
        below_player_nodes: i32,
        above_player_nodes: i32,
    ) -> Result<Self, TerrainStreamError> {
        if below_player_nodes < 0 || above_player_nodes < 0 {
            return Err(TerrainStreamError::new(
                "terrain vertical policy windows must be non-negative",
            ));
        }
        Ok(Self {
            below_player_nodes,
            above_player_nodes,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainStreamJob {
    BuildNode {
        generation: u64,
        key: TerrainNodeKey,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct TerrainStreamStatus {
    pub generation: u64,
    pub desired_density_count: usize,
    pub density_ready_count: usize,
    pub in_flight_density_count: usize,
    pub missing_density_count: usize,
    pub desired_mesh_count: usize,
    pub desired_lod0_count: usize,
    pub mesh_empty_count: usize,
    pub lod0_empty_count: usize,
    pub in_flight_lod_count: usize,
    pub missing_mesh_count: usize,
    pub missing_lod0_count: usize,
    pub max_in_flight_jobs: usize,
    pub lod_summaries: Vec<TerrainLodStatus>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainLodStatus {
    pub lod: u8,
    pub desired_node_count: usize,
    pub min_desired_node_y: Option<i32>,
    pub max_desired_node_y: Option<i32>,
    pub density_ready_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainDissolveTransition {
    pub outgoing: TerrainNodeKey,
    pub incoming: Vec<TerrainNodeKey>,
    pub remaining_ticks: u8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainStreamError {
    message: String,
}

impl TerrainStreamError {
    /// Creates a terrain stream error with a stable diagnostic string.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl std::fmt::Display for TerrainStreamError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for TerrainStreamError {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum NodeBuildState {
    InFlight(u64),
    Generated,
    Empty,
    Failed,
}

pub struct TerrainStreamScheduler {
    generation: u64,
    config: TerrainStreamConfig,
    center: TerrainChunkCoord,
    desired: BTreeSet<TerrainNodeKey>,
    states: BTreeMap<TerrainNodeKey, NodeBuildState>,
    transitions: Vec<TerrainDissolveTransition>,
}

impl TerrainStreamScheduler {
    /// Creates a terrain stream scheduler from a validated config.
    pub fn new(config: TerrainStreamConfig) -> Result<Self, TerrainStreamError> {
        validate_config(&config)?;
        Ok(Self {
            generation: 1,
            config,
            center: TerrainChunkCoord::default(),
            desired: BTreeSet::new(),
            states: BTreeMap::new(),
            transitions: Vec::new(),
        })
    }

    /// Updates the stream seed and variant without changing the desired center.
    pub fn set_terrain_context(
        &mut self,
        terrain_seed: u32,
        terrain_variant: TerrainVariantDescriptor,
        base_cell_size: f64,
    ) -> Result<(), TerrainStreamError> {
        terrain_variant
            .validate()
            .map_err(|error| TerrainStreamError::new(error.to_string()))?;
        self.config.terrain_seed = terrain_seed;
        self.config.terrain_variant = terrain_variant;
        self.config.base_cell_size = base_cell_size;
        self.invalidate_all();
        Ok(())
    }

    /// Updates the maximum number of node jobs that can be in flight.
    pub fn set_max_in_flight_jobs(
        &mut self,
        max_in_flight_jobs: usize,
    ) -> Result<(), TerrainStreamError> {
        if max_in_flight_jobs == 0 {
            return Err(TerrainStreamError::new(
                "terrain stream max in-flight jobs must be positive",
            ));
        }
        self.config.max_in_flight_jobs = max_in_flight_jobs;
        Ok(())
    }

    /// Resets all node state around a new LOD0 center coordinate.
    pub fn reset(&mut self, center: TerrainChunkCoord) {
        self.generation = self.generation.wrapping_add(1).max(1);
        self.center = center;
        self.states.clear();
        self.transitions.clear();
        self.rebuild_desired();
    }

    /// Synchronizes desired nodes around a new LOD0 center coordinate.
    pub fn sync_center(&mut self, center: TerrainChunkCoord) {
        if self.center == center && !self.desired.is_empty() {
            return;
        }
        self.center = center;
        self.rebuild_desired();
        self.states.retain(|key, _state| self.desired.contains(key));
    }

    /// Invalidates all generated terrain while keeping the desired center.
    pub fn invalidate_all(&mut self) {
        self.generation = self.generation.wrapping_add(1).max(1);
        self.states.clear();
        self.transitions.clear();
        self.rebuild_desired();
    }

    /// Returns the next batch of whole-node build jobs.
    pub fn tick(&mut self) -> Vec<TerrainStreamJob> {
        self.advance_transitions();
        self.rebuild_desired();

        let in_flight = self.in_flight_count();
        let capacity = self.config.max_in_flight_jobs.saturating_sub(in_flight);
        if capacity == 0 {
            return Vec::new();
        }

        let mut jobs = Vec::new();
        let mut candidates = self.desired.iter().copied().collect::<Vec<_>>();
        candidates
            .sort_by(|left, right| right.lod.cmp(&left.lod).then(left.coord.cmp(&right.coord)));
        for key in candidates {
            if jobs.len() >= capacity {
                break;
            }
            if self.states.contains_key(&key) || !self.parent_ready(key) {
                continue;
            }
            self.states
                .insert(key, NodeBuildState::InFlight(self.generation));
            jobs.push(TerrainStreamJob::BuildNode {
                generation: self.generation,
                key,
            });
        }

        jobs
    }

    /// Marks a node job complete.
    pub fn complete_node(&mut self, generation: u64, key: TerrainNodeKey, empty: bool) -> bool {
        if self.states.get(&key) != Some(&NodeBuildState::InFlight(generation)) {
            return false;
        }
        self.states.insert(
            key,
            if empty {
                NodeBuildState::Empty
            } else {
                NodeBuildState::Generated
            },
        );
        self.record_ready_child_transition(key);
        true
    }

    /// Marks a node job failed so it can be retried later.
    pub fn fail_node(&mut self, generation: u64, key: TerrainNodeKey) -> bool {
        if self.states.get(&key) != Some(&NodeBuildState::InFlight(generation)) {
            return false;
        }
        self.states.insert(key, NodeBuildState::Failed);
        true
    }

    /// Returns whether a node has completed as generated or empty.
    pub fn mesh_generated(&self, key: TerrainNodeKey) -> bool {
        matches!(
            self.states.get(&key),
            Some(NodeBuildState::Generated | NodeBuildState::Empty)
        )
    }

    /// Returns desired LOD0 compatibility coordinates.
    pub fn desired_density_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired
            .iter()
            .filter(|key| key.lod == 0)
            .map(|key| key.coord)
            .collect()
    }

    /// Returns all desired terrain nodes.
    pub fn desired_density_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired.iter().copied().collect()
    }

    /// Returns all desired mesh nodes.
    pub fn desired_mesh_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_density_nodes()
    }

    /// Returns whether any desired node remains missing or in flight.
    pub fn pending(&self) -> bool {
        self.status().missing_mesh_count > 0 || self.in_flight_count() > 0
    }

    /// Returns a stream status snapshot.
    pub fn status(&self) -> TerrainStreamStatus {
        let desired_mesh_count = self.desired.len();
        let desired_lod0_count = self.desired.iter().filter(|key| key.lod == 0).count();
        let density_ready_count = self
            .desired
            .iter()
            .filter(|key| self.mesh_generated(**key))
            .count();
        let mesh_empty_count = self
            .desired
            .iter()
            .filter(|key| self.states.get(key) == Some(&NodeBuildState::Empty))
            .count();
        let missing_mesh_count = self
            .desired
            .iter()
            .filter(|key| !self.mesh_generated(**key))
            .count();
        TerrainStreamStatus {
            generation: self.generation,
            desired_density_count: desired_mesh_count,
            density_ready_count,
            in_flight_density_count: self.in_flight_count(),
            missing_density_count: missing_mesh_count,
            desired_mesh_count,
            desired_lod0_count,
            mesh_empty_count,
            lod0_empty_count: self
                .desired
                .iter()
                .filter(|key| key.lod == 0 && self.states.get(key) == Some(&NodeBuildState::Empty))
                .count(),
            in_flight_lod_count: self.in_flight_count(),
            missing_mesh_count,
            missing_lod0_count: self
                .desired
                .iter()
                .filter(|key| key.lod == 0 && !self.mesh_generated(**key))
                .count(),
            max_in_flight_jobs: self.config.max_in_flight_jobs,
            lod_summaries: self.lod_summaries(),
        }
    }

    /// Test helper for directly marking a node ready.
    pub fn force_ready_for_test(&mut self, key: TerrainNodeKey, empty: bool) {
        self.states.insert(
            key,
            if empty {
                NodeBuildState::Empty
            } else {
                NodeBuildState::Generated
            },
        );
    }

    /// Returns the visible cover below a root node.
    pub fn visible_cover_from(&self, root: TerrainNodeKey) -> Vec<TerrainNodeKey> {
        let mut visible = Vec::new();
        self.collect_visible_cover(root, &mut visible);
        visible
    }

    fn rebuild_desired(&mut self) {
        self.desired.clear();
        for band in &self.config.lod_bands {
            let center = terrain_node_coord_for_lod(
                f64::from(self.center.x) * crate::LOD0_NODE_SIZE_METERS,
                f64::from(self.center.y) * crate::LOD0_NODE_SIZE_METERS,
                f64::from(self.center.z) * crate::LOD0_NODE_SIZE_METERS,
                self.config.base_cell_size,
                band.lod,
            );
            for x in center.x - band.horizontal_radius..=center.x + band.horizontal_radius {
                for z in center.z - band.horizontal_radius..=center.z + band.horizontal_radius {
                    for y in vertical_offsets(center.y, &band.vertical) {
                        self.desired.insert(TerrainNodeKey {
                            lod: band.lod,
                            coord: TerrainChunkCoord { x, y, z },
                        });
                    }
                }
            }
        }
    }

    fn parent_ready(&self, key: TerrainNodeKey) -> bool {
        match terrain_node_parent(key) {
            Some(parent) if self.desired.contains(&parent) => self.mesh_generated(parent),
            _ => true,
        }
    }

    fn in_flight_count(&self) -> usize {
        self.states
            .values()
            .filter(|state| matches!(state, NodeBuildState::InFlight(_)))
            .count()
    }

    fn record_ready_child_transition(&mut self, child: TerrainNodeKey) {
        let Some(parent) = terrain_node_parent(child) else {
            return;
        };
        let Some(children) = terrain_node_children(parent) else {
            return;
        };
        if !self.mesh_generated(parent)
            || !children
                .iter()
                .all(|candidate| self.mesh_generated(*candidate))
        {
            return;
        }
        if self
            .transitions
            .iter()
            .any(|transition| transition.outgoing == parent)
        {
            return;
        }
        self.transitions.push(TerrainDissolveTransition {
            outgoing: parent,
            incoming: children.to_vec(),
            remaining_ticks: DISSOLVE_TICKS,
        });
    }

    fn advance_transitions(&mut self) {
        for transition in &mut self.transitions {
            transition.remaining_ticks = transition.remaining_ticks.saturating_sub(1);
        }
        self.transitions
            .retain(|transition| transition.remaining_ticks > 0);
    }

    fn collect_visible_cover(&self, key: TerrainNodeKey, visible: &mut Vec<TerrainNodeKey>) {
        if !self.mesh_generated(key) {
            return;
        }
        if let Some(children) = terrain_node_children(key) {
            if children.iter().all(|child| self.mesh_generated(*child)) {
                for child in children {
                    self.collect_visible_cover(child, visible);
                }
                return;
            }
        }
        visible.push(key);
    }

    fn lod_summaries(&self) -> Vec<TerrainLodStatus> {
        let mut summaries = Vec::new();
        for lod in 0..=MAX_PLAYABLE_LOD {
            let nodes = self
                .desired
                .iter()
                .filter(|key| key.lod == lod)
                .copied()
                .collect::<Vec<_>>();
            if nodes.is_empty() {
                continue;
            }
            summaries.push(TerrainLodStatus {
                lod,
                desired_node_count: nodes.len(),
                min_desired_node_y: nodes.iter().map(|key| key.coord.y).min(),
                max_desired_node_y: nodes.iter().map(|key| key.coord.y).max(),
                density_ready_node_count: nodes
                    .iter()
                    .filter(|key| self.mesh_generated(**key))
                    .count(),
                empty_node_count: nodes
                    .iter()
                    .filter(|key| self.states.get(key) == Some(&NodeBuildState::Empty))
                    .count(),
                missing_node_count: nodes
                    .iter()
                    .filter(|key| !self.mesh_generated(**key))
                    .count(),
            });
        }
        summaries
    }
}

fn validate_config(config: &TerrainStreamConfig) -> Result<(), TerrainStreamError> {
    if config.max_in_flight_jobs == 0 {
        return Err(TerrainStreamError::new(
            "terrain stream max in-flight jobs must be positive",
        ));
    }
    if !config.base_cell_size.is_finite() || config.base_cell_size <= 0.0 {
        return Err(TerrainStreamError::new(
            "terrain stream base cell size must be positive",
        ));
    }
    config
        .terrain_variant
        .validate()
        .map_err(|error| TerrainStreamError::new(error.to_string()))?;
    Ok(())
}

fn default_lod_bands() -> Vec<TerrainLodBand> {
    (0..=MAX_PLAYABLE_LOD)
        .map(|lod| TerrainLodBand::fixed_offsets(lod, 1, vec![0]))
        .collect()
}

fn vertical_offsets(center_y: i32, policy: &TerrainLodVerticalPolicy) -> Vec<i32> {
    match policy {
        TerrainLodVerticalPolicy::FixedOffsets(offsets) => {
            offsets.iter().map(|offset| center_y + *offset).collect()
        }
        TerrainLodVerticalPolicy::Bounded(window) => {
            (center_y - window.below_player_nodes..=center_y + window.above_player_nodes).collect()
        }
    }
}

#[allow(dead_code)]
fn coord_from_position(
    position: engine_core_compat::Vec3Compat,
    base_cell_size: f64,
) -> TerrainChunkCoord {
    terrain_chunk_coord_containing_position(position.x, position.y, position.z, base_cell_size)
}

mod engine_core_compat {
    pub struct Vec3Compat {
        pub x: f32,
        pub y: f32,
        pub z: f32,
    }
}
