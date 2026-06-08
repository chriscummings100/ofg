// Rust-only terrain benchmark helpers. This module exists for repository tools
// that need density and store phase timings without turning TypeScript or
// browser smoke tests back into terrain clients.

use std::time::Instant;

use crate::mesh::{build_neighbor_aware_chunk_mesh_raw, neighbor_chunk};
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainNodeBuildClass {
    EmptyAir,
    Solid,
    SurfaceSparse,
    SurfaceHeavy,
    SurfaceComplex,
}

#[derive(Clone, Debug, PartialEq)]
pub struct TerrainNodeBuildProfile {
    pub key: TerrainNodeKey,
    pub cell_size: f64,
    pub class: TerrainNodeBuildClass,
    pub total_ms: f64,
    pub density_ms: f64,
    pub contouring_ms: f64,
    pub material_ms: f64,
    pub copy_ms: f64,
    pub prepared_total_ms: f64,
    pub prepared_density_ms: f64,
    pub prepared_contouring_ms: f64,
    pub prepared_material_ms: f64,
    pub prepared_copy_ms: f64,
    pub reused_density_chunks: u64,
    pub generated_density_chunks: u64,
    pub evicted_density_chunks: u64,
    pub prepared_reused_density_chunks: u64,
    pub prepared_generated_density_chunks: u64,
    pub prepared_evicted_density_chunks: u64,
    pub raw_vertex_count: usize,
    pub raw_index_count: usize,
    pub vertex_count: usize,
    pub index_count: usize,
    pub vertex_bytes: usize,
    pub index_bytes: usize,
    pub copy_checksum: f64,
}

impl TerrainNodeBuildClass {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::EmptyAir => "emptyAir",
            Self::Solid => "solid",
            Self::SurfaceSparse => "surfaceSparse",
            Self::SurfaceHeavy => "surfaceHeavy",
            Self::SurfaceComplex => "surfaceComplex",
        }
    }
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

