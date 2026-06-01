use crate::*;

pub(crate) static mut DENSITY_CHUNK_BUFFER: [f32; TERRAIN_CHUNK_SAMPLE_COUNT] =
    [0.0; TERRAIN_CHUNK_SAMPLE_COUNT];
pub(crate) static mut MESH_VERTEX_BUFFER: Vec<f32> = Vec::new();
pub(crate) static mut MESH_INDEX_BUFFER: Vec<u32> = Vec::new();

#[no_mangle]
pub extern "C" fn ofg_terrain_core_version() -> u32 {
    TERRAIN_CORE_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_terrain_core_preset_count() -> u32 {
    TERRAIN_PRESETS.len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_max_entries() -> u32 {
    DENSITY_CHUNK_STORE_MAX_ENTRIES as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_entry_count() -> u32 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .entries
        .len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_reuse_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .reuses as f64
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_generation_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .generations as f64
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_eviction_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .evictions as f64
}

#[no_mangle]
pub extern "C" fn ofg_reset_density_chunk_store() {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .reset();
}

#[no_mangle]
pub extern "C" fn ofg_store_density_chunk_buffer(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    if cell_size <= 0.0 {
        return 0;
    }

    let coord = TerrainChunkCoord {
        x: chunk_x,
        y: chunk_y,
        z: chunk_z,
    };
    let preset_id = terrain_preset_index(preset);
    let key = density_chunk_store_key(seed, preset_id, coord, cell_size);
    let densities = unsafe {
        core::slice::from_raw_parts(
            core::ptr::addr_of!(DENSITY_CHUNK_BUFFER).cast::<f32>(),
            TERRAIN_CHUNK_SAMPLE_COUNT,
        )
    }
    .to_vec();

    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .insert(key, densities);

    1
}

#[no_mangle]
pub extern "C" fn ofg_prepare_density_chunk_window(
    seed: u32,
    preset: u32,
    min_chunk_x: i32,
    min_chunk_y: i32,
    min_chunk_z: i32,
    max_chunk_x: i32,
    max_chunk_y: i32,
    max_chunk_z: i32,
    cell_size: f64,
) -> u32 {
    if cell_size <= 0.0 {
        return 0;
    }

    let min_x = min_chunk_x.min(max_chunk_x);
    let max_x = min_chunk_x.max(max_chunk_x);
    let min_y = min_chunk_y.min(max_chunk_y);
    let max_y = min_chunk_y.max(max_chunk_y);
    let min_z = min_chunk_z.min(max_chunk_z);
    let max_z = min_chunk_z.max(max_chunk_z);
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

#[no_mangle]
pub extern "C" fn ofg_density_chunk_sample_count() -> u32 {
    TERRAIN_CHUNK_SAMPLE_COUNT as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_buffer_ptr() -> *const f32 {
    unsafe { core::ptr::addr_of!(DENSITY_CHUNK_BUFFER).cast::<f32>() }
}

#[no_mangle]
pub extern "C" fn ofg_fill_density_chunk(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) {
    if cell_size <= 0.0 {
        return;
    }

    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * cell_size;
    let origin = Vec3 {
        x: chunk_x as f64 * chunk_size,
        y: chunk_y as f64 * chunk_size,
        z: chunk_z as f64 * chunk_size,
    };
    let buffer = unsafe { core::ptr::addr_of_mut!(DENSITY_CHUNK_BUFFER).cast::<f32>() };

    for z in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
        for x in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
            let column_x = origin.x + x as f64 * cell_size;
            let column_z = origin.z + z as f64 * cell_size;
            let macro_sample = sample_macro_terrain(
                &noise,
                preset,
                seed,
                Vec3 {
                    x: column_x,
                    y: 0.0,
                    z: column_z,
                },
            );

            for y in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
                let position = Vec3 {
                    x: column_x,
                    y: origin.y + y as f64 * cell_size,
                    z: column_z,
                };
                let density = density_at_position_with_macro(&noise, preset, position, macro_sample)
                    .density as f32;
                let index = terrain_chunk_sample_index(x, y, z);

                unsafe {
                    *buffer.add(index) = density;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn ofg_build_chunk_mesh(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    unsafe {
        MESH_VERTEX_BUFFER.clear();
        MESH_INDEX_BUFFER.clear();
    }

    if cell_size <= 0.0 {
        return 0;
    }

    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);
    let center_coord = TerrainChunkCoord {
        x: chunk_x,
        y: chunk_y,
        z: chunk_z,
    };
    let chunks =
        generate_neighbor_apron_chunks(&noise, preset, preset_id, seed, center_coord, cell_size);
    let mesh = build_neighbor_aware_chunk_mesh(&noise, preset, seed, &chunks, center_coord);

    unsafe {
        MESH_VERTEX_BUFFER = mesh.vertices;
        MESH_INDEX_BUFFER = mesh.indices;
        MESH_INDEX_BUFFER.len() as u32
    }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_ptr() -> *const f32 {
    unsafe { MESH_VERTEX_BUFFER.as_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_len() -> u32 {
    unsafe { MESH_VERTEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_ptr() -> *const u32 {
    unsafe { MESH_INDEX_BUFFER.as_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_len() -> u32 {
    unsafe { MESH_INDEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_macro_base_elevation_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    sample_macro_terrain(&noise, preset, seed, Vec3 { x, y: 0.0, z }).base_elevation
}

#[no_mangle]
pub extern "C" fn ofg_density_at(seed: u32, preset: u32, x: f64, y: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    density_at_position(&noise, preset, seed, Vec3 { x, y, z }).density
}

#[no_mangle]
pub extern "C" fn ofg_height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    height_at(seed, preset, x, z)
}
