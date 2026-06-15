#[cfg(feature = "benchmark")]
pub mod benchmark;
mod chunk;
mod constants;
mod density;
mod facade;
mod field;
mod material;
mod math;
mod mesh;
mod mesh_packet_store;
mod mesh_surface;
mod node;
mod noise;
mod placement;
mod presets;
mod probe;
mod store;
mod stream;
mod stream_helpers;
mod stream_types;
mod surface_query;
mod surface_query_geometry;
mod transition_mesh;
mod variant;
mod vertical_band;
mod water;
mod worker_pool;

pub(crate) use chunk::*;
pub(crate) use constants::*;
pub(crate) use density::*;
#[allow(unused_imports)]
pub(crate) use facade::*;
pub(crate) use field::*;
pub(crate) use material::*;
pub(crate) use math::*;
pub(crate) use mesh_packet_store::*;
pub(crate) use noise::*;
pub(crate) use presets::*;
pub(crate) use store::*;
#[allow(unused_imports)]
pub(crate) use stream::*;
pub(crate) use stream_helpers::*;
pub(crate) use worker_pool::*;

pub use chunk::{terrain_chunk_coord_containing_position, terrain_chunk_key, TerrainChunkCoord};
pub use constants::{DEFAULT_TERRAIN_PRESET, TERRAIN_CHUNK_CELLS_PER_AXIS};
pub use field::{height_at, height_at_for_variant, height_at_with_shape};
pub use mesh::{
    build_chunk_mesh, build_chunk_mesh_for_variant, build_node_mesh, build_node_mesh_for_variant,
    MeshData,
};
pub use mesh_surface::{build_node_mesh_and_surface_for_variant, TerrainNodeBuildSurface};
pub use node::{
    terrain_node_cell_size, terrain_node_children, terrain_node_coord_for_lod, terrain_node_key,
    terrain_node_parent, TerrainNodeKey,
};
pub use noise::{
    CellularNoiseOptions, DomainWarpOptions, FractalNoiseOptions, RidgedFractalNoiseOptions,
};
pub use placement::{
    build_node_surface_placement_samples_for_variant,
    build_node_surface_placement_samples_for_variant_with_config,
    sample_terrain_placements_from_candidates, terrain_placement_candidates_for_node,
    TerrainPlacementSample, TerrainPlacementSamplePacket, TerrainPlacementSamplingConfig,
};
pub use probe::{
    terrain_variant_probe_summary, TerrainBiomeWeightsProbe, TerrainVariantProbeSummary,
};
pub use stream::TerrainStreamScheduler;
pub use stream_types::{
    TerrainLodBand, TerrainLodBoundedVerticalPolicy, TerrainLodStatus, TerrainLodVerticalPolicy,
    TerrainStreamConfig, TerrainStreamError, TerrainStreamJob, TerrainStreamStatus,
};
pub use surface_query::{TerrainSurfaceHit, TerrainSurfaceIndex, TerrainVerticalQuery};
pub use transition_mesh::{
    build_parent_lod_transition_edge_mesh, TerrainTransitionFace, TerrainTransitionMeshConfig,
    TerrainTransitionMeshInput, TerrainTransitionMeshKey,
};
pub use variant::{
    terrain_preset_count, terrain_preset_metadata, terrain_variant_cache_key,
    terrain_variant_flat_values, terrain_variant_for_preset, terrain_variant_from_flat_values,
    TerrainMaterialBias, TerrainPresetMetadata, TerrainShapeParameters, TerrainVariantDescriptor,
    TerrainVariantValidationError, TERRAIN_BASE_HEIGHT_MAX, TERRAIN_BASE_HEIGHT_MIN,
    TERRAIN_CELLULAR_HEIGHT_SCALE_MAX, TERRAIN_DETAIL_AMPLITUDE_MAX, TERRAIN_HEIGHT_SCALE_MAX,
    TERRAIN_HEIGHT_SCALE_MIN, TERRAIN_RIDGE_HEIGHT_SCALE_MAX, TERRAIN_VARIANT_DESCRIPTOR_VERSION,
    TERRAIN_VARIANT_FLAT_VALUE_COUNT, TERRAIN_WARP_AMPLITUDE_MAX,
};
pub use vertical_band::{
    estimate_terrain_column_world_y_range, terrain_node_column_xz_bounds,
    terrain_node_world_span_y, terrain_node_world_y_span, terrain_world_y_range_to_node_y_range,
    TerrainLodVerticalWindow, TerrainNodeColumnKey, TerrainNodeYRange, TerrainVerticalBoundsConfig,
    TerrainVerticalBoundsError, TerrainWorldYRange,
};
pub use water::{
    build_water_node_packet_for_variant, sea_depth_at_for_variant, WaterNodePacket,
    SEA_LEVEL_METERS, WATER_NODE_BATHYMETRY_TEXEL_COUNT, WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
};

#[cfg(test)]
pub(crate) static TEST_MUTEX: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(test)]
pub(crate) fn test_lock() -> std::sync::MutexGuard<'static, ()> {
    TEST_MUTEX
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
mod tests;

#[cfg(test)]
mod surface_query_tests;

#[cfg(test)]
mod placement_tests;

#[cfg(test)]
mod transition_mesh_tests;