/// Builds one terrain node and records coarse generation phase timings.
pub fn profile_node_mesh_build(
    seed: u32,
    preset: u32,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> TerrainNodeBuildProfile {
    if base_cell_size <= 0.0 {
        return empty_profile(key, base_cell_size);
    }

    let total_started_at = Instant::now();
    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);
    let cell_size = terrain_node_cell_size(base_cell_size, key.lod);

    let density_stats_before = density_store_stats();
    let density_started_at = Instant::now();
    let chunks =
        generate_neighbor_apron_chunks(&noise, preset, preset_id, seed, key.coord, cell_size);
    let density_ms = elapsed_ms(density_started_at);
    let density_stats_after = density_store_stats();

    let contouring_started_at = Instant::now();
    let raw_mesh = build_neighbor_aware_chunk_mesh_raw(&noise, preset, seed, &chunks, key.coord);
    let contouring_ms = elapsed_ms(contouring_started_at);

    let material_started_at = Instant::now();
    let mesh =
        expand_terrain_mesh_for_triangle_material_palettes(&raw_mesh.vertices, &raw_mesh.indices);
    let material_ms = elapsed_ms(material_started_at);

    let copy_started_at = Instant::now();
    let copied_vertices = mesh.vertices.clone();
    let copied_indices = mesh.indices.clone();
    let copy_checksum = mesh_copy_checksum(&copied_vertices, &copied_indices);
    let copy_ms = elapsed_ms(copy_started_at);
    let cold_total_ms = elapsed_ms(total_started_at);
    let prepared = profile_prepared_node_build(&noise, preset, preset_id, seed, key, cell_size);

    TerrainNodeBuildProfile {
        key,
        cell_size,
        class: classify_profiled_node(&chunks, key.coord, &mesh),
        total_ms: cold_total_ms,
        density_ms,
        contouring_ms,
        material_ms,
        copy_ms,
        prepared_total_ms: prepared.total_ms,
        prepared_density_ms: prepared.density_ms,
        prepared_contouring_ms: prepared.contouring_ms,
        prepared_material_ms: prepared.material_ms,
        prepared_copy_ms: prepared.copy_ms,
        reused_density_chunks: density_stats_after
            .reuses
            .saturating_sub(density_stats_before.reuses),
        generated_density_chunks: density_stats_after
            .generations
            .saturating_sub(density_stats_before.generations),
        evicted_density_chunks: density_stats_after
            .evictions
            .saturating_sub(density_stats_before.evictions),
        prepared_reused_density_chunks: prepared.reused_density_chunks,
        prepared_generated_density_chunks: prepared.generated_density_chunks,
        prepared_evicted_density_chunks: prepared.evicted_density_chunks,
        raw_vertex_count: raw_mesh.vertices.len() / FLOATS_PER_VERTEX,
        raw_index_count: raw_mesh.indices.len(),
        vertex_count: mesh.vertices.len() / FLOATS_PER_VERTEX,
        index_count: mesh.indices.len(),
        vertex_bytes: mesh.vertices.len() * std::mem::size_of::<f32>(),
        index_bytes: mesh.indices.len() * std::mem::size_of::<u32>(),
        copy_checksum,
    }
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

fn empty_profile(key: TerrainNodeKey, base_cell_size: f64) -> TerrainNodeBuildProfile {
    TerrainNodeBuildProfile {
        key,
        cell_size: base_cell_size,
        class: TerrainNodeBuildClass::EmptyAir,
        total_ms: 0.0,
        density_ms: 0.0,
        contouring_ms: 0.0,
        material_ms: 0.0,
        copy_ms: 0.0,
        prepared_total_ms: 0.0,
        prepared_density_ms: 0.0,
        prepared_contouring_ms: 0.0,
        prepared_material_ms: 0.0,
        prepared_copy_ms: 0.0,
        reused_density_chunks: 0,
        generated_density_chunks: 0,
        evicted_density_chunks: 0,
        prepared_reused_density_chunks: 0,
        prepared_generated_density_chunks: 0,
        prepared_evicted_density_chunks: 0,
        raw_vertex_count: 0,
        raw_index_count: 0,
        vertex_count: 0,
        index_count: 0,
        vertex_bytes: 0,
        index_bytes: 0,
        copy_checksum: 0.0,
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct PreparedNodeBuildProfile {
    total_ms: f64,
    density_ms: f64,
    contouring_ms: f64,
    material_ms: f64,
    copy_ms: f64,
    reused_density_chunks: u64,
    generated_density_chunks: u64,
    evicted_density_chunks: u64,
}

fn profile_prepared_node_build(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    preset_id: u32,
    seed: u32,
    key: TerrainNodeKey,
    cell_size: f64,
) -> PreparedNodeBuildProfile {
    let total_started_at = Instant::now();
    let density_stats_before = density_store_stats();

    let density_started_at = Instant::now();
    let chunks =
        generate_neighbor_apron_chunks(noise, preset, preset_id, seed, key.coord, cell_size);
    let density_ms = elapsed_ms(density_started_at);
    let density_stats_after = density_store_stats();

    let contouring_started_at = Instant::now();
    let raw_mesh = build_neighbor_aware_chunk_mesh_raw(noise, preset, seed, &chunks, key.coord);
    let contouring_ms = elapsed_ms(contouring_started_at);

    let material_started_at = Instant::now();
    let mesh =
        expand_terrain_mesh_for_triangle_material_palettes(&raw_mesh.vertices, &raw_mesh.indices);
    let material_ms = elapsed_ms(material_started_at);

    let copy_started_at = Instant::now();
    let copied_vertices = mesh.vertices.clone();
    let copied_indices = mesh.indices.clone();
    let _copy_checksum = mesh_copy_checksum(&copied_vertices, &copied_indices);
    let copy_ms = elapsed_ms(copy_started_at);

    PreparedNodeBuildProfile {
        total_ms: elapsed_ms(total_started_at),
        density_ms,
        contouring_ms,
        material_ms,
        copy_ms,
        reused_density_chunks: density_stats_after
            .reuses
            .saturating_sub(density_stats_before.reuses),
        generated_density_chunks: density_stats_after
            .generations
            .saturating_sub(density_stats_before.generations),
        evicted_density_chunks: density_stats_after
            .evictions
            .saturating_sub(density_stats_before.evictions),
    }
}

fn classify_profiled_node(
    chunks: &[TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
    mesh: &MeshData,
) -> TerrainNodeBuildClass {
    if !mesh.indices.is_empty() {
        return match mesh.indices.len() {
            0..=1_499 => TerrainNodeBuildClass::SurfaceSparse,
            1_500..=5_999 => TerrainNodeBuildClass::SurfaceHeavy,
            _ => TerrainNodeBuildClass::SurfaceComplex,
        };
    }

    let Some(center_chunk) = neighbor_chunk(chunks, center_coord, center_coord) else {
        return TerrainNodeBuildClass::EmptyAir;
    };
    let mut has_negative = false;
    let mut has_positive = false;
    for density in &center_chunk.densities {
        has_negative |= *density <= 0.0;
        has_positive |= *density > 0.0;
        if has_negative && has_positive {
            return TerrainNodeBuildClass::SurfaceSparse;
        }
    }

    if has_negative {
        TerrainNodeBuildClass::Solid
    } else {
        TerrainNodeBuildClass::EmptyAir
    }
}

fn mesh_copy_checksum(vertices: &[f32], indices: &[u32]) -> f64 {
    vertices.len() as f64
        + indices.len() as f64
        + vertices.first().copied().unwrap_or(0.0) as f64
        + indices.first().copied().unwrap_or(0) as f64
}

fn elapsed_ms(started_at: Instant) -> f64 {
    started_at.elapsed().as_secs_f64() * 1000.0
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

    #[test]
    fn benchmark_helpers_cover_invalid_inputs_and_class_names() {
        let _lock = crate::test_lock();
        reset_density_store();
        let key = TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
        };
        let bounds = DensityWindowBounds {
            min_x: 0,
            min_y: 0,
            min_z: 0,
            max_x: 0,
            max_y: 0,
            max_z: 0,
        };

        assert_eq!(TerrainNodeBuildClass::EmptyAir.as_str(), "emptyAir");
        assert_eq!(TerrainNodeBuildClass::Solid.as_str(), "solid");
        assert_eq!(
            TerrainNodeBuildClass::SurfaceSparse.as_str(),
            "surfaceSparse"
        );
        assert_eq!(TerrainNodeBuildClass::SurfaceHeavy.as_str(), "surfaceHeavy");
        assert_eq!(
            TerrainNodeBuildClass::SurfaceComplex.as_str(),
            "surfaceComplex"
        );
        assert!(fill_density_chunk(0x0F6, 1, key.coord, 0.0).is_empty());
        assert_eq!(prepare_density_chunk_window(0x0F6, 1, bounds, 0.0), 0);

        let empty = profile_node_mesh_build(0x0F6, 1, key, 0.0);
        assert_eq!(empty.class, TerrainNodeBuildClass::EmptyAir);
        assert_eq!(empty.cell_size, 0.0);
        assert_eq!(empty.vertex_count, 0);
        assert_eq!(empty.index_count, 0);

        let empty_mesh = MeshData {
            vertices: Vec::new(),
            indices: Vec::new(),
        };
        assert_eq!(
            classify_profiled_node(&[], key.coord, &empty_mesh),
            TerrainNodeBuildClass::EmptyAir
        );
        let mixed_chunk = TerrainDensityChunk {
            coord: key.coord,
            cell_size: 1.0,
            densities: vec![-1.0, 1.0],
        };
        assert_eq!(
            classify_profiled_node(&[mixed_chunk], key.coord, &empty_mesh),
            TerrainNodeBuildClass::SurfaceSparse
        );
        let heavy_mesh = MeshData {
            vertices: Vec::new(),
            indices: vec![0; 2_000],
        };
        assert_eq!(
            classify_profiled_node(&[], key.coord, &heavy_mesh),
            TerrainNodeBuildClass::SurfaceHeavy
        );
        let complex_mesh = MeshData {
            vertices: Vec::new(),
            indices: vec![0; 6_000],
        };
        assert_eq!(
            classify_profiled_node(&[], key.coord, &complex_mesh),
            TerrainNodeBuildClass::SurfaceComplex
        );
    }

    #[test]
    fn benchmark_profile_records_phase_timings_and_buffers() {
        let _lock = crate::test_lock();
        reset_density_store();
        let profile = profile_node_mesh_build(
            0x0F6,
            1,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
            },
            1.0,
        );

        assert_eq!(profile.key.lod, 0);
        assert_eq!(profile.cell_size, 1.0);
        assert!(profile.total_ms >= profile.density_ms);
        assert!(profile.generated_density_chunks > 0);
        assert!(profile.prepared_total_ms > 0.0);
        assert_eq!(profile.prepared_generated_density_chunks, 0);
        assert!(profile.prepared_reused_density_chunks >= 8);
        assert_eq!(
            profile.vertex_bytes,
            profile.vertex_count * FLOATS_PER_VERTEX * 4
        );
        assert_eq!(profile.index_bytes, profile.index_count * 4);
        assert!(profile.copy_checksum >= 0.0);
        assert!(!profile.class.as_str().is_empty());
    }

    #[test]
    fn benchmark_profile_classifies_air_and_solid_nodes() {
        let _lock = crate::test_lock();
        reset_density_store();
        let air = profile_node_mesh_build(
            0x0F6,
            1,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 10, z: 0 },
            },
            1.0,
        );
        reset_density_store();
        let solid = profile_node_mesh_build(
            0x0F6,
            1,
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: -10, z: 0 },
            },
            1.0,
        );

        assert_eq!(air.class, TerrainNodeBuildClass::EmptyAir);
        assert_eq!(solid.class, TerrainNodeBuildClass::Solid);
    }
}
