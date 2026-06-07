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

        if band.vertical_chunk_offsets.is_empty() {
            return Err(TerrainStreamError::EmptyVerticalOffsets);
        }

        let unique_offsets: BTreeSet<i32> = band.vertical_chunk_offsets.iter().copied().collect();
        if unique_offsets.len() != band.vertical_chunk_offsets.len() {
            return Err(TerrainStreamError::DuplicateVerticalOffsets);
        }
    }

    if config.max_in_flight_jobs == 0 {
        return Err(TerrainStreamError::ZeroMaxInFlightJobs);
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
