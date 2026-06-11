use std::sync::{Mutex, OnceLock};

use crate::*;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct DensityChunkStoreKey {
    pub(crate) seed: u32,
    pub(crate) preset: u32,
    pub(crate) variant_cache_key: u64,
    pub(crate) chunk_x: i32,
    pub(crate) chunk_y: i32,
    pub(crate) chunk_z: i32,
    pub(crate) cell_size_bits: u64,
}

pub(crate) struct StoredDensityChunk {
    pub(crate) key: DensityChunkStoreKey,
    pub(crate) densities: Vec<f32>,
    pub(crate) last_used: u64,
}

pub(crate) struct DensityChunkStore {
    pub(crate) entries: Vec<StoredDensityChunk>,
    pub(crate) tick: u64,
    pub(crate) reuses: u64,
    pub(crate) generations: u64,
    pub(crate) evictions: u64,
    pub(crate) max_entries: usize,
}

pub(crate) static DENSITY_CHUNK_STORE: OnceLock<Mutex<DensityChunkStore>> = OnceLock::new();

pub(crate) fn density_chunk_store() -> &'static Mutex<DensityChunkStore> {
    DENSITY_CHUNK_STORE.get_or_init(|| Mutex::new(DensityChunkStore::new()))
}

pub(crate) fn density_chunk_store_key(
    seed: u32,
    preset: u32,
    variant_cache_key: u64,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> DensityChunkStoreKey {
    DensityChunkStoreKey {
        seed,
        preset,
        variant_cache_key,
        chunk_x: coord.x,
        chunk_y: coord.y,
        chunk_z: coord.z,
        cell_size_bits: cell_size.to_bits(),
    }
}

impl DensityChunkStore {
    fn new() -> Self {
        Self {
            entries: Vec::new(),
            tick: 0,
            reuses: 0,
            generations: 0,
            evictions: 0,
            max_entries: DENSITY_CHUNK_STORE_MAX_ENTRIES,
        }
    }

    pub(crate) fn reset(&mut self) {
        self.entries.clear();
        self.tick = 0;
        self.reuses = 0;
        self.generations = 0;
        self.evictions = 0;
    }

    pub(crate) fn get(&mut self, key: DensityChunkStoreKey) -> Option<Vec<f32>> {
        self.tick = self.tick.wrapping_add(1);
        for entry in &mut self.entries {
            if entry.key == key {
                entry.last_used = self.tick;
                self.reuses = self.reuses.wrapping_add(1);
                return Some(entry.densities.clone());
            }
        }

        None
    }

    pub(crate) fn insert(&mut self, key: DensityChunkStoreKey, densities: Vec<f32>) {
        self.tick = self.tick.wrapping_add(1);
        for entry in &mut self.entries {
            if entry.key == key {
                entry.densities = densities;
                entry.last_used = self.tick;
                return;
            }
        }

        self.entries.push(StoredDensityChunk {
            key,
            densities,
            last_used: self.tick,
        });
        self.generations = self.generations.wrapping_add(1);
        self.evict_until_within_budget();
    }

    pub(crate) fn retain_window(
        &mut self,
        seed: u32,
        preset: u32,
        variant_cache_key: u64,
        cell_size: f64,
        min_x: i32,
        min_y: i32,
        min_z: i32,
        max_x: i32,
        max_y: i32,
        max_z: i32,
    ) {
        let cell_size_bits = cell_size.to_bits();
        let before = self.entries.len();
        self.entries.retain(|entry| {
            entry.key.seed == seed
                && entry.key.preset == preset
                && entry.key.variant_cache_key == variant_cache_key
                && entry.key.cell_size_bits == cell_size_bits
                && entry.key.chunk_x >= min_x
                && entry.key.chunk_x <= max_x
                && entry.key.chunk_y >= min_y
                && entry.key.chunk_y <= max_y
                && entry.key.chunk_z >= min_z
                && entry.key.chunk_z <= max_z
        });
        self.evictions = self
            .evictions
            .wrapping_add((before - self.entries.len()) as u64);
    }

    fn evict_until_within_budget(&mut self) {
        while self.entries.len() > self.max_entries {
            let mut oldest_index = 0;
            let mut oldest_tick = self.entries[0].last_used;
            for (index, entry) in self.entries.iter().enumerate().skip(1) {
                if entry.last_used < oldest_tick {
                    oldest_tick = entry.last_used;
                    oldest_index = index;
                }
            }
            self.entries.swap_remove(oldest_index);
            self.evictions = self.evictions.wrapping_add(1);
        }
    }
}
