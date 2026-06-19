//! Exact parent-grid terrain stream for the rebuild baseline.
//!
//! This module owns terrain stream truth. Given a player position it computes
//! the documented desired node set, generates missing whole-node meshes, reports
//! created and destroyed mesh packets for renderer caches, exposes the exact
//! visible cover, and answers mesh-backed height queries. Browser/renderer code
//! should mirror these reports; it should not reselect terrain visibility.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::Arc;

use crate::mesh::{build_node_mesh_for_variant, MeshData};
use crate::node::{
    terrain_chunk_coord_containing_position, terrain_node_cell_size, terrain_node_children,
    terrain_node_coord_for_lod, terrain_node_parent, terrain_node_size, TerrainChunkCoord,
    TerrainNodeKey, MAX_PLAYABLE_LOD,
};
use crate::variant::TerrainVariantDescriptor;

const DEFAULT_MAX_IN_FLIGHT_JOBS: usize = 1;

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
    pub source: TerrainLodBandSource,
}

impl TerrainLodBand {
    /// Creates a direct node grid band. This is retained for narrow tests.
    pub fn fixed_offsets(lod: u8, horizontal_radius: i32, vertical_offsets: Vec<i32>) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::FixedOffsets(vertical_offsets),
            source: TerrainLodBandSource::DirectNodes,
        }
    }

    /// Creates a direct node grid band with a player-centered vertical window.
    pub fn bounded(
        lod: u8,
        horizontal_radius: i32,
        vertical: TerrainLodBoundedVerticalPolicy,
    ) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::Bounded(vertical),
            source: TerrainLodBandSource::DirectNodes,
        }
    }

    /// Creates this LOD by filling each node in a parent-LOD grid with children.
    pub fn children_of_parent_grid(
        lod: u8,
        horizontal_radius: i32,
        vertical: TerrainLodBoundedVerticalPolicy,
    ) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::Bounded(vertical),
            source: TerrainLodBandSource::ParentGridChildren,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainLodBandSource {
    DirectNodes,
    ParentGridChildren,
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
pub struct TerrainStreamMeshUpdate {
    pub key: TerrainNodeKey,
    pub mesh: Arc<MeshData>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TerrainStreamUpdate {
    pub created_nodes: Vec<TerrainStreamMeshUpdate>,
    pub destroyed_nodes: Vec<TerrainNodeKey>,
    pub visible_nodes: Vec<TerrainNodeKey>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainStreamHeightSample {
    pub key: TerrainNodeKey,
    pub height: f32,
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

pub struct TerrainStreamScheduler {
    generation: u64,
    config: TerrainStreamConfig,
    position: [f32; 3],
    desired: BTreeSet<TerrainNodeKey>,
    meshes: BTreeMap<TerrainNodeKey, Arc<MeshData>>,
    empty_nodes: BTreeSet<TerrainNodeKey>,
    visible_nodes: BTreeSet<TerrainNodeKey>,
    pending_destroyed_nodes: BTreeSet<TerrainNodeKey>,
    completed_build_count: u64,
}

impl TerrainStreamScheduler {
    /// Creates a terrain stream from a validated config.
    pub fn new(config: TerrainStreamConfig) -> Result<Self, TerrainStreamError> {
        validate_config(&config)?;
        Ok(Self {
            generation: 1,
            config,
            position: [0.0, 0.0, 0.0],
            desired: BTreeSet::new(),
            meshes: BTreeMap::new(),
            empty_nodes: BTreeSet::new(),
            visible_nodes: BTreeSet::new(),
            pending_destroyed_nodes: BTreeSet::new(),
            completed_build_count: 0,
        })
    }

    /// Updates the stream seed and variant. Existing generated meshes are destroyed.
    pub fn set_terrain_context(
        &mut self,
        terrain_seed: u32,
        terrain_variant: TerrainVariantDescriptor,
        base_cell_size: f64,
    ) -> Result<(), TerrainStreamError> {
        terrain_variant
            .validate()
            .map_err(|error| TerrainStreamError::new(error.to_string()))?;
        if !base_cell_size.is_finite() || base_cell_size <= 0.0 {
            return Err(TerrainStreamError::new(
                "terrain stream base cell size must be positive",
            ));
        }
        self.config.terrain_seed = terrain_seed;
        self.config.terrain_variant = terrain_variant;
        self.config.base_cell_size = base_cell_size;
        self.invalidate_all();
        Ok(())
    }

    /// Retained compatibility hook. The sync baseline does not launch jobs.
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

    /// Resets all generated state around an LOD0 compatibility center.
    pub fn reset(&mut self, center: TerrainChunkCoord) {
        self.invalidate_all();
        let span = terrain_node_size(self.config.base_cell_size, 0) as f32;
        let _ = self.sync_position(
            center.x as f32 * span,
            center.y as f32 * span,
            center.z as f32 * span,
        );
    }

    /// Synchronizes state around an LOD0 compatibility center.
    pub fn sync_center(&mut self, center: TerrainChunkCoord) {
        let span = terrain_node_size(self.config.base_cell_size, 0) as f32;
        let _ = self.sync_position(
            center.x as f32 * span,
            center.y as f32 * span,
            center.z as f32 * span,
        );
    }

    /// Synchronizes the terrain stream around the player position.
    pub fn sync_position(&mut self, x: f32, y: f32, z: f32) -> TerrainStreamUpdate {
        self.position = [x, y, z];
        let desired = desired_nodes_for_position(&self.config, self.position);

        let mut destroyed_nodes = self
            .pending_destroyed_nodes
            .iter()
            .copied()
            .collect::<Vec<_>>();
        self.pending_destroyed_nodes.clear();

        let no_longer_desired = self
            .meshes
            .keys()
            .filter(|key| !desired.contains(key))
            .copied()
            .collect::<Vec<_>>();
        for key in &no_longer_desired {
            self.meshes.remove(key);
            destroyed_nodes.push(*key);
        }

        self.empty_nodes.retain(|key| desired.contains(key));
        self.desired = desired;

        let mut created_nodes = Vec::new();
        for key in self.sorted_generation_order() {
            if self.meshes.contains_key(&key) || self.empty_nodes.contains(&key) {
                continue;
            }

            let mesh = build_node_mesh_for_variant(
                self.config.terrain_seed,
                self.config.terrain_variant,
                key,
                self.config.base_cell_size,
            );
            self.completed_build_count = self.completed_build_count.saturating_add(1);
            if mesh.indices.is_empty() {
                self.empty_nodes.insert(key);
                continue;
            }

            let mesh = Arc::new(mesh);
            self.meshes.insert(key, Arc::clone(&mesh));
            created_nodes.push(TerrainStreamMeshUpdate { key, mesh });
        }

        let visible_nodes = self.compute_visible_cover();
        self.visible_nodes = visible_nodes.iter().copied().collect();

        TerrainStreamUpdate {
            created_nodes,
            destroyed_nodes,
            visible_nodes,
        }
    }

    /// Invalidates all generated terrain while keeping the current position.
    pub fn invalidate_all(&mut self) {
        self.generation = self.generation.wrapping_add(1).max(1);
        self.pending_destroyed_nodes
            .extend(self.meshes.keys().copied());
        self.meshes.clear();
        self.empty_nodes.clear();
        self.visible_nodes.clear();
        self.desired.clear();
    }

    /// The sync baseline has no worker jobs to issue.
    pub fn tick(&mut self) -> Vec<TerrainStreamJob> {
        Vec::new()
    }

    /// Compatibility completion hook for disabled async jobs.
    pub fn complete_node(&mut self, _generation: u64, _key: TerrainNodeKey, _empty: bool) -> bool {
        false
    }

    /// Compatibility failure hook for disabled async jobs.
    pub fn fail_node(&mut self, _generation: u64, _key: TerrainNodeKey) -> bool {
        false
    }

    /// Returns whether a node has completed as generated or empty.
    pub fn mesh_generated(&self, key: TerrainNodeKey) -> bool {
        self.node_ready(key)
    }

    /// Returns desired LOD0 compatibility coordinates.
    pub fn desired_density_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired
            .iter()
            .filter(|key| key.lod == 0)
            .map(|key| key.coord)
            .collect()
    }

    /// Returns all loaded terrain nodes.
    pub fn desired_density_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired.iter().copied().collect()
    }

    /// Returns all loaded terrain nodes.
    pub fn desired_mesh_nodes(&self) -> Vec<TerrainNodeKey> {
        self.desired_density_nodes()
    }

    /// Returns the visible terrain cover.
    pub fn visible_nodes(&self) -> Vec<TerrainNodeKey> {
        self.visible_nodes.iter().copied().collect()
    }

    /// Returns false in the synchronous baseline after a position sync.
    pub fn pending(&self) -> bool {
        self.status().missing_mesh_count > 0
    }

    /// Returns a stream status snapshot.
    pub fn status(&self) -> TerrainStreamStatus {
        let desired_mesh_count = self.desired.len();
        let desired_lod0_count = self.desired.iter().filter(|key| key.lod == 0).count();
        let density_ready_count = self
            .desired
            .iter()
            .filter(|key| self.node_ready(**key))
            .count();
        let mesh_empty_count = self
            .desired
            .iter()
            .filter(|key| self.empty_nodes.contains(key))
            .count();
        let missing_mesh_count = self
            .desired
            .iter()
            .filter(|key| !self.node_ready(**key))
            .count();

        TerrainStreamStatus {
            generation: self.generation,
            desired_density_count: desired_mesh_count,
            density_ready_count,
            in_flight_density_count: 0,
            missing_density_count: missing_mesh_count,
            desired_mesh_count,
            desired_lod0_count,
            mesh_empty_count,
            lod0_empty_count: self
                .desired
                .iter()
                .filter(|key| key.lod == 0 && self.empty_nodes.contains(key))
                .count(),
            in_flight_lod_count: 0,
            missing_mesh_count,
            missing_lod0_count: self
                .desired
                .iter()
                .filter(|key| key.lod == 0 && !self.node_ready(**key))
                .count(),
            max_in_flight_jobs: self.config.max_in_flight_jobs,
            lod_summaries: self.lod_summaries(),
        }
    }

    /// Returns the number of generated node jobs completed by this stream.
    pub fn completed_build_count(&self) -> u64 {
        self.completed_build_count
    }

    /// Samples the visible mesh cover below a downward ray start.
    pub fn height_at_below(
        &self,
        x: f32,
        z: f32,
        ray_start_y: f32,
    ) -> Option<TerrainStreamHeightSample> {
        self.height_at_from_nodes(x, z, Some(ray_start_y), self.visible_nodes.iter().copied())
    }

    /// Samples the visible mesh cover at a world X/Z point.
    pub fn height_at(&self, x: f32, z: f32) -> Option<TerrainStreamHeightSample> {
        self.height_at_from_nodes(x, z, None, self.visible_nodes.iter().copied())
    }

    /// Test helper for directly marking a node ready.
    pub fn force_ready_for_test(&mut self, key: TerrainNodeKey, empty: bool) {
        if empty {
            self.empty_nodes.insert(key);
            self.meshes.remove(&key);
        } else {
            self.empty_nodes.remove(&key);
            self.meshes.insert(
                key,
                Arc::new(MeshData {
                    vertices: Vec::new(),
                    indices: vec![0, 1, 2],
                }),
            );
        }
        self.desired.insert(key);
    }

    /// Returns the visible cover below a root node.
    pub fn visible_cover_from(&self, root: TerrainNodeKey) -> Vec<TerrainNodeKey> {
        let mut visible = Vec::new();
        self.collect_visible_cover(root, &mut visible);
        visible
    }

    fn sorted_generation_order(&self) -> Vec<TerrainNodeKey> {
        let mut keys = self.desired.iter().copied().collect::<Vec<_>>();
        keys.sort_by(|left, right| right.lod.cmp(&left.lod).then(left.coord.cmp(&right.coord)));
        keys
    }

    fn compute_visible_cover(&self) -> Vec<TerrainNodeKey> {
        let mut roots = self
            .desired
            .iter()
            .filter(|key| match terrain_node_parent(**key) {
                Some(parent) => !self.desired.contains(&parent),
                None => true,
            })
            .copied()
            .collect::<Vec<_>>();
        roots.sort();

        let mut visible = Vec::new();
        for root in roots {
            self.collect_visible_cover(root, &mut visible);
        }
        visible
    }

    fn collect_visible_cover(&self, key: TerrainNodeKey, visible: &mut Vec<TerrainNodeKey>) {
        if !self.node_ready(key) {
            return;
        }
        if self.empty_nodes.contains(&key) {
            return;
        }

        if let Some(children) = terrain_node_children(key) {
            let children_can_replace = children
                .iter()
                .all(|child| self.desired.contains(child) && self.node_ready(*child));
            if children_can_replace {
                for child in children {
                    self.collect_visible_cover(child, visible);
                }
                return;
            }
        }

        if self.meshes.contains_key(&key) {
            visible.push(key);
        }
    }

    fn node_ready(&self, key: TerrainNodeKey) -> bool {
        self.meshes.contains_key(&key) || self.empty_nodes.contains(&key)
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
                density_ready_node_count: nodes.iter().filter(|key| self.node_ready(**key)).count(),
                empty_node_count: nodes
                    .iter()
                    .filter(|key| self.empty_nodes.contains(key))
                    .count(),
                missing_node_count: nodes.iter().filter(|key| !self.node_ready(**key)).count(),
            });
        }
        summaries
    }

    fn height_at_from_nodes(
        &self,
        x: f32,
        z: f32,
        ray_start_y: Option<f32>,
        nodes: impl Iterator<Item = TerrainNodeKey>,
    ) -> Option<TerrainStreamHeightSample> {
        if !x.is_finite() || !z.is_finite() {
            return None;
        }

        let mut best: Option<TerrainStreamHeightSample> = None;
        for key in nodes {
            if !node_contains_xz(key, self.config.base_cell_size, x, z) {
                continue;
            }
            let Some(mesh) = self.meshes.get(&key) else {
                continue;
            };
            let height = match ray_start_y {
                Some(ray_start_y) => mesh.height_at_below(x, z, ray_start_y),
                None => mesh.height_at(x, z),
            };
            let Some(height) = height else {
                continue;
            };
            let sample = TerrainStreamHeightSample { key, height };
            best = match best {
                Some(previous) if previous.key.lod <= key.lod => Some(previous),
                _ => Some(sample),
            };
        }

        best
    }
}

