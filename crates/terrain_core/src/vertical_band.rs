// Vertical terrain band primitives used to convert world-space terrain interest
// envelopes into inclusive terrain node Y ranges.

use crate::{
    height_at_with_shape, terrain_node_cell_size, TerrainChunkCoord, TerrainNodeKey,
    TerrainVariantDescriptor, TerrainVariantValidationError, TERRAIN_CHUNK_CELLS_PER_AXIS,
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainWorldYRange {
    pub min_y: f64,
    pub max_y: f64,
}

impl TerrainWorldYRange {
    /// Builds a finite, non-inverted world-space Y range in meters.
    pub fn new(min_y: f64, max_y: f64) -> Option<Self> {
        if !min_y.is_finite() || !max_y.is_finite() || min_y > max_y {
            return None;
        }

        Some(Self { min_y, max_y })
    }

    /// Returns this range expanded by non-negative world-space margins.
    pub fn expanded(self, below_m: f64, above_m: f64) -> Option<Self> {
        if !below_m.is_finite() || !above_m.is_finite() || below_m < 0.0 || above_m < 0.0 {
            return None;
        }

        Self::new(self.min_y - below_m, self.max_y + above_m)
    }

    /// Returns the intersection between this range and another world-space range.
    pub fn intersect(self, other: Self) -> Option<Self> {
        Self::new(self.min_y.max(other.min_y), self.max_y.min(other.max_y))
    }

    /// Returns true when this inclusive range contains a world-space Y value.
    pub fn contains(self, y: f64) -> bool {
        y.is_finite() && self.min_y <= y && y <= self.max_y
    }

    /// Returns the world-space height covered by this range.
    pub fn span_m(self) -> f64 {
        self.max_y - self.min_y
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainNodeYRange {
    pub min_y: i32,
    pub max_y: i32,
}

impl TerrainNodeYRange {
    /// Builds a non-inverted inclusive node Y range.
    pub fn new(min_y: i32, max_y: i32) -> Option<Self> {
        if min_y > max_y {
            return None;
        }

        Some(Self { min_y, max_y })
    }

    /// Returns true when this inclusive range contains `y`.
    pub fn contains(self, y: i32) -> bool {
        self.min_y <= y && y <= self.max_y
    }

    /// Returns the number of integer node coordinates in this range.
    pub fn len(self) -> u32 {
        i64::from(self.max_y)
            .saturating_sub(i64::from(self.min_y))
            .saturating_add(1)
            .clamp(0, i64::from(u32::MAX)) as u32
    }

    /// Returns every integer node Y coordinate in this range.
    pub fn iter(self) -> TerrainNodeYRangeIter {
        TerrainNodeYRangeIter {
            next_y: self.min_y,
            end_y: self.max_y,
            exhausted: false,
        }
    }

    /// Returns this range expanded by non-negative integer node margins.
    pub fn expanded(self, below_nodes: i32, above_nodes: i32) -> Option<Self> {
        if below_nodes < 0 || above_nodes < 0 {
            return None;
        }

        Some(Self {
            min_y: self.min_y.saturating_sub(below_nodes),
            max_y: self.max_y.saturating_add(above_nodes),
        })
    }

    /// Returns the intersection between this range and another node Y range.
    pub fn intersect(self, other: Self) -> Option<Self> {
        Self::new(self.min_y.max(other.min_y), self.max_y.min(other.max_y))
    }
}

pub struct TerrainNodeYRangeIter {
    next_y: i32,
    end_y: i32,
    exhausted: bool,
}

impl Iterator for TerrainNodeYRangeIter {
    type Item = i32;

    /// Returns the next inclusive node Y coordinate.
    fn next(&mut self) -> Option<Self::Item> {
        if self.exhausted {
            return None;
        }

        let value = self.next_y;
        if value == self.end_y {
            self.exhausted = true;
        } else {
            self.next_y = self.next_y.saturating_add(1);
        }

        Some(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainLodVerticalWindow {
    pub below_player_nodes: i32,
    pub above_player_nodes: i32,
}

impl TerrainLodVerticalWindow {
    /// Builds a player-centered vertical window with non-negative node margins.
    pub fn new(below_player_nodes: i32, above_player_nodes: i32) -> Option<Self> {
        if below_player_nodes < 0 || above_player_nodes < 0 {
            return None;
        }

        Some(Self {
            below_player_nodes,
            above_player_nodes,
        })
    }

    /// Returns the inclusive node Y range covered around a player node coordinate.
    pub fn node_range_around(self, player_node_y: i32) -> TerrainNodeYRange {
        TerrainNodeYRange {
            min_y: player_node_y.saturating_sub(self.below_player_nodes),
            max_y: player_node_y.saturating_add(self.above_player_nodes),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainNodeColumnKey {
    pub lod: u8,
    pub x: i32,
    pub z: i32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainVerticalBoundsConfig {
    pub surface_padding_below_m: f64,
    pub surface_padding_above_m: f64,
    pub feature_padding_below_m: f64,
    pub feature_padding_above_m: f64,
    pub sample_steps_per_axis: u8,
}

impl TerrainVerticalBoundsConfig {
    /// Validates estimator padding and sampling settings.
    pub fn validate(self) -> Result<(), TerrainVerticalBoundsError> {
        for padding in [
            self.surface_padding_below_m,
            self.surface_padding_above_m,
            self.feature_padding_below_m,
            self.feature_padding_above_m,
        ] {
            if !padding.is_finite() || padding < 0.0 {
                return Err(TerrainVerticalBoundsError::InvalidPadding);
            }
        }

        if !(2..=17).contains(&self.sample_steps_per_axis) {
            return Err(TerrainVerticalBoundsError::InvalidSampleGrid);
        }

        Ok(())
    }
}

impl Default for TerrainVerticalBoundsConfig {
    /// Returns the default conservative column estimator configuration.
    fn default() -> Self {
        Self {
            surface_padding_below_m: 2.0,
            surface_padding_above_m: 2.0,
            feature_padding_below_m: 0.0,
            feature_padding_above_m: 0.0,
            sample_steps_per_axis: 3,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainVerticalBoundsError {
    InvalidTerrainVariant(TerrainVariantValidationError),
    InvalidBaseCellSize,
    InvalidPadding,
    InvalidSampleGrid,
}

impl std::fmt::Display for TerrainVerticalBoundsError {
    /// Formats a human-readable vertical bounds estimator error.
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            TerrainVerticalBoundsError::InvalidTerrainVariant(error) => {
                return write!(formatter, "invalid terrain variant: {error}");
            }
            TerrainVerticalBoundsError::InvalidBaseCellSize => {
                "terrain vertical bounds base cell size is invalid"
            }
            TerrainVerticalBoundsError::InvalidPadding => {
                "terrain vertical bounds padding is invalid"
            }
            TerrainVerticalBoundsError::InvalidSampleGrid => {
                "terrain vertical bounds sample grid is invalid"
            }
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for TerrainVerticalBoundsError {}

impl From<TerrainVariantValidationError> for TerrainVerticalBoundsError {
    /// Wraps terrain variant validation errors for the estimator boundary.
    fn from(error: TerrainVariantValidationError) -> Self {
        TerrainVerticalBoundsError::InvalidTerrainVariant(error)
    }
}

impl TerrainNodeColumnKey {
    /// Creates a Y-less terrain column identity from a full terrain node key.
    pub fn from_node(key: TerrainNodeKey) -> Self {
        Self {
            lod: key.lod,
            x: key.coord.x,
            z: key.coord.z,
        }
    }

    /// Returns the full terrain node key for this column and a chosen Y coordinate.
    pub fn with_y(self, y: i32) -> TerrainNodeKey {
        TerrainNodeKey {
            lod: self.lod,
            coord: TerrainChunkCoord {
                x: self.x,
                y,
                z: self.z,
            },
        }
    }
}

/// Estimates the world Y interval that may contain terrain in one node column.
pub fn estimate_terrain_column_world_y_range(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    column: TerrainNodeColumnKey,
    base_cell_size: f64,
    config: TerrainVerticalBoundsConfig,
) -> Result<TerrainWorldYRange, TerrainVerticalBoundsError> {
    descriptor.validate()?;
    config.validate()?;

    let (min_x, min_z, max_x, max_z) = terrain_node_column_xz_bounds(column, base_cell_size)
        .ok_or(TerrainVerticalBoundsError::InvalidBaseCellSize)?;
    let sample_steps = usize::from(config.sample_steps_per_axis);
    let denominator = (sample_steps - 1) as f64;
    let mut min_height = f64::INFINITY;
    let mut max_height = f64::NEG_INFINITY;

    for z_index in 0..sample_steps {
        let z_t = z_index as f64 / denominator;
        let z = lerp(min_z, max_z, z_t);
        for x_index in 0..sample_steps {
            let x_t = x_index as f64 / denominator;
            let x = lerp(min_x, max_x, x_t);
            let height = height_at_with_shape(seed, descriptor.shape, x, z);
            min_height = min_height.min(height);
            max_height = max_height.max(height);
        }
    }

    let shape_padding = terrain_shape_sampling_padding_m(descriptor);
    TerrainWorldYRange::new(min_height, max_height)
        .and_then(|range| {
            range.expanded(
                shape_padding + config.surface_padding_below_m + config.feature_padding_below_m,
                shape_padding + config.surface_padding_above_m + config.feature_padding_above_m,
            )
        })
        .ok_or(TerrainVerticalBoundsError::InvalidPadding)
}

/// Returns the world-space height of one vertical terrain node at `lod`.
pub fn terrain_node_world_span_y(lod: u8, base_cell_size: f64) -> Option<f64> {
    if !base_cell_size.is_finite() || base_cell_size <= 0.0 {
        return None;
    }

    let cell_size = terrain_node_cell_size(base_cell_size, lod);
    let span = cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    if span.is_finite() && span > 0.0 {
        Some(span)
    } else {
        None
    }
}

/// Returns the world-space Y span covered by one terrain node coordinate.
pub fn terrain_node_world_y_span(
    lod: u8,
    coord_y: i32,
    base_cell_size: f64,
) -> Option<TerrainWorldYRange> {
    let node_span = terrain_node_world_span_y(lod, base_cell_size)?;
    let min_y = f64::from(coord_y) * node_span;
    TerrainWorldYRange::new(min_y, min_y + node_span)
}

/// Converts a world Y range into every node Y coordinate touched by that range.
pub fn terrain_world_y_range_to_node_y_range(
    range: TerrainWorldYRange,
    lod: u8,
    base_cell_size: f64,
) -> Option<TerrainNodeYRange> {
    let node_span = terrain_node_world_span_y(lod, base_cell_size)?;
    let min_y = floor_to_i32_saturating(range.min_y / node_span);
    let max_y = floor_to_i32_saturating(range.max_y / node_span);
    TerrainNodeYRange::new(min_y, max_y)
}

/// Returns the full world-space X/Z footprint for a node column.
pub fn terrain_node_column_xz_bounds(
    column: TerrainNodeColumnKey,
    base_cell_size: f64,
) -> Option<(f64, f64, f64, f64)> {
    let node_span = terrain_node_world_span_y(column.lod, base_cell_size)?;
    let min_x = f64::from(column.x) * node_span;
    let min_z = f64::from(column.z) * node_span;

    Some((min_x, min_z, min_x + node_span, min_z + node_span))
}

/// Returns linear interpolation between two world coordinates.
fn lerp(min: f64, max: f64, t: f64) -> f64 {
    min + (max - min) * t
}

/// Returns conservative per-column padding for unsampled terrain shape extrema.
fn terrain_shape_sampling_padding_m(descriptor: TerrainVariantDescriptor) -> f64 {
    let shape = descriptor.shape;
    let one_sided_shape_extent = shape.height_scale.abs()
        + shape.ridge_height_scale.abs()
        + shape.cellular_height_scale.abs()
        + shape.detail_amplitude.abs();

    one_sided_shape_extent * 2.0
}

/// Floors a finite coordinate ratio into the terrain node grid with saturation.
fn floor_to_i32_saturating(value: f64) -> i32 {
    if value.is_nan() {
        return 0;
    }

    value
        .floor()
        .clamp(f64::from(i32::MIN), f64::from(i32::MAX)) as i32
}

#[cfg(test)]
#[path = "vertical_band_tests.rs"]
mod tests;
