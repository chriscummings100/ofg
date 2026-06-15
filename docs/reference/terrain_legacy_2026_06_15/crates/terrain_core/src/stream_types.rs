// Public terrain stream types shared by the scheduler, facade, and browser
// runtime integration.

use crate::{
    terrain_variant_for_preset, TerrainLodVerticalWindow, TerrainNodeKey, TerrainVariantDescriptor,
    TerrainVerticalBoundsConfig, TerrainVerticalBoundsError, DEFAULT_TERRAIN_PRESET,
};

#[derive(Clone, Debug, PartialEq)]
pub struct TerrainLodBand {
    pub lod: u8,
    pub horizontal_radius: i32,
    pub vertical: TerrainLodVerticalPolicy,
}

impl TerrainLodBand {
    /// Builds an LOD band that uses legacy fixed Y offsets around the player.
    pub fn fixed_offsets(lod: u8, horizontal_radius: i32, vertical_offsets: Vec<i32>) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::FixedOffsets(vertical_offsets),
        }
    }

    /// Builds an LOD band that resolves Y ranges from terrain bounds and player windows.
    pub fn bounded(
        lod: u8,
        horizontal_radius: i32,
        policy: TerrainLodBoundedVerticalPolicy,
    ) -> Self {
        Self {
            lod,
            horizontal_radius,
            vertical: TerrainLodVerticalPolicy::Bounded(policy),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum TerrainLodVerticalPolicy {
    FixedOffsets(Vec<i32>),
    Bounded(TerrainLodBoundedVerticalPolicy),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainLodBoundedVerticalPolicy {
    pub below_player_nodes: i32,
    pub above_player_nodes: i32,
    pub surface_padding_below_m: f64,
    pub surface_padding_above_m: f64,
    pub feature_padding_below_m: f64,
    pub feature_padding_above_m: f64,
    pub sample_steps_per_axis: u8,
}

impl TerrainLodBoundedVerticalPolicy {
    /// Builds a bounded vertical policy from a valid node window and default estimator padding.
    pub fn new(below_player_nodes: i32, above_player_nodes: i32) -> Option<Self> {
        TerrainLodVerticalWindow::new(below_player_nodes, above_player_nodes)?;
        let bounds = TerrainVerticalBoundsConfig::default();
        Some(Self {
            below_player_nodes,
            above_player_nodes,
            surface_padding_below_m: bounds.surface_padding_below_m,
            surface_padding_above_m: bounds.surface_padding_above_m,
            feature_padding_below_m: bounds.feature_padding_below_m,
            feature_padding_above_m: bounds.feature_padding_above_m,
            sample_steps_per_axis: bounds.sample_steps_per_axis,
        })
    }

    /// Returns the player-centered vertical node window for this policy.
    pub fn vertical_window(self) -> Option<TerrainLodVerticalWindow> {
        TerrainLodVerticalWindow::new(self.below_player_nodes, self.above_player_nodes)
    }

    /// Returns the terrain-interest estimator configuration for this policy.
    pub fn bounds_config(self) -> TerrainVerticalBoundsConfig {
        TerrainVerticalBoundsConfig {
            surface_padding_below_m: self.surface_padding_below_m,
            surface_padding_above_m: self.surface_padding_above_m,
            feature_padding_below_m: self.feature_padding_below_m,
            feature_padding_above_m: self.feature_padding_above_m,
            sample_steps_per_axis: self.sample_steps_per_axis,
        }
    }

    /// Validates the node window and estimator settings.
    pub fn validate(self) -> Result<(), TerrainStreamError> {
        self.vertical_window()
            .ok_or(TerrainStreamError::InvalidVerticalWindow)?;
        self.bounds_config()
            .validate()
            .map_err(|error| match error {
                TerrainVerticalBoundsError::InvalidPadding
                | TerrainVerticalBoundsError::InvalidSampleGrid => {
                    TerrainStreamError::InvalidVerticalBounds
                }
                TerrainVerticalBoundsError::InvalidBaseCellSize
                | TerrainVerticalBoundsError::InvalidTerrainVariant(_) => {
                    TerrainStreamError::InvalidVerticalBounds
                }
            })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct TerrainStreamConfig {
    pub lod_bands: Vec<TerrainLodBand>,
    pub max_in_flight_jobs: usize,
    pub terrain_seed: u32,
    pub terrain_variant: TerrainVariantDescriptor,
    pub base_cell_size: f64,
}

impl TerrainStreamConfig {
    /// Builds a single-LOD fixed-offset stream config for fixture and unit tests.
    pub fn single_lod0(
        horizontal_radius: i32,
        vertical_chunk_offsets: Vec<i32>,
        max_in_flight_jobs: usize,
    ) -> Self {
        Self {
            lod_bands: vec![TerrainLodBand::fixed_offsets(
                0,
                horizontal_radius,
                vertical_chunk_offsets,
            )],
            max_in_flight_jobs,
            terrain_seed: 0x0F6,
            terrain_variant: terrain_variant_for_preset(DEFAULT_TERRAIN_PRESET),
            base_cell_size: 1.0,
        }
    }
}

impl Default for TerrainStreamConfig {
    fn default() -> Self {
        Self::single_lod0(1, vec![-1, 0, 1], 1)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainStreamJob {
    BuildNode {
        generation: u64,
        key: TerrainNodeKey,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainStreamError {
    EmptyLodBands,
    DuplicateLodBands,
    NegativeHorizontalRadius,
    EmptyVerticalOffsets,
    DuplicateVerticalOffsets,
    InvalidVerticalWindow,
    InvalidVerticalBounds,
    InvalidTerrainVariant,
    InvalidBaseCellSize,
    ZeroMaxInFlightJobs,
}

impl std::fmt::Display for TerrainStreamError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::EmptyLodBands => "empty terrain stream LOD bands",
            Self::DuplicateLodBands => "duplicate terrain stream LOD bands",
            Self::NegativeHorizontalRadius => "negative terrain stream horizontal radius",
            Self::EmptyVerticalOffsets => "empty terrain stream vertical offsets",
            Self::DuplicateVerticalOffsets => "duplicate terrain stream vertical offsets",
            Self::InvalidVerticalWindow => "invalid terrain stream vertical window",
            Self::InvalidVerticalBounds => "invalid terrain stream vertical bounds",
            Self::InvalidTerrainVariant => "invalid terrain stream terrain variant",
            Self::InvalidBaseCellSize => "invalid terrain stream base cell size",
            Self::ZeroMaxInFlightJobs => "zero terrain stream max in-flight jobs",
        };

        formatter.write_str(message)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainLodStatus {
    pub lod: u8,
    pub desired_node_count: usize,
    pub min_desired_node_y: Option<i32>,
    pub max_desired_node_y: Option<i32>,
    pub density_ready_node_count: usize,
    pub rendered_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainStreamStatus {
    pub generation: u64,
    pub desired_density_count: usize,
    pub desired_lod0_count: usize,
    pub desired_mesh_count: usize,
    pub density_ready_count: usize,
    pub lod0_ready_count: usize,
    pub lod0_empty_count: usize,
    pub mesh_ready_count: usize,
    pub mesh_empty_count: usize,
    pub in_flight_density_count: usize,
    pub in_flight_lod_count: usize,
    pub missing_density_count: usize,
    pub missing_lod0_count: usize,
    pub missing_mesh_count: usize,
    pub max_in_flight_jobs: usize,
    pub lod_summaries: Vec<TerrainLodStatus>,
}