pub fn exact_parent_grid_desired_nodes_for_position(
    x: f32,
    y: f32,
    z: f32,
    base_cell_size: f64,
) -> BTreeSet<TerrainNodeKey> {
    let config = TerrainStreamConfig {
        base_cell_size,
        ..TerrainStreamConfig::default()
    };
    desired_nodes_for_position(&config, [x, y, z])
}

fn desired_nodes_for_position(
    config: &TerrainStreamConfig,
    position: [f32; 3],
) -> BTreeSet<TerrainNodeKey> {
    let mut desired = BTreeSet::new();
    for band in &config.lod_bands {
        match band.source {
            TerrainLodBandSource::DirectNodes => {
                insert_direct_band_nodes(&mut desired, config, position, band);
            }
            TerrainLodBandSource::ParentGridChildren => {
                insert_parent_grid_child_nodes(&mut desired, config, position, band);
            }
        }
    }
    desired
}

fn insert_direct_band_nodes(
    desired: &mut BTreeSet<TerrainNodeKey>,
    config: &TerrainStreamConfig,
    position: [f32; 3],
    band: &TerrainLodBand,
) {
    let center = center_coord_for_lod(config, position, band.lod);
    for x in center.x - band.horizontal_radius..=center.x + band.horizontal_radius {
        for y in vertical_coords(center.y, &band.vertical) {
            for z in center.z - band.horizontal_radius..=center.z + band.horizontal_radius {
                desired.insert(TerrainNodeKey {
                    lod: band.lod,
                    coord: TerrainChunkCoord { x, y, z },
                });
            }
        }
    }
}

