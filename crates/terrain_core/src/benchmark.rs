//! Minimal benchmark helpers for the sine-wave terrain baseline.

use crate::{build_chunk_mesh, MeshData, TerrainChunkCoord};

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct DensityStoreStats {
    pub entries: usize,
    pub max_entries: usize,
    pub reuses: u64,
    pub generations: u64,
    pub evictions: u64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct DensityWindowBounds {
    pub min_x: i32,
    pub min_y: i32,
    pub min_z: i32,
    pub max_x: i32,
    pub max_y: i32,
    pub max_z: i32,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TerrainNodePopulationProfileReport {
    pub sample_count: usize,
}

/// Returns the number of samples in one baseline node.
pub fn density_chunk_sample_count() -> usize {
    (crate::TERRAIN_NODE_SAMPLES_PER_AXIS as usize).pow(3)
}

/// Fills a simple density-like buffer for benchmark compatibility.
pub fn fill_density_chunk(
    seed: u32,
    preset: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> Vec<f32> {
    let mesh = build_chunk_mesh(seed, preset, coord, cell_size);
    density_like_values(&mesh)
}

/// Prepares no retained density because the baseline builds nodes directly.
pub fn prepare_density_chunk_window(
    _seed: u32,
    _preset: u32,
    _bounds: DensityWindowBounds,
    _cell_size: f64,
) -> usize {
    0
}

/// Resets the removed density store compatibility surface.
pub fn reset_density_store() {}

/// Reports an empty density store.
pub fn density_store_stats() -> DensityStoreStats {
    DensityStoreStats::default()
}

/// Profiles no additional population data in the baseline.
pub fn profile_terrain_node_population(_scenarios: &[()]) -> TerrainNodePopulationProfileReport {
    TerrainNodePopulationProfileReport { sample_count: 0 }
}

fn density_like_values(mesh: &MeshData) -> Vec<f32> {
    if mesh.vertices.is_empty() {
        return vec![0.0];
    }
    mesh.vertices.chunks(19).map(|vertex| vertex[1]).collect()
}
