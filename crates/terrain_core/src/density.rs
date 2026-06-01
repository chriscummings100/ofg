use crate::*;

pub(crate) fn generate_neighbor_apron_chunks(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    preset_id: u32,
    seed: u32,
    center_coord: TerrainChunkCoord,
    cell_size: f64,
) -> Vec<TerrainDensityChunk> {
    let mut chunks = Vec::with_capacity(8);

    for dz in 0..=1 {
        for dy in 0..=1 {
            for dx in 0..=1 {
                chunks.push(ensure_density_chunk_stored(
                    noise,
                    preset,
                    preset_id,
                    seed,
                    TerrainChunkCoord {
                        x: center_coord.x + dx,
                        y: center_coord.y + dy,
                        z: center_coord.z + dz,
                    },
                    cell_size,
                ));
            }
        }
    }

    chunks
}

pub(crate) fn generate_density_chunk(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> TerrainDensityChunk {
    let mut densities = vec![0.0; TERRAIN_CHUNK_SAMPLE_COUNT];
    let origin = terrain_chunk_origin(coord, cell_size);

    for z in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
        for x in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
            let column_x = origin.x + x as f64 * cell_size;
            let column_z = origin.z + z as f64 * cell_size;
            let macro_sample = sample_macro_terrain(
                noise,
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
                densities[terrain_chunk_sample_index(x, y, z)] =
                    density_at_position_with_macro(noise, preset, position, macro_sample).density
                        as f32;
            }
        }
    }

    TerrainDensityChunk {
        coord,
        cell_size,
        densities,
    }
}

pub(crate) fn ensure_density_chunk_stored(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    preset_id: u32,
    seed: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> TerrainDensityChunk {
    let key = density_chunk_store_key(seed, preset_id, coord, cell_size);
    if let Some(densities) = density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .get(key)
    {
        return TerrainDensityChunk {
            coord,
            cell_size,
            densities,
        };
    }

    let chunk = generate_density_chunk(noise, preset, seed, coord, cell_size);
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .insert(key, chunk.densities.clone());

    chunk
}