fn insert_parent_grid_child_nodes(
    desired: &mut BTreeSet<TerrainNodeKey>,
    config: &TerrainStreamConfig,
    position: [f32; 3],
    band: &TerrainLodBand,
) {
    if band.lod >= MAX_PLAYABLE_LOD {
        insert_direct_band_nodes(desired, config, position, band);
        return;
    }

    let parent_lod = band.lod + 1;
    let center = center_coord_for_lod(config, position, parent_lod);
    for x in center.x - band.horizontal_radius..=center.x + band.horizontal_radius {
        for y in vertical_coords(center.y, &band.vertical) {
            for z in center.z - band.horizontal_radius..=center.z + band.horizontal_radius {
                let parent = TerrainNodeKey {
                    lod: parent_lod,
                    coord: TerrainChunkCoord { x, y, z },
                };
                if let Some(children) = terrain_node_children(parent) {
                    desired.extend(children);
                }
            }
        }
    }
}

fn center_coord_for_lod(
    config: &TerrainStreamConfig,
    position: [f32; 3],
    lod: u8,
) -> TerrainChunkCoord {
    terrain_node_coord_for_lod(
        f64::from(position[0]),
        f64::from(position[1]),
        f64::from(position[2]),
        config.base_cell_size,
        lod,
    )
}

