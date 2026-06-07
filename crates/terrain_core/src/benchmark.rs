// Rust-only terrain benchmark helpers. This module exists for repository tools
// that need density and store phase timings without turning TypeScript or
// browser smoke tests back into terrain clients.

use crate::*;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DensityWindowBounds {
    pub min_x: i32,
    pub min_y: i32,
    pub min_z: i32,
    pub max_x: i32,
    pub max_y: i32,
    pub max_z: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DensityStoreStats {
    pub entries: usize,
    pub max_entries: usize,
    pub reuses: u64,
    pub generations: u64,
    pub evictions: u64,
}

/// Returns the number of density samples in one terrain chunk.
pub fn density_chunk_sample_count() -> usize {
    TERRAIN_CHUNK_SAMPLE_COUNT
}

/// Generates one density chunk and returns its samples in terrain chunk order.
pub fn fill_density_chunk(
    seed: u32,
    preset: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> Vec<f32> {
    if cell_size <= 0.0 {
        return Vec::new();
    }

    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);
    generate_density_chunk(&noise, preset, seed, coord, cell_size).densities
}

/// Clears the retained density chunk store used by terrain mesh generation.
pub fn reset_density_store() {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .reset();
}

/// Returns current density chunk store counters.
pub fn density_store_stats() -> DensityStoreStats {
    let store = density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned");
    DensityStoreStats {
        entries: store.entries.len(),
        max_entries: store.max_entries,
        reuses: store.reuses,
        generations: store.generations,
        evictions: store.evictions,
    }
}

/// Retains and prepares every density chunk in an inclusive chunk-coordinate window.
pub fn prepare_density_chunk_window(
    seed: u32,
    preset: u32,
    bounds: DensityWindowBounds,
    cell_size: f64,
) -> usize {
    if cell_size <= 0.0 {
        return 0;
    }

    let min_x = bounds.min_x.min(bounds.max_x);
    let max_x = bounds.min_x.max(bounds.max_x);
    let min_y = bounds.min_y.min(bounds.max_y);
    let max_y = bounds.min_y.max(bounds.max_y);
    let min_z = bounds.min_z.min(bounds.max_z);
    let max_z = bounds.min_z.max(bounds.max_z);
    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);

    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .retain_window(
            seed, preset_id, cell_size, min_x, min_y, min_z, max_x, max_y, max_z,
        );

    let mut prepared = 0;
    for z in min_z..=max_z {
        for y in min_y..=max_y {
            for x in min_x..=max_x {
                ensure_density_chunk_stored(
                    &noise,
                    preset,
                    preset_id,
                    seed,
                    TerrainChunkCoord { x, y, z },
                    cell_size,
                );
                prepared += 1;
            }
        }
    }

    prepared
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn benchmark_fill_returns_one_density_chunk() {
        let _lock = crate::test_lock();
        let samples = fill_density_chunk(0x0F6, 1, TerrainChunkCoord { x: -1, y: 0, z: 2 }, 1.0);

        assert_eq!(samples.len(), density_chunk_sample_count());
        assert!(samples.iter().all(|sample| sample.is_finite()));
    }

    #[test]
    fn benchmark_density_window_prepares_and_reuses_store_entries() {
        let _lock = crate::test_lock();
        reset_density_store();
        let bounds = DensityWindowBounds {
            min_x: 0,
            min_y: 0,
            min_z: 0,
            max_x: 1,
            max_y: 0,
            max_z: 0,
        };

        assert_eq!(prepare_density_chunk_window(0x0F6, 1, bounds, 1.0), 2);
        let after_first_prepare = density_store_stats();
        assert_eq!(after_first_prepare.entries, 2);
        assert_eq!(after_first_prepare.generations, 2);

        assert_eq!(prepare_density_chunk_window(0x0F6, 1, bounds, 1.0), 2);
        let after_second_prepare = density_store_stats();
        assert_eq!(after_second_prepare.entries, 2);
        assert!(after_second_prepare.reuses >= 2);
    }
}
