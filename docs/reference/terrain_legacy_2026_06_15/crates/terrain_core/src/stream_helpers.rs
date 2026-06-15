// Pure stream configuration validation and ordering helpers shared by the
// terrain stream scheduler.

use std::collections::BTreeSet;

use crate::*;

pub(crate) fn validate_stream_config(
    config: &TerrainStreamConfig,
) -> Result<(), TerrainStreamError> {
    if config.lod_bands.is_empty() {
        return Err(TerrainStreamError::EmptyLodBands);
    }

    let mut lods = BTreeSet::new();
    for band in &config.lod_bands {
        if !lods.insert(band.lod) {
            return Err(TerrainStreamError::DuplicateLodBands);
        }

        if band.horizontal_radius < 0 {
            return Err(TerrainStreamError::NegativeHorizontalRadius);
        }

        match &band.vertical {
            TerrainLodVerticalPolicy::FixedOffsets(vertical_offsets) => {
                if vertical_offsets.is_empty() {
                    return Err(TerrainStreamError::EmptyVerticalOffsets);
                }

                let unique_offsets: BTreeSet<i32> = vertical_offsets.iter().copied().collect();
                if unique_offsets.len() != vertical_offsets.len() {
                    return Err(TerrainStreamError::DuplicateVerticalOffsets);
                }
            }
            TerrainLodVerticalPolicy::Bounded(policy) => {
                policy.validate()?;
            }
        }
    }

    if config.max_in_flight_jobs == 0 {
        return Err(TerrainStreamError::ZeroMaxInFlightJobs);
    }

    config
        .terrain_variant
        .validate()
        .map_err(|_| TerrainStreamError::InvalidTerrainVariant)?;
    if !config.base_cell_size.is_finite() || config.base_cell_size <= 0.0 {
        return Err(TerrainStreamError::InvalidBaseCellSize);
    }

    Ok(())
}

pub(crate) fn node_priority(
    key: TerrainNodeKey,
    center_coord: TerrainChunkCoord,
) -> (u8, i64, TerrainNodeKey) {
    let center = terrain_node_coord_for_lod(center_coord, key.lod);
    let dx = i64::from(key.coord.x - center.x);
    let dy = i64::from((key.coord.y - center.y).abs());
    let dz = i64::from(key.coord.z - center.z);
    let distance = (dx * dx + dz * dz) * 2 + dy;

    (u8::MAX - key.lod, distance, key)
}