fn vertical_coords(center_y: i32, policy: &TerrainLodVerticalPolicy) -> Vec<i32> {
    match policy {
        TerrainLodVerticalPolicy::FixedOffsets(offsets) => {
            offsets.iter().map(|offset| center_y + *offset).collect()
        }
        TerrainLodVerticalPolicy::Bounded(window) => {
            (center_y - window.below_player_nodes..=center_y + window.above_player_nodes).collect()
        }
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
    for band in &config.lod_bands {
        if band.lod > MAX_PLAYABLE_LOD {
            return Err(TerrainStreamError::new(
                "terrain stream LOD bands must stay within the playable LOD range",
            ));
        }
        if band.horizontal_radius < 0 {
            return Err(TerrainStreamError::new(
                "terrain stream horizontal radius must be non-negative",
            ));
        }
    }
    config
        .terrain_variant
        .validate()
        .map_err(|error| TerrainStreamError::new(error.to_string()))?;
    Ok(())
}

fn default_lod_bands() -> Vec<TerrainLodBand> {
    let window = TerrainLodBoundedVerticalPolicy {
        below_player_nodes: 1,
        above_player_nodes: 1,
    };
    let mut bands = Vec::with_capacity(MAX_PLAYABLE_LOD as usize + 1);
    bands.push(TerrainLodBand::bounded(MAX_PLAYABLE_LOD, 1, window));
    for lod in (0..MAX_PLAYABLE_LOD).rev() {
        bands.push(TerrainLodBand::children_of_parent_grid(lod, 1, window));
    }
    bands
}

fn node_contains_xz(key: TerrainNodeKey, base_cell_size: f64, x: f32, z: f32) -> bool {
    let node_size = terrain_node_cell_size(base_cell_size, key.lod) * 32.0;
    let min_x = key.coord.x as f64 * node_size;
    let min_z = key.coord.z as f64 * node_size;
    let max_x = min_x + node_size;
    let max_z = min_z + node_size;
    let x = f64::from(x);
    let z = f64::from(z);
    x >= min_x - 0.001 && x <= max_x + 0.001 && z >= min_z - 0.001 && z <= max_z + 0.001
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn desired_set_matches_parent_grid_contract_from_many_positions() {
        let positions = [
            [0.0, 0.0, 0.0],
            [31.75, -17.5, 63.25],
            [-0.25, 33.0, -65.0],
            [1024.0, -256.0, -2048.0],
        ];

        for position in positions {
            let desired = exact_parent_grid_desired_nodes_for_position(
                position[0],
                position[1],
                position[2],
                1.0,
            );

            assert_eq!(
                desired
                    .iter()
                    .filter(|key| key.lod == MAX_PLAYABLE_LOD)
                    .count(),
                27
            );
            for lod in 0..MAX_PLAYABLE_LOD {
                assert_eq!(
                    desired.iter().filter(|key| key.lod == lod).count(),
                    216,
                    "LOD{lod} desired count should be the 8 children of a 3x3x3 parent grid"
                );
            }
            assert_eq!(desired.len(), 27 + 216 * MAX_PLAYABLE_LOD as usize);

            for lod in 0..=MAX_PLAYABLE_LOD {
                let actual = desired
                    .iter()
                    .filter(|key| key.lod == lod)
                    .copied()
                    .collect::<BTreeSet<_>>();
                let expected = expected_desired_lod(position, lod);
                assert_eq!(
                    actual, expected,
                    "LOD{lod} desired nodes did not match the exact parent-grid rule at {position:?}"
                );
            }
        }
    }

    #[test]
    fn fully_streamed_visible_set_is_exact_cover_of_desired_nodes() {
        let mut stream = TerrainStreamScheduler::new(TerrainStreamConfig::default()).unwrap();

        for position in [[0.0, 0.0, 0.0], [-48.0, 19.0, 96.0], [777.0, -64.0, -513.0]] {
            let update = stream.sync_position(position[0], position[1], position[2]);
            let status = stream.status();
            let visible = update
                .visible_nodes
                .iter()
                .copied()
                .collect::<BTreeSet<_>>();
            let expected = expected_visible_cover(&stream);

            assert_eq!(status.pending(), false);
            assert_eq!(status.missing_mesh_count, 0);
            assert_eq!(stream.visible_nodes, expected);
            assert_eq!(visible, expected);
            assert!(visible.iter().all(|key| stream.desired.contains(key)));
            assert_visible_cover_invariants(&stream, &visible);
            assert_visible_cover_has_no_gaps_from_lod5_roots(&stream, &visible);
        }
    }

    #[test]
    fn stream_reports_created_and_destroyed_mesh_cache_events() {
        let mut stream = TerrainStreamScheduler::new(TerrainStreamConfig::default()).unwrap();

        let first = stream.sync_position(0.0, 0.0, 0.0);
        assert!(!first.created_nodes.is_empty());
        assert!(first.destroyed_nodes.is_empty());

        let first_desired = stream.desired.clone();
        let second = stream.sync_position(4096.0, 0.0, 4096.0);

        assert!(!second.created_nodes.is_empty());
        assert!(!second.destroyed_nodes.is_empty());
        assert!(second
            .destroyed_nodes
            .iter()
            .all(|key| first_desired.contains(key) && !stream.desired.contains(key)));
        assert!(stream.meshes.keys().all(|key| stream.desired.contains(key)));
        let visible = second
            .visible_nodes
            .iter()
            .copied()
            .collect::<BTreeSet<_>>();
        assert_visible_cover_invariants(&stream, &visible);
        assert_visible_cover_has_no_gaps_from_lod5_roots(&stream, &visible);
    }

    #[test]
    fn partial_stream_state_never_replaces_parent_with_incomplete_octet() {
        let mut stream = TerrainStreamScheduler::new(TerrainStreamConfig::default()).unwrap();
        let parent = TerrainNodeKey {
            lod: 2,
            coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
        };
        stream.force_ready_for_test(parent, false);
        let children = terrain_node_children(parent).unwrap();
        for child in children.iter().take(7) {
            stream.force_ready_for_test(*child, false);
        }

        assert_eq!(stream.visible_cover_from(parent), vec![parent]);
        assert_subtree_has_visible_or_empty_cover(&stream, &BTreeSet::from([parent]), parent);

        stream.force_ready_for_test(children[7], true);
        let visible = stream
            .visible_cover_from(parent)
            .into_iter()
            .collect::<BTreeSet<_>>();

        assert!(!visible.contains(&parent));
        assert_eq!(visible.len(), 7);
        assert_visible_cover_invariants(&stream, &visible);
        assert_subtree_has_visible_or_empty_cover(&stream, &visible, parent);

        let refined_child = children[0];
        let grandchildren = terrain_node_children(refined_child).unwrap();
        for grandchild in grandchildren.iter().take(7) {
            stream.force_ready_for_test(*grandchild, false);
        }
        let visible = stream
            .visible_cover_from(parent)
            .into_iter()
            .collect::<BTreeSet<_>>();
        assert!(visible.contains(&refined_child));
        assert!(grandchildren
            .iter()
            .all(|grandchild| !visible.contains(grandchild)));
        assert_visible_cover_invariants(&stream, &visible);
        assert_subtree_has_visible_or_empty_cover(&stream, &visible, parent);

        stream.force_ready_for_test(grandchildren[7], false);
        let visible = stream
            .visible_cover_from(parent)
            .into_iter()
            .collect::<BTreeSet<_>>();
        assert!(!visible.contains(&refined_child));
        assert!(grandchildren
            .iter()
            .all(|grandchild| visible.contains(grandchild)));
        assert_visible_cover_invariants(&stream, &visible);
        assert_subtree_has_visible_or_empty_cover(&stream, &visible, parent);
    }

    fn expected_desired_lod(position: [f32; 3], lod: u8) -> BTreeSet<TerrainNodeKey> {
        let mut expected = BTreeSet::new();
        if lod == MAX_PLAYABLE_LOD {
            let center = terrain_node_coord_for_lod(
                f64::from(position[0]),
                f64::from(position[1]),
                f64::from(position[2]),
                1.0,
                lod,
            );
            for x in center.x - 1..=center.x + 1 {
                for y in center.y - 1..=center.y + 1 {
                    for z in center.z - 1..=center.z + 1 {
                        expected.insert(TerrainNodeKey {
                            lod,
                            coord: TerrainChunkCoord { x, y, z },
                        });
                    }
                }
            }
            return expected;
        }

        let parent_lod = lod + 1;
        let center = terrain_node_coord_for_lod(
            f64::from(position[0]),
            f64::from(position[1]),
            f64::from(position[2]),
            1.0,
            parent_lod,
        );
        for x in center.x - 1..=center.x + 1 {
            for y in center.y - 1..=center.y + 1 {
                for z in center.z - 1..=center.z + 1 {
                    let parent = TerrainNodeKey {
                        lod: parent_lod,
                        coord: TerrainChunkCoord { x, y, z },
                    };
                    expected.extend(terrain_node_children(parent).unwrap());
                }
            }
        }
        expected
    }

    fn expected_visible_cover(stream: &TerrainStreamScheduler) -> BTreeSet<TerrainNodeKey> {
        let mut visible = BTreeSet::new();
        for root in stream
            .desired
            .iter()
            .filter(|key| key.lod == MAX_PLAYABLE_LOD)
            .copied()
        {
            collect_expected_visible(stream, root, &mut visible);
        }
        visible
    }

    fn collect_expected_visible(
        stream: &TerrainStreamScheduler,
        key: TerrainNodeKey,
        visible: &mut BTreeSet<TerrainNodeKey>,
    ) {
        if stream.empty_nodes.contains(&key) {
            return;
        }
        if !stream.meshes.contains_key(&key) {
            return;
        }

        if let Some(children) = terrain_node_children(key) {
            if children
                .iter()
                .all(|child| stream.desired.contains(child) && stream.node_ready(*child))
            {
                for child in children {
                    collect_expected_visible(stream, child, visible);
                }
                return;
            }
        }

        visible.insert(key);
    }

    fn assert_visible_cover_invariants(
        stream: &TerrainStreamScheduler,
        visible: &BTreeSet<TerrainNodeKey>,
    ) {
        for key in visible {
            let mut ancestor = terrain_node_parent(*key);
            while let Some(parent) = ancestor {
                assert!(
                    !visible.contains(&parent),
                    "visible terrain overlaps parent {parent:?} and child {key:?}"
                );
                ancestor = terrain_node_parent(parent);
            }

            if let Some(children) = terrain_node_children(*key) {
                for child in children {
                    assert!(
                        !visible.contains(&child),
                        "visible terrain node {key:?} should hide direct child {child:?}"
                    );
                }
            }
            assert_visible_has_no_descendant(visible, *key);

            if let Some(parent) = terrain_node_parent(*key) {
                assert!(
                    !visible.contains(&parent),
                    "visible terrain node {key:?} should hide parent {parent:?}"
                );
                for sibling in terrain_node_children(parent).unwrap() {
                    assert!(
                        subtree_is_represented(stream, visible, sibling),
                        "sibling subtree {sibling:?} was not visible, empty, or fully represented"
                    );
                }
            }
        }
    }

    fn assert_visible_cover_has_no_gaps_from_lod5_roots(
        stream: &TerrainStreamScheduler,
        visible: &BTreeSet<TerrainNodeKey>,
    ) {
        let roots = stream
            .desired
            .iter()
            .filter(|key| key.lod == MAX_PLAYABLE_LOD)
            .copied()
            .collect::<Vec<_>>();
        assert_eq!(roots.len(), 27);
        for root in roots {
            assert_subtree_has_visible_or_empty_cover(stream, visible, root);
        }
    }

    fn assert_subtree_has_visible_or_empty_cover(
        stream: &TerrainStreamScheduler,
        visible: &BTreeSet<TerrainNodeKey>,
        key: TerrainNodeKey,
    ) {
        if visible.contains(&key) || stream.empty_nodes.contains(&key) {
            return;
        }

        let Some(children) = terrain_node_children(key) else {
            panic!("leaf terrain node {key:?} was neither visible nor empty");
        };
        for child in children {
            assert!(
                stream.desired.contains(&child) || stream.empty_nodes.contains(&child),
                "child {child:?} needed to cover parent {key:?} was not desired or empty"
            );
            assert_subtree_has_visible_or_empty_cover(stream, visible, child);
        }
    }

    fn assert_visible_has_no_descendant(visible: &BTreeSet<TerrainNodeKey>, key: TerrainNodeKey) {
        let Some(children) = terrain_node_children(key) else {
            return;
        };
        for child in children {
            assert!(
                !visible.contains(&child),
                "visible terrain node {key:?} should hide descendant {child:?}"
            );
            assert_visible_has_no_descendant(visible, child);
        }
    }

    fn subtree_is_represented(
        stream: &TerrainStreamScheduler,
        visible: &BTreeSet<TerrainNodeKey>,
        key: TerrainNodeKey,
    ) -> bool {
        if stream.empty_nodes.contains(&key) || visible.contains(&key) {
            return true;
        }
        let Some(children) = terrain_node_children(key) else {
            return false;
        };
        children.iter().all(|child| {
            stream.desired.contains(child) && subtree_is_represented(stream, visible, *child)
        })
    }

    trait PendingStatus {
        fn pending(&self) -> bool;
    }

    impl PendingStatus for TerrainStreamStatus {
        fn pending(&self) -> bool {
            self.in_flight_density_count > 0
                || self.in_flight_lod_count > 0
                || self.missing_density_count > 0
                || self.missing_mesh_count > 0
        }
    }
}
