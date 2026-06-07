// Public terrain stream types shared by the scheduler, facade, and browser
// runtime integration.

use crate::TerrainNodeKey;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainLodBand {
    pub lod: u8,
    pub horizontal_radius: i32,
    pub vertical_chunk_offsets: Vec<i32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainStreamConfig {
    pub lod_bands: Vec<TerrainLodBand>,
    pub max_in_flight_jobs: usize,
}

impl TerrainStreamConfig {
    pub fn single_lod0(
        horizontal_radius: i32,
        vertical_chunk_offsets: Vec<i32>,
        max_in_flight_jobs: usize,
    ) -> Self {
        Self {
            lod_bands: vec![TerrainLodBand {
                lod: 0,
                horizontal_radius,
                vertical_chunk_offsets,
            }],
            max_in_flight_jobs,
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
            Self::ZeroMaxInFlightJobs => "zero terrain stream max in-flight jobs",
        };

        formatter.write_str(message)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainLodStatus {
    pub lod: u8,
    pub desired_node_count: usize,
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
