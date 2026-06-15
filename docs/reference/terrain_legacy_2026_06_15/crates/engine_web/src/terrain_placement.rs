// Runtime terrain placement diagnostics owned by Rust terrain streaming.

use std::collections::BTreeMap;
use std::sync::Arc;

use terrain_core::{
    sample_terrain_placements_from_candidates, terrain_placement_candidates_for_node, MeshData,
    TerrainNodeKey, TerrainPlacementSamplePacket, TerrainPlacementSamplingConfig,
    TerrainSurfaceIndex,
};

#[derive(Default)]
pub(crate) struct TerrainPlacementCounterTotals {
    pub(crate) candidate_count: usize,
    pub(crate) sample_count: usize,
    pub(crate) missed_surface_count: usize,
    pub(crate) rejected_below_water_count: usize,
    pub(crate) rejected_slope_count: usize,
}

/// Builds a debug placement sample packet from one accepted terrain mesh.
pub(crate) fn build_surface_placement_packet(
    seed: u32,
    key: TerrainNodeKey,
    base_cell_size: f64,
    node_cell_size: f64,
    mesh: &MeshData,
) -> Option<TerrainPlacementSamplePacket> {
    let config = TerrainPlacementSamplingConfig::default();
    let surface = TerrainSurfaceIndex::from_mesh(key, node_cell_size, mesh)?;
    let candidates = terrain_placement_candidates_for_node(
        seed,
        key,
        base_cell_size,
        config.candidate_grid_axis,
    );

    Some(sample_terrain_placements_from_candidates(
        seed,
        &surface,
        &candidates,
        config,
    ))
}

/// Aggregates cached placement packets into debug stream counters.
pub(crate) fn placement_counter_totals(
    packets: &BTreeMap<TerrainNodeKey, Arc<TerrainPlacementSamplePacket>>,
) -> TerrainPlacementCounterTotals {
    packets.values().fold(
        TerrainPlacementCounterTotals::default(),
        |mut total, packet| {
            total.candidate_count += packet.candidate_count;
            total.sample_count += packet.accepted_count;
            total.missed_surface_count += packet.missed_surface_count;
            total.rejected_below_water_count += packet.rejected_below_water_count;
            total.rejected_slope_count += packet.rejected_slope_count;
            total
        },
    )
}
