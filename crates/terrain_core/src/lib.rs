const TERRAIN_CORE_VERSION: u32 = 1;
const DEFAULT_TERRAIN_PRESET: u32 = 1;
const SURFACE_SEARCH_MIN_Y: f64 = -96.0;
const SURFACE_SEARCH_MAX_Y: f64 = 96.0;
const SURFACE_SEARCH_STEP: f64 = 1.0;
const SURFACE_REFINE_STEPS: usize = 12;
const TERRAIN_CHUNK_CELLS_PER_AXIS: usize = 32;
const TERRAIN_CHUNK_SAMPLES_PER_AXIS: usize = TERRAIN_CHUNK_CELLS_PER_AXIS + 1;
const TERRAIN_CHUNK_SAMPLE_COUNT: usize = TERRAIN_CHUNK_SAMPLES_PER_AXIS
    * TERRAIN_CHUNK_SAMPLES_PER_AXIS
    * TERRAIN_CHUNK_SAMPLES_PER_AXIS;
const TERRAIN_CHUNK_APRON_CELLS_PER_AXIS: usize = TERRAIN_CHUNK_CELLS_PER_AXIS + 1;
const TERRAIN_CHUNK_APRON_CELL_COUNT: usize = TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
    * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
    * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS;
const FLOATS_PER_VERTEX: usize = 19;
const MATERIAL_INDICES_VERTEX_OFFSET: usize = 11;
const MATERIAL_WEIGHTS_VERTEX_OFFSET: usize = 15;
const F3: f64 = 1.0 / 3.0;
const G3: f64 = 1.0 / 6.0;
const NOISE_SCALE: f64 = 32.0;
const UINT32_SCALE: f64 = 1.0 / 4294967296.0;

#[derive(Clone, Copy, Debug)]
struct Vec3 {
    x: f64,
    y: f64,
    z: f64,
}

#[derive(Clone, Copy)]
struct NoiseSample {
    value: f64,
    gradient: Vec3,
}

#[derive(Clone, Copy)]
struct FractalNoiseOptions {
    octaves: u32,
    frequency: f64,
    lacunarity: f64,
    persistence: f64,
}

#[derive(Clone, Copy)]
struct RidgedFractalNoiseOptions {
    octaves: u32,
    frequency: f64,
    lacunarity: f64,
    persistence: f64,
    ridge_offset: f64,
    ridge_sharpness: f64,
}

#[derive(Clone, Copy)]
struct DomainWarpOptions {
    octaves: u32,
    frequency: f64,
    lacunarity: f64,
    persistence: f64,
    amplitude: f64,
}

#[derive(Clone, Copy)]
struct CellularNoiseOptions {
    frequency: f64,
}

#[derive(Clone, Copy)]
struct CellularNoiseSample {
    edge_distance: f64,
}

#[derive(Clone, Copy)]
struct TerrainPresetDefinition {
    base_height: f64,
    height_scale: f64,
    large_feature_noise: FractalNoiseOptions,
    ridge_height_scale: f64,
    ridge_noise: RidgedFractalNoiseOptions,
    warp: DomainWarpOptions,
    cellular: CellularNoiseOptions,
    cellular_height_scale: f64,
    detail_noise: FractalNoiseOptions,
    detail_amplitude: f64,
}

#[derive(Clone, Copy)]
#[allow(dead_code)]
struct MacroTerrainSample {
    base_elevation: f64,
    large_feature: f64,
    mountainness: f64,
    continentality: f64,
    erosion_susceptibility: f64,
    ridge: f64,
    warp: Vec3,
    gradient_x: f64,
    gradient_z: f64,
    cellular_edge: f64,
}

#[derive(Clone, Copy)]
#[allow(dead_code)]
struct DensitySample {
    density: f64,
    gradient: Vec3,
}

#[derive(Clone, Copy)]
struct TerrainChunkCoord {
    x: i32,
    y: i32,
    z: i32,
}

#[derive(Clone, Copy)]
struct TerrainCellCoord {
    x: usize,
    y: usize,
    z: usize,
}

#[derive(Clone, Copy)]
struct TerrainSampleCoord {
    x: usize,
    y: usize,
    z: usize,
}

struct TerrainDensityChunk {
    coord: TerrainChunkCoord,
    cell_size: f64,
    densities: Vec<f32>,
}

#[derive(Clone, Copy)]
struct TerrainChunkBounds {
    min: Vec3,
    max: Vec3,
}

#[derive(Clone, Copy)]
struct HermiteIntersection {
    position: Vec3,
    normal: Vec3,
}

#[derive(Clone, Copy)]
struct BiomeWeights {
    grassland: f64,
    temperate_forest: f64,
    wetland: f64,
    coast_beach: f64,
    dry_badland: f64,
    alpine_meadow: f64,
    high_mountain_rock: f64,
    snow_tundra: f64,
}

#[derive(Clone, Copy)]
struct PackedTerrainMaterial {
    indices: [f32; 4],
    weights: [f32; 4],
}

struct MeshData {
    vertices: Vec<f32>,
    indices: Vec<u32>,
}

#[derive(Clone, Copy)]
struct DomainWarpSample {
    position: Vec3,
    offset: Vec3,
}

#[derive(Clone, Copy)]
struct SimplexCornerOffset {
    x: i32,
    y: i32,
    z: i32,
    unskew: f64,
}

struct SimplexNoise3D {
    perm: [u8; 512],
}

const GRADIENTS: [Vec3; 12] = [
    Vec3 {
        x: 1.0,
        y: 1.0,
        z: 0.0,
    },
    Vec3 {
        x: -1.0,
        y: 1.0,
        z: 0.0,
    },
    Vec3 {
        x: 1.0,
        y: -1.0,
        z: 0.0,
    },
    Vec3 {
        x: -1.0,
        y: -1.0,
        z: 0.0,
    },
    Vec3 {
        x: 1.0,
        y: 0.0,
        z: 1.0,
    },
    Vec3 {
        x: -1.0,
        y: 0.0,
        z: 1.0,
    },
    Vec3 {
        x: 1.0,
        y: 0.0,
        z: -1.0,
    },
    Vec3 {
        x: -1.0,
        y: 0.0,
        z: -1.0,
    },
    Vec3 {
        x: 0.0,
        y: 1.0,
        z: 1.0,
    },
    Vec3 {
        x: 0.0,
        y: -1.0,
        z: 1.0,
    },
    Vec3 {
        x: 0.0,
        y: 1.0,
        z: -1.0,
    },
    Vec3 {
        x: 0.0,
        y: -1.0,
        z: -1.0,
    },
];
static mut DENSITY_CHUNK_BUFFER: [f32; TERRAIN_CHUNK_SAMPLE_COUNT] =
    [0.0; TERRAIN_CHUNK_SAMPLE_COUNT];
static mut MESH_VERTEX_BUFFER: Vec<f32> = Vec::new();
static mut MESH_INDEX_BUFFER: Vec<u32> = Vec::new();

const CELL_CORNERS: [TerrainSampleCoord; 8] = [
    TerrainSampleCoord { x: 0, y: 0, z: 0 },
    TerrainSampleCoord { x: 1, y: 0, z: 0 },
    TerrainSampleCoord { x: 0, y: 1, z: 0 },
    TerrainSampleCoord { x: 1, y: 1, z: 0 },
    TerrainSampleCoord { x: 0, y: 0, z: 1 },
    TerrainSampleCoord { x: 1, y: 0, z: 1 },
    TerrainSampleCoord { x: 0, y: 1, z: 1 },
    TerrainSampleCoord { x: 1, y: 1, z: 1 },
];
const CELL_EDGES: [(usize, usize); 12] = [
    (0, 1),
    (2, 3),
    (4, 5),
    (6, 7),
    (0, 2),
    (1, 3),
    (4, 6),
    (5, 7),
    (0, 4),
    (1, 5),
    (2, 6),
    (3, 7),
];

const SEED_LARGE_FEATURE_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.0065,
    lacunarity: 2.0,
    persistence: 0.52,
};
const SEED_DENSITY_DETAIL_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.035,
    lacunarity: 2.15,
    persistence: 0.46,
};
const TERRAIN_PRESETS: [TerrainPresetDefinition; 4] = [
    TerrainPresetDefinition {
        base_height: 2.0,
        height_scale: 22.0,
        large_feature_noise: SEED_LARGE_FEATURE_NOISE,
        ridge_height_scale: 0.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 1,
            frequency: 0.008,
            lacunarity: 2.0,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 1.0,
        },
        warp: DomainWarpOptions {
            octaves: 1,
            frequency: 0.005,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 0.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.015 },
        cellular_height_scale: 0.0,
        detail_noise: SEED_DENSITY_DETAIL_NOISE,
        detail_amplitude: 5.0,
    },
    TerrainPresetDefinition {
        base_height: 3.0,
        height_scale: 16.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.004,
            lacunarity: 2.0,
            persistence: 0.5,
        },
        ridge_height_scale: 3.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 3,
            frequency: 0.009,
            lacunarity: 2.1,
            persistence: 0.48,
            ridge_offset: 1.0,
            ridge_sharpness: 1.8,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.004,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 14.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.018 },
        cellular_height_scale: 1.3,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.03,
            lacunarity: 2.05,
            persistence: 0.44,
        },
        detail_amplitude: 3.2,
    },
    TerrainPresetDefinition {
        base_height: 2.0,
        height_scale: 20.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.0028,
            lacunarity: 2.0,
            persistence: 0.53,
        },
        ridge_height_scale: 24.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.0065,
            lacunarity: 2.05,
            persistence: 0.52,
            ridge_offset: 1.0,
            ridge_sharpness: 2.25,
        },
        warp: DomainWarpOptions {
            octaves: 3,
            frequency: 0.0032,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 28.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.012 },
        cellular_height_scale: 2.0,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.026,
            lacunarity: 2.1,
            persistence: 0.45,
        },
        detail_amplitude: 4.5,
    },
    TerrainPresetDefinition {
        base_height: 7.0,
        height_scale: 18.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.0036,
            lacunarity: 2.2,
            persistence: 0.5,
        },
        ridge_height_scale: 11.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.011,
            lacunarity: 2.2,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 1.45,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.0055,
            lacunarity: 2.1,
            persistence: 0.52,
            amplitude: 18.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.02 },
        cellular_height_scale: 6.0,
        detail_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.038,
            lacunarity: 2.2,
            persistence: 0.48,
        },
        detail_amplitude: 6.5,
    },
];

#[no_mangle]
pub extern "C" fn ofg_terrain_core_version() -> u32 {
    TERRAIN_CORE_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_terrain_core_preset_count() -> u32 {
    TERRAIN_PRESETS.len() as u32
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
    let preset = terrain_preset(preset);
    let center_coord = TerrainChunkCoord {
        x: chunk_x,
        y: chunk_y,
        z: chunk_z,
    };
    let chunks = generate_neighbor_apron_chunks(&noise, preset, seed, center_coord, cell_size);
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

fn height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    let mut upper_y = SURFACE_SEARCH_MAX_Y;
    let mut upper_density =
        density_at_position(&noise, preset, seed, Vec3 { x, y: upper_y, z }).density;
    let mut lower_y = upper_y - SURFACE_SEARCH_STEP;

    while lower_y >= SURFACE_SEARCH_MIN_Y {
        let lower_density =
            density_at_position(&noise, preset, seed, Vec3 { x, y: lower_y, z }).density;
        if lower_density <= 0.0 && upper_density > 0.0 {
            return refine_surface_height(&noise, preset, seed, x, z, lower_y, upper_y);
        }

        upper_y = lower_y;
        upper_density = lower_density;
        lower_y -= SURFACE_SEARCH_STEP;
    }

    sample_macro_terrain(&noise, preset, seed, Vec3 { x, y: 0.0, z }).base_elevation
}

fn refine_surface_height(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    x: f64,
    z: f64,
    solid_y: f64,
    air_y: f64,
) -> f64 {
    let mut lower_y = solid_y;
    let mut upper_y = air_y;

    for _ in 0..SURFACE_REFINE_STEPS {
        let mid_y = (lower_y + upper_y) * 0.5;
        if density_at_position(noise, preset, seed, Vec3 { x, y: mid_y, z }).density <= 0.0 {
            lower_y = mid_y;
        } else {
            upper_y = mid_y;
        }
    }

    (lower_y + upper_y) * 0.5
}

fn density_at_position(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    position: Vec3,
) -> DensitySample {
    let macro_sample = sample_macro_terrain(noise, preset, seed, position);

    density_at_position_with_macro(noise, preset, position, macro_sample)
}

fn density_at_position_with_macro(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    position: Vec3,
    macro_sample: MacroTerrainSample,
) -> DensitySample {
    let detail = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x + 83.5 + macro_sample.warp.x * 0.15,
            y: position.y - 41.75,
            z: position.z - 19.25 + macro_sample.warp.z * 0.15,
        },
        preset.detail_noise,
    );

    DensitySample {
        density: position.y - macro_sample.base_elevation - detail.value * preset.detail_amplitude,
        gradient: Vec3 {
            x: -macro_sample.gradient_x - detail.gradient.x * preset.detail_amplitude,
            y: 1.0 - detail.gradient.y * preset.detail_amplitude,
            z: -macro_sample.gradient_z - detail.gradient.z * preset.detail_amplitude,
        },
    }
}

fn generate_neighbor_apron_chunks(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    center_coord: TerrainChunkCoord,
    cell_size: f64,
) -> Vec<TerrainDensityChunk> {
    let mut chunks = Vec::with_capacity(8);

    for dz in 0..=1 {
        for dy in 0..=1 {
            for dx in 0..=1 {
                chunks.push(generate_density_chunk(
                    noise,
                    preset,
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

fn generate_density_chunk(
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

fn build_neighbor_aware_chunk_mesh(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    chunks: &[TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
) -> MeshData {
    let center_chunk = match neighbor_chunk(chunks, center_coord, center_coord) {
        Some(chunk) => chunk,
        None => {
            return MeshData {
                vertices: Vec::new(),
                indices: Vec::new(),
            };
        }
    };
    let mut vertex_indices = vec![-1_i32; TERRAIN_CHUNK_APRON_CELL_COUNT];
    let mut vertices = Vec::new();
    let mesh_bounds = center_chunk.bounds();

    for z in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
                let (chunk_coord, cell) = local_apron_cell_ref(center_coord, x, y, z);
                let chunk = match neighbor_chunk(chunks, center_coord, chunk_coord) {
                    Some(chunk) => chunk,
                    None => continue,
                };
                let intersections = extract_hermite_intersections(noise, preset, seed, chunk, cell);
                if intersections.is_empty() {
                    continue;
                }

                let position = centroid_of_intersections(&intersections, chunk.cell_bounds(cell));
                let normal = average_normal(&intersections);
                let vertex_index = vertices.len() / FLOATS_PER_VERTEX;
                vertex_indices[apron_cell_index(x, y, z)] = vertex_index as i32;
                write_dual_contouring_vertex(
                    &mut vertices,
                    mesh_bounds,
                    position,
                    normal,
                    noise,
                    preset,
                    seed,
                );
            }
        }
    }

    let mut indices = Vec::new();
    emit_owned_x_edge_quads(center_chunk, &vertex_indices, &mut indices);
    emit_owned_y_edge_quads(center_chunk, &vertex_indices, &mut indices);
    emit_owned_z_edge_quads(center_chunk, &vertex_indices, &mut indices);

    expand_terrain_mesh_for_triangle_material_palettes(&vertices, &indices)
}

fn extract_hermite_intersections(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    chunk: &TerrainDensityChunk,
    cell: TerrainCellCoord,
) -> Vec<HermiteIntersection> {
    let mut corner_densities = [0.0_f32; 8];
    for (index, corner) in CELL_CORNERS.iter().enumerate() {
        corner_densities[index] = chunk.density_at_sample(sample_for_cell_corner(cell, *corner));
    }

    let mut intersections = Vec::new();
    for (start_corner_index, end_corner_index) in CELL_EDGES {
        let start_density = corner_densities[start_corner_index];
        let end_density = corner_densities[end_corner_index];
        if !has_sign_change(start_density, end_density) {
            continue;
        }

        let start_sample = sample_for_cell_corner(cell, CELL_CORNERS[start_corner_index]);
        let end_sample = sample_for_cell_corner(cell, CELL_CORNERS[end_corner_index]);
        let start_position = chunk.sample_position(start_sample);
        let end_position = chunk.sample_position(end_sample);
        let t = clamp(
            start_density as f64 / (start_density as f64 - end_density as f64),
            0.0,
            1.0,
        );
        let position = lerp_vec3(start_position, end_position, t);
        let normal = normalize_vec3(density_at_position(noise, preset, seed, position).gradient);

        intersections.push(HermiteIntersection { position, normal });
    }

    intersections
}

fn emit_owned_x_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x,
                    y: y + 1,
                    z: z + 1,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32,
                            z as i32 + 1,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32 + 1,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

fn emit_owned_y_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y,
                    z: z + 1,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32,
                            z as i32 + 1,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32 + 1,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

fn emit_owned_z_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32 + 1,
                            z as i32,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

fn emit_quad(indices: &mut Vec<u32>, vertices: [i32; 4], forward: bool) {
    if vertices.iter().any(|vertex| *vertex < 0) {
        return;
    }

    let [a, b, c, d] = vertices.map(|vertex| vertex as u32);
    if forward {
        indices.extend_from_slice(&[a, b, c, c, b, d]);
    } else {
        indices.extend_from_slice(&[a, c, b, c, d, b]);
    }
}

fn write_dual_contouring_vertex(
    vertices: &mut Vec<f32>,
    chunk_bounds: TerrainChunkBounds,
    position: Vec3,
    normal: Vec3,
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
) {
    let color = color_for_height(position.y);
    let width = chunk_bounds.max.x - chunk_bounds.min.x;
    let depth = chunk_bounds.max.z - chunk_bounds.min.z;
    let material = material_pack_at(noise, preset, seed, position);

    vertices.extend_from_slice(&[
        position.x as f32,
        position.y as f32,
        position.z as f32,
        color[0],
        color[1],
        color[2],
        normal.x as f32,
        normal.y as f32,
        normal.z as f32,
        if width == 0.0 {
            0.0
        } else {
            ((position.x - chunk_bounds.min.x) / width) as f32
        },
        if depth == 0.0 {
            0.0
        } else {
            ((position.z - chunk_bounds.min.z) / depth) as f32
        },
        material.indices[0],
        material.indices[1],
        material.indices[2],
        material.indices[3],
        material.weights[0],
        material.weights[1],
        material.weights[2],
        material.weights[3],
    ]);
}

fn material_pack_at(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    position: Vec3,
) -> PackedTerrainMaterial {
    let macro_sample = sample_macro_terrain(noise, preset, seed, position);
    let density_sample = density_at_position_with_macro(noise, preset, position, macro_sample);
    let biome = biome_weights_at(noise, preset, seed, position, macro_sample);
    let normal = normalize_vec3(density_sample.gradient);
    let slope = clamp(1.0 - normal.y, 0.0, 1.0);
    let lowland = clamp((4.0 - position.y) / 8.0, 0.0, 1.0);
    let highland = clamp((position.y - 28.0) / 28.0, 0.0, 1.0);
    let cliff = smoothstep(0.62, 0.86, slope);
    let rocky = smoothstep(0.34, 0.68, slope) * (1.0 - cliff);
    let snow = smoothstep(38.0, 56.0, position.y) * smoothstep(0.1, 0.65, normal.y);
    let wet = lowland * smoothstep(0.12, 0.72, normal.y) * (1.0 - rocky) * (1.0 - cliff);
    let sand = clamp((2.5 - position.y.abs()) / 5.0, 0.0, 1.0)
        * smoothstep(0.18, 0.82, normal.y)
        * (0.45 + macro_sample.continentality * 0.25);
    let dry = clamp(
        0.35 + macro_sample.continentality * 0.45 - macro_sample.mountainness * 0.25,
        0.0,
        1.0,
    );
    let moss = clamp(
        (macro_sample.mountainness + macro_sample.ridge) * 0.35,
        0.0,
        0.8,
    ) * (1.0 - cliff)
        * (1.0 - snow);
    let red_soil = clamp((macro_sample.cellular_edge - 0.42) / 0.45, 0.0, 0.75)
        * dry
        * (1.0 - rocky)
        * (1.0 - snow);
    let meadow = (1.0 - dry * 0.55) * smoothstep(0.2, 0.85, normal.y) * (1.0 - wet) * (1.0 - snow);
    let dry_ground = dry * smoothstep(0.28, 0.88, normal.y) * (1.0 - wet) * (1.0 - snow);
    let scree = rocky * highland * 0.65;

    pack_material_weights(&[
        (
            0,
            meadow * (0.72 + biome.grassland * 0.42 + biome.alpine_meadow * 0.18),
        ),
        (1, dry_ground * (0.72 + biome.dry_badland * 0.65)),
        (
            2,
            (1.0 - dry) * 0.2 * (1.0 - rocky) * (1.0 - wet) + biome.temperate_forest * 0.45,
        ),
        (
            4,
            lowland * 0.28 * (1.0 - wet) * (1.0 - sand) + biome.wetland * 0.1,
        ),
        (6, wet + biome.wetland * 0.65),
        (7, sand + biome.coast_beach * 0.55),
        (8, sand * rocky * 0.8 + biome.coast_beach * rocky * 0.22),
        (10, scree + biome.high_mountain_rock * rocky * 0.28),
        (
            11,
            rocky * (1.0 - highland * 0.35) + biome.high_mountain_rock * 0.3,
        ),
        (12, cliff + biome.high_mountain_rock * cliff * 0.35),
        (
            13,
            moss + biome.temperate_forest * 0.16 + biome.alpine_meadow * 0.14,
        ),
        (14, red_soil + biome.dry_badland * 0.4),
        (15, snow + biome.snow_tundra * 0.85),
    ])
}

fn biome_weights_at(
    noise: &SimplexNoise3D,
    _preset: TerrainPresetDefinition,
    _seed: u32,
    position: Vec3,
    macro_sample: MacroTerrainSample,
) -> BiomeWeights {
    let climate_noise = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x + 971.2,
            y: 43.5,
            z: position.z - 211.7,
        },
        FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0025,
            lacunarity: 2.0,
            persistence: 0.52,
        },
    );
    let moisture_noise = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x - 317.6,
            y: -29.25,
            z: position.z + 513.4,
        },
        FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0032,
            lacunarity: 2.0,
            persistence: 0.5,
        },
    );
    let altitude = position.y;
    let high = smoothstep(14.0, 34.0, altitude);
    let very_high = smoothstep(30.0, 52.0, altitude);
    let near_sea_level = clamp(1.0 - altitude.abs() / 8.0, 0.0, 1.0);
    let temperature = clamp(
        0.72 - high * 0.34 - very_high * 0.22 - macro_sample.continentality * 0.05
            + climate_noise.value * 0.12,
        0.0,
        1.0,
    );
    let moisture = clamp(
        0.42 + (1.0 - macro_sample.continentality) * 0.22
            + macro_sample.erosion_susceptibility * 0.12
            - high * 0.09
            + moisture_noise.value * 0.18,
        0.0,
        1.0,
    );
    let wetness = smoothstep(0.5, 0.78, moisture) * (1.0 - high * 0.75);
    let dryness = smoothstep(0.48, 0.76, macro_sample.continentality)
        * (1.0 - smoothstep(0.42, 0.68, moisture))
        * (1.0 - high * 0.35);
    let coast = near_sea_level * smoothstep(0.4, 0.82, moisture) * (1.0 - high);
    let mountain_rock =
        smoothstep(0.46, 0.76, macro_sample.mountainness) * smoothstep(10.0, 26.0, altitude);
    let snow = smoothstep(34.0, 54.0, altitude) * (1.0 - smoothstep(0.28, 0.58, temperature));
    let alpine = smoothstep(16.0, 34.0, altitude) * (1.0 - snow) * (1.0 - mountain_rock * 0.5);
    let forest = smoothstep(0.52, 0.78, moisture)
        * smoothstep(0.34, 0.72, temperature)
        * (1.0 - high * 0.7)
        * (1.0 - coast * 0.5)
        * (1.0 - dryness * 0.55);
    let grassland = (1.0 - high * 0.55)
        * (1.0 - wetness * 0.6)
        * (1.0 - dryness * 0.45)
        * (1.0 - forest * 0.45);

    normalize_biome_weights([
        grassland,
        forest,
        wetness * (1.0 - coast * 0.35),
        coast,
        dryness,
        alpine,
        mountain_rock * (1.0 - snow * 0.5),
        snow,
    ])
}

fn normalize_biome_weights(weights: [f64; 8]) -> BiomeWeights {
    let total: f64 = weights.iter().copied().filter(|weight| *weight > 0.0).sum();
    if total <= f64::EPSILON {
        return BiomeWeights {
            grassland: 1.0,
            temperate_forest: 0.0,
            wetland: 0.0,
            coast_beach: 0.0,
            dry_badland: 0.0,
            alpine_meadow: 0.0,
            high_mountain_rock: 0.0,
            snow_tundra: 0.0,
        };
    }

    BiomeWeights {
        grassland: positive_weight(weights[0]) / total,
        temperate_forest: positive_weight(weights[1]) / total,
        wetland: positive_weight(weights[2]) / total,
        coast_beach: positive_weight(weights[3]) / total,
        dry_badland: positive_weight(weights[4]) / total,
        alpine_meadow: positive_weight(weights[5]) / total,
        high_mountain_rock: positive_weight(weights[6]) / total,
        snow_tundra: positive_weight(weights[7]) / total,
    }
}

fn pack_material_weights(candidates: &[(usize, f64)]) -> PackedTerrainMaterial {
    let mut positive: Vec<(usize, f64, usize)> = candidates
        .iter()
        .enumerate()
        .filter_map(|(order, (layer, weight))| {
            if *weight > 0.0 {
                Some((*layer, *weight, order))
            } else {
                None
            }
        })
        .collect();

    if positive.is_empty() {
        return default_material_pack();
    }

    positive.sort_by(|a, b| {
        b.1.partial_cmp(&a.1)
            .unwrap_or(core::cmp::Ordering::Equal)
            .then_with(|| a.2.cmp(&b.2))
    });
    positive.truncate(4);
    let total: f64 = positive.iter().map(|(_, weight, _)| *weight).sum();
    if total <= f64::EPSILON {
        return default_material_pack();
    }

    let mut indices = [0.0_f32; 4];
    let mut weights = [0.0_f32; 4];
    for (slot, (layer, weight, _)) in positive.iter().enumerate() {
        indices[slot] = *layer as f32;
        weights[slot] = (*weight / total) as f32;
    }

    if weights[0] == 0.0 {
        return default_material_pack();
    }

    PackedTerrainMaterial { indices, weights }
}

fn expand_terrain_mesh_for_triangle_material_palettes(
    source_vertices: &[f32],
    source_indices: &[u32],
) -> MeshData {
    if source_indices.is_empty() {
        return MeshData {
            vertices: Vec::new(),
            indices: Vec::new(),
        };
    }

    let mut vertices = vec![0.0_f32; source_indices.len() * FLOATS_PER_VERTEX];
    let mut indices = Vec::with_capacity(source_indices.len());

    for triangle_offset in (0..source_indices.len()).step_by(3) {
        let source_vertex_indices = [
            source_indices[triangle_offset] as usize,
            source_indices[triangle_offset + 1] as usize,
            source_indices[triangle_offset + 2] as usize,
        ];
        let palette = triangle_material_palette(source_vertices, source_vertex_indices);

        for corner in 0..3 {
            let source_vertex_offset = source_vertex_indices[corner] * FLOATS_PER_VERTEX;
            let expanded_vertex_index = triangle_offset + corner;
            let expanded_vertex_offset = expanded_vertex_index * FLOATS_PER_VERTEX;

            vertices[expanded_vertex_offset..expanded_vertex_offset + FLOATS_PER_VERTEX]
                .copy_from_slice(
                    &source_vertices
                        [source_vertex_offset..source_vertex_offset + FLOATS_PER_VERTEX],
                );
            let weights =
                vertex_weights_for_palette(source_vertices, source_vertex_offset, palette);
            write_packed_material_to_vertex(
                &mut vertices,
                expanded_vertex_offset,
                PackedTerrainMaterial {
                    indices: [
                        palette[0] as f32,
                        palette[1] as f32,
                        palette[2] as f32,
                        palette[3] as f32,
                    ],
                    weights,
                },
            );
            indices.push(expanded_vertex_index as u32);
        }
    }

    MeshData { vertices, indices }
}

fn triangle_material_palette(vertices: &[f32], source_vertex_indices: [usize; 3]) -> [usize; 4] {
    let mut weight_by_layer = [0.0_f32; 16];

    for source_vertex_index in source_vertex_indices {
        let source_vertex_offset = source_vertex_index * FLOATS_PER_VERTEX;
        for slot in 0..4 {
            let layer = vertices[source_vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot]
                .round() as usize;
            let weight = vertices[source_vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
            if layer < weight_by_layer.len() && weight > 0.0 {
                weight_by_layer[layer] += weight;
            }
        }
    }

    let mut ranked: Vec<usize> = (0..weight_by_layer.len())
        .filter(|layer| weight_by_layer[*layer] > 0.0)
        .collect();
    ranked.sort_by(|a, b| {
        weight_by_layer[*b]
            .partial_cmp(&weight_by_layer[*a])
            .unwrap_or(core::cmp::Ordering::Equal)
            .then_with(|| a.cmp(b))
    });

    let mut palette = [0_usize; 4];
    for (index, layer) in ranked.into_iter().take(4).enumerate() {
        palette[index] = layer;
    }

    palette
}

fn vertex_weights_for_palette(
    vertices: &[f32],
    source_vertex_offset: usize,
    palette: [usize; 4],
) -> [f32; 4] {
    let mut weights = [0.0_f32; 4];

    for slot in 0..4 {
        let source_layer =
            vertices[source_vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot].round() as usize;
        let source_weight = vertices[source_vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
        if let Some(palette_slot) = palette.iter().position(|layer| *layer == source_layer) {
            weights[palette_slot] += source_weight;
        }
    }

    let total: f32 = weights.iter().sum();
    if total <= f32::EPSILON {
        weights[0] = 1.0;
        return weights;
    }

    for weight in &mut weights {
        *weight /= total;
    }

    weights
}

fn write_packed_material_to_vertex(
    vertices: &mut [f32],
    vertex_offset: usize,
    material: PackedTerrainMaterial,
) {
    for slot in 0..4 {
        vertices[vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot] = material.indices[slot];
        vertices[vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot] = material.weights[slot];
    }
}

impl TerrainDensityChunk {
    fn density_at_sample(&self, sample: TerrainSampleCoord) -> f32 {
        self.densities[terrain_chunk_sample_index(sample.x, sample.y, sample.z)]
    }

    fn sample_position(&self, sample: TerrainSampleCoord) -> Vec3 {
        let origin = terrain_chunk_origin(self.coord, self.cell_size);

        Vec3 {
            x: origin.x + sample.x as f64 * self.cell_size,
            y: origin.y + sample.y as f64 * self.cell_size,
            z: origin.z + sample.z as f64 * self.cell_size,
        }
    }

    fn bounds(&self) -> TerrainChunkBounds {
        let min = terrain_chunk_origin(self.coord, self.cell_size);
        let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * self.cell_size;

        TerrainChunkBounds {
            min,
            max: Vec3 {
                x: min.x + chunk_size,
                y: min.y + chunk_size,
                z: min.z + chunk_size,
            },
        }
    }

    fn cell_bounds(&self, cell: TerrainCellCoord) -> TerrainChunkBounds {
        let min = self.sample_position(TerrainSampleCoord {
            x: cell.x,
            y: cell.y,
            z: cell.z,
        });

        TerrainChunkBounds {
            min,
            max: Vec3 {
                x: min.x + self.cell_size,
                y: min.y + self.cell_size,
                z: min.z + self.cell_size,
            },
        }
    }
}

fn neighbor_chunk<'a>(
    chunks: &'a [TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
    coord: TerrainChunkCoord,
) -> Option<&'a TerrainDensityChunk> {
    let dx = coord.x - center_coord.x;
    let dy = coord.y - center_coord.y;
    let dz = coord.z - center_coord.z;
    if !(0..=1).contains(&dx) || !(0..=1).contains(&dy) || !(0..=1).contains(&dz) {
        return None;
    }

    chunks.get(dx as usize + dy as usize * 2 + dz as usize * 4)
}

fn local_apron_cell_ref(
    center_coord: TerrainChunkCoord,
    x: usize,
    y: usize,
    z: usize,
) -> (TerrainChunkCoord, TerrainCellCoord) {
    (
        TerrainChunkCoord {
            x: center_coord.x
                + if x == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
            y: center_coord.y
                + if y == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
            z: center_coord.z
                + if z == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
        },
        TerrainCellCoord {
            x: if x == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                x
            },
            y: if y == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                y
            },
            z: if z == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                z
            },
        },
    )
}

fn sample_for_cell_corner(
    cell: TerrainCellCoord,
    corner: TerrainSampleCoord,
) -> TerrainSampleCoord {
    TerrainSampleCoord {
        x: cell.x + corner.x,
        y: cell.y + corner.y,
        z: cell.z + corner.z,
    }
}

fn centroid_of_intersections(
    intersections: &[HermiteIntersection],
    bounds: TerrainChunkBounds,
) -> Vec3 {
    let mut x = 0.0;
    let mut y = 0.0;
    let mut z = 0.0;

    for intersection in intersections {
        x += intersection.position.x;
        y += intersection.position.y;
        z += intersection.position.z;
    }

    let scale = 1.0 / intersections.len() as f64;
    clamp_vec3_to_bounds(
        Vec3 {
            x: x * scale,
            y: y * scale,
            z: z * scale,
        },
        bounds,
    )
}

fn average_normal(intersections: &[HermiteIntersection]) -> Vec3 {
    let mut normal = Vec3 {
        x: 0.0,
        y: 0.0,
        z: 0.0,
    };
    for intersection in intersections {
        normal.x += intersection.normal.x;
        normal.y += intersection.normal.y;
        normal.z += intersection.normal.z;
    }

    normalize_vec3(normal)
}

fn terrain_chunk_origin(coord: TerrainChunkCoord, cell_size: f64) -> Vec3 {
    let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * cell_size;

    Vec3 {
        x: coord.x as f64 * chunk_size,
        y: coord.y as f64 * chunk_size,
        z: coord.z as f64 * chunk_size,
    }
}

fn apron_cell_index(x: usize, y: usize, z: usize) -> usize {
    x + y * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
        + z * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
}

fn local_cell_vertex_index(indices: &[i32], x: i32, y: i32, z: i32) -> i32 {
    if x < 0
        || y < 0
        || z < 0
        || x > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
        || y > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
        || z > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
    {
        return -1;
    }

    indices[apron_cell_index(x as usize, y as usize, z as usize)]
}

fn has_sign_change(a: f32, b: f32) -> bool {
    (a <= 0.0 && b > 0.0) || (a > 0.0 && b <= 0.0)
}

fn lerp_vec3(a: Vec3, b: Vec3, t: f64) -> Vec3 {
    Vec3 {
        x: a.x + (b.x - a.x) * t,
        y: a.y + (b.y - a.y) * t,
        z: a.z + (b.z - a.z) * t,
    }
}

fn normalize_vec3(value: Vec3) -> Vec3 {
    let length = (value.x * value.x + value.y * value.y + value.z * value.z).sqrt();
    if length <= f64::EPSILON {
        return Vec3 {
            x: 0.0,
            y: 0.0,
            z: 0.0,
        };
    }

    Vec3 {
        x: value.x / length,
        y: value.y / length,
        z: value.z / length,
    }
}

fn clamp_vec3_to_bounds(position: Vec3, bounds: TerrainChunkBounds) -> Vec3 {
    Vec3 {
        x: clamp(position.x, bounds.min.x, bounds.max.x),
        y: clamp(position.y, bounds.min.y, bounds.max.y),
        z: clamp(position.z, bounds.min.z, bounds.max.z),
    }
}

fn color_for_height(height: f64) -> [f32; 3] {
    if height > 2.2 {
        return [0.72, 0.75, 0.7];
    }

    if height > 0.4 {
        return [0.38, 0.48, 0.31];
    }

    if height < -2.0 {
        return [0.26, 0.35, 0.44];
    }

    [0.31, 0.55, 0.38]
}

fn default_material_pack() -> PackedTerrainMaterial {
    PackedTerrainMaterial {
        indices: [0.0, 0.0, 0.0, 0.0],
        weights: [1.0, 0.0, 0.0, 0.0],
    }
}

fn positive_weight(weight: f64) -> f64 {
    if weight > 0.0 {
        weight
    } else {
        0.0
    }
}

fn smoothstep(edge0: f64, edge1: f64, value: f64) -> f64 {
    let t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn sample_macro_terrain(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    position: Vec3,
) -> MacroTerrainSample {
    let warp = sample_domain_warp_2d(noise, position, preset.warp);
    let large = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: warp.position.x,
            y: 17.25,
            z: warp.position.z,
        },
        preset.large_feature_noise,
    );
    let ridge = sample_ridged_fractal_simplex_3d(
        noise,
        Vec3 {
            x: warp.position.x - 137.2,
            y: 61.4,
            z: warp.position.z + 88.1,
        },
        preset.ridge_noise,
    );
    let cellular = sample_cellular_2d(
        warp.position,
        CellularNoiseOptions {
            frequency: preset.cellular.frequency,
        },
        seed ^ 0xB5297A4D,
    );
    let normalized_large_feature = clamp(large.value * 0.5 + 0.5, 0.0, 1.0);
    let cellular_edge = 1.0 - clamp(cellular.edge_distance * 2.5, 0.0, 1.0);
    let mountainness = clamp(
        normalized_large_feature * 0.55 + ridge.value * 0.45,
        0.0,
        1.0,
    );
    let cellular_contribution =
        (cellular_edge - 0.35) * preset.cellular_height_scale * mountainness;
    let ridge_contribution = ridge.value * preset.ridge_height_scale * mountainness;
    let base_elevation = preset.base_height
        + large.value * preset.height_scale
        + ridge_contribution
        + cellular_contribution;

    MacroTerrainSample {
        base_elevation,
        large_feature: large.value,
        mountainness,
        continentality: normalized_large_feature,
        erosion_susceptibility: clamp(1.0 - ridge.value * 0.5 - cellular_edge * 0.2, 0.0, 1.0),
        ridge: ridge.value,
        warp: warp.offset,
        gradient_x: large.gradient.x * preset.height_scale
            + ridge.gradient.x * preset.ridge_height_scale * mountainness,
        gradient_z: large.gradient.z * preset.height_scale
            + ridge.gradient.z * preset.ridge_height_scale * mountainness,
        cellular_edge,
    }
}

fn sample_fractal_simplex_3d(
    noise: &SimplexNoise3D,
    position: Vec3,
    options: FractalNoiseOptions,
) -> NoiseSample {
    let mut amplitude = 1.0;
    let mut frequency = options.frequency;
    let mut amplitude_sum = 0.0;
    let mut value = 0.0;
    let mut gradient_x = 0.0;
    let mut gradient_y = 0.0;
    let mut gradient_z = 0.0;

    for _ in 0..options.octaves {
        let sample = noise.sample_with_gradient(
            position.x * frequency,
            position.y * frequency,
            position.z * frequency,
        );
        value += sample.value * amplitude;
        gradient_x += sample.gradient.x * amplitude * frequency;
        gradient_y += sample.gradient.y * amplitude * frequency;
        gradient_z += sample.gradient.z * amplitude * frequency;
        amplitude_sum += amplitude;
        amplitude *= options.persistence;
        frequency *= options.lacunarity;
    }

    NoiseSample {
        value: value / amplitude_sum,
        gradient: Vec3 {
            x: gradient_x / amplitude_sum,
            y: gradient_y / amplitude_sum,
            z: gradient_z / amplitude_sum,
        },
    }
}

fn sample_ridged_fractal_simplex_3d(
    noise: &SimplexNoise3D,
    position: Vec3,
    options: RidgedFractalNoiseOptions,
) -> NoiseSample {
    let mut amplitude = 1.0;
    let mut frequency = options.frequency;
    let mut amplitude_sum = 0.0;
    let mut value = 0.0;
    let mut gradient_x = 0.0;
    let mut gradient_y = 0.0;
    let mut gradient_z = 0.0;

    for _ in 0..options.octaves {
        let sample = noise.sample_with_gradient(
            position.x * frequency,
            position.y * frequency,
            position.z * frequency,
        );
        let raw_ridge = options.ridge_offset - sample.value.abs();
        let ridge_base = clamp(raw_ridge / options.ridge_offset, 0.0, 1.0);
        let ridge_value = ridge_base.powf(options.ridge_sharpness);
        let derivative_by_value = if raw_ridge <= 0.0 || sample.value.abs() <= f64::EPSILON {
            0.0
        } else {
            -sample.value.signum()
                * options.ridge_sharpness
                * ridge_base.powf(options.ridge_sharpness - 1.0)
                / options.ridge_offset
        };

        value += ridge_value * amplitude;
        gradient_x += sample.gradient.x * derivative_by_value * amplitude * frequency;
        gradient_y += sample.gradient.y * derivative_by_value * amplitude * frequency;
        gradient_z += sample.gradient.z * derivative_by_value * amplitude * frequency;
        amplitude_sum += amplitude;
        amplitude *= options.persistence;
        frequency *= options.lacunarity;
    }

    NoiseSample {
        value: value / amplitude_sum,
        gradient: Vec3 {
            x: gradient_x / amplitude_sum,
            y: gradient_y / amplitude_sum,
            z: gradient_z / amplitude_sum,
        },
    }
}

fn sample_domain_warp_2d(
    noise: &SimplexNoise3D,
    position: Vec3,
    options: DomainWarpOptions,
) -> DomainWarpSample {
    let fractal_options = FractalNoiseOptions {
        octaves: options.octaves,
        frequency: options.frequency,
        lacunarity: options.lacunarity,
        persistence: options.persistence,
    };
    let x_warp = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x + 31.17,
            y: 93.5,
            z: position.z - 47.23,
        },
        fractal_options,
    );
    let z_warp = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x - 73.81,
            y: -18.25,
            z: position.z + 11.47,
        },
        fractal_options,
    );
    let offset = Vec3 {
        x: x_warp.value * options.amplitude,
        y: 0.0,
        z: z_warp.value * options.amplitude,
    };

    DomainWarpSample {
        offset,
        position: Vec3 {
            x: position.x + offset.x,
            y: position.y,
            z: position.z + offset.z,
        },
    }
}

fn sample_cellular_2d(
    position: Vec3,
    options: CellularNoiseOptions,
    seed: u32,
) -> CellularNoiseSample {
    let sample_x = position.x * options.frequency;
    let sample_z = position.z * options.frequency;
    let cell_x = sample_x.floor() as i32;
    let cell_z = sample_z.floor() as i32;
    let mut nearest_distance = f64::INFINITY;
    let mut second_nearest_distance = f64::INFINITY;

    for dz in -2..=2 {
        for dx in -2..=2 {
            let candidate_x = cell_x + dx;
            let candidate_z = cell_z + dz;
            let feature_x = candidate_x as f64 + hash01(candidate_x, candidate_z, seed, 0xA53C9E27);
            let feature_z = candidate_z as f64 + hash01(candidate_x, candidate_z, seed, 0xC2B2AE35);
            let distance = ((feature_x - sample_x).powi(2) + (feature_z - sample_z).powi(2)).sqrt();

            if distance < nearest_distance {
                second_nearest_distance = nearest_distance;
                nearest_distance = distance;
            } else if distance < second_nearest_distance {
                second_nearest_distance = distance;
            }
        }
    }

    CellularNoiseSample {
        edge_distance: second_nearest_distance - nearest_distance,
    }
}

impl SimplexNoise3D {
    fn new(seed: u32) -> Self {
        Self {
            perm: build_permutation(seed),
        }
    }

    fn sample_with_gradient(&self, x: f64, y: f64, z: f64) -> NoiseSample {
        let skew = (x + y + z) * F3;
        let i = fast_floor(x + skew);
        let j = fast_floor(y + skew);
        let k = fast_floor(z + skew);
        let unskew = (i + j + k) as f64 * G3;
        let cell_origin_x = i as f64 - unskew;
        let cell_origin_y = j as f64 - unskew;
        let cell_origin_z = k as f64 - unskew;
        let x0 = x - cell_origin_x;
        let y0 = y - cell_origin_y;
        let z0 = z - cell_origin_z;
        let offsets = simplex_corner_offsets(x0, y0, z0);
        let mut value = 0.0;
        let mut gradient_x = 0.0;
        let mut gradient_y = 0.0;
        let mut gradient_z = 0.0;

        for offset in offsets {
            let x_corner = x0 - offset.x as f64 + offset.unskew;
            let y_corner = y0 - offset.y as f64 + offset.unskew;
            let z_corner = z0 - offset.z as f64 + offset.unskew;
            let corner = corner_contribution(
                self.gradient_at(i + offset.x, j + offset.y, k + offset.z),
                x_corner,
                y_corner,
                z_corner,
            );
            value += corner.value;
            gradient_x += corner.gradient.x;
            gradient_y += corner.gradient.y;
            gradient_z += corner.gradient.z;
        }

        NoiseSample {
            value: value * NOISE_SCALE,
            gradient: Vec3 {
                x: gradient_x * NOISE_SCALE,
                y: gradient_y * NOISE_SCALE,
                z: gradient_z * NOISE_SCALE,
            },
        }
    }

    fn gradient_at(&self, i: i32, j: i32, k: i32) -> Vec3 {
        let k_index = (k & 255) as usize;
        let j_index = ((j + self.perm[k_index] as i32) & 255) as usize;
        let i_index = ((i + self.perm[j_index] as i32) & 255) as usize;
        let hash = self.perm[i_index] as usize;
        GRADIENTS[hash % GRADIENTS.len()]
    }
}

fn simplex_corner_offsets(x0: f64, y0: f64, z0: f64) -> [SimplexCornerOffset; 4] {
    let (mut i1, mut j1, mut k1) = (0, 0, 0);
    let (mut i2, mut j2, mut k2) = (0, 0, 0);

    if x0 >= y0 {
        if y0 >= z0 {
            i1 = 1;
            i2 = 1;
            j2 = 1;
        } else if x0 >= z0 {
            i1 = 1;
            i2 = 1;
            k2 = 1;
        } else {
            k1 = 1;
            i2 = 1;
            k2 = 1;
        }
    } else if y0 < z0 {
        k1 = 1;
        j2 = 1;
        k2 = 1;
    } else if x0 < z0 {
        j1 = 1;
        j2 = 1;
        k2 = 1;
    } else {
        j1 = 1;
        i2 = 1;
        j2 = 1;
    }

    [
        SimplexCornerOffset {
            x: 0,
            y: 0,
            z: 0,
            unskew: 0.0,
        },
        SimplexCornerOffset {
            x: i1,
            y: j1,
            z: k1,
            unskew: G3,
        },
        SimplexCornerOffset {
            x: i2,
            y: j2,
            z: k2,
            unskew: 2.0 * G3,
        },
        SimplexCornerOffset {
            x: 1,
            y: 1,
            z: 1,
            unskew: 3.0 * G3,
        },
    ]
}

fn corner_contribution(gradient: Vec3, x: f64, y: f64, z: f64) -> NoiseSample {
    let attenuation = 0.6 - x * x - y * y - z * z;
    if attenuation <= 0.0 {
        return NoiseSample {
            value: 0.0,
            gradient: Vec3 {
                x: 0.0,
                y: 0.0,
                z: 0.0,
            },
        };
    }

    let dot = gradient.x * x + gradient.y * y + gradient.z * z;
    let attenuation_2 = attenuation * attenuation;
    let attenuation_3 = attenuation_2 * attenuation;
    let attenuation_4 = attenuation_2 * attenuation_2;
    let derivative_scale = -8.0 * attenuation_3 * dot;

    NoiseSample {
        value: attenuation_4 * dot,
        gradient: Vec3 {
            x: attenuation_4 * gradient.x + derivative_scale * x,
            y: attenuation_4 * gradient.y + derivative_scale * y,
            z: attenuation_4 * gradient.z + derivative_scale * z,
        },
    }
}

fn build_permutation(seed: u32) -> [u8; 512] {
    let mut values = [0_u8; 256];
    for (index, value) in values.iter_mut().enumerate() {
        *value = index as u8;
    }

    let mut random = Mulberry32::new(seed);
    for index in (1..values.len()).rev() {
        let swap_index = (random.next() * (index + 1) as f64).floor() as usize;
        values.swap(index, swap_index);
    }

    let mut perm = [0_u8; 512];
    for (index, value) in perm.iter_mut().enumerate() {
        *value = values[index & 255];
    }
    perm
}

struct Mulberry32 {
    state: u32,
}

impl Mulberry32 {
    fn new(seed: u32) -> Self {
        Self { state: seed }
    }

    fn next(&mut self) -> f64 {
        self.state = self.state.wrapping_add(0x6D2B79F5);
        let mut value = self.state;
        value = (value ^ (value >> 15)).wrapping_mul(value | 1);
        value ^= value.wrapping_add((value ^ (value >> 7)).wrapping_mul(value | 61));
        (value ^ (value >> 14)) as f64 * UINT32_SCALE
    }
}

fn terrain_preset(preset: u32) -> TerrainPresetDefinition {
    TERRAIN_PRESETS
        .get(preset as usize)
        .copied()
        .unwrap_or(TERRAIN_PRESETS[DEFAULT_TERRAIN_PRESET as usize])
}

fn hash01(x: i32, z: i32, seed: u32, salt: u32) -> f64 {
    hash_uint32(x, z, seed, salt) as f64 * UINT32_SCALE
}

fn hash_uint32(x: i32, z: i32, seed: u32, salt: u32) -> u32 {
    let mut value = seed ^ salt;
    value ^= (x as u32).wrapping_mul(0x85EBCA6B);
    value = (value ^ (value >> 13)).wrapping_mul(0xC2B2AE35);
    value ^= (z as u32).wrapping_mul(0x27D4EB2F);
    value = (value ^ (value >> 16)).wrapping_mul(0x165667B1);
    value ^ (value >> 15)
}

fn terrain_chunk_sample_index(x: usize, y: usize, z: usize) -> usize {
    x + y * TERRAIN_CHUNK_SAMPLES_PER_AXIS
        + z * TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS
}

fn clamp(value: f64, minimum: f64, maximum: f64) -> f64 {
    value.max(minimum).min(maximum)
}

fn fast_floor(value: f64) -> i32 {
    value.floor() as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exported_version_is_stable() {
        assert_eq!(ofg_terrain_core_version(), 1);
        assert_eq!(ofg_terrain_core_preset_count(), 4);
    }

    #[test]
    fn height_sampling_is_deterministic() {
        let a = height_at(0x0F6, 1, 12.5, -20.25);
        let b = height_at(0x0F6, 1, 12.5, -20.25);

        assert_eq!(a.to_bits(), b.to_bits());
    }

    #[test]
    fn presets_produce_different_surfaces() {
        let rolling = height_at(0x0F6, 1, 44.0, -36.0);
        let mountains = height_at(0x0F6, 2, 44.0, -36.0);
        let highland = height_at(0x0F6, 3, 44.0, -36.0);

        assert!((rolling - mountains).abs() > 0.1);
        assert!((rolling - highland).abs() > 0.1);
    }

    #[test]
    fn density_crosses_zero_near_surface() {
        let height = height_at(0x0F6, 1, -18.0, 27.0);
        let below = ofg_density_at(0x0F6, 1, -18.0, height - 0.5, 27.0);
        let above = ofg_density_at(0x0F6, 1, -18.0, height + 0.5, 27.0);

        assert!(below <= 0.0);
        assert!(above > 0.0);
    }

    #[test]
    fn fills_density_chunk_buffer_in_terrain_chunk_order() {
        ofg_fill_density_chunk(0x0F6, 1, -1, 0, 2, 1.0);
        let buffer = unsafe {
            std::slice::from_raw_parts(
                ofg_density_chunk_buffer_ptr(),
                ofg_density_chunk_sample_count() as usize,
            )
        };
        let origin_x = -32.0;
        let origin_y = 0.0;
        let origin_z = 64.0;

        assert_eq!(buffer.len(), TERRAIN_CHUNK_SAMPLE_COUNT);
        assert_eq!(
            buffer[terrain_chunk_sample_index(0, 0, 0)].to_bits(),
            (ofg_density_at(0x0F6, 1, origin_x, origin_y, origin_z) as f32).to_bits()
        );
        assert_eq!(
            buffer[terrain_chunk_sample_index(1, 0, 0)].to_bits(),
            (ofg_density_at(0x0F6, 1, origin_x + 1.0, origin_y, origin_z) as f32).to_bits()
        );
        assert_eq!(
            buffer[terrain_chunk_sample_index(0, 1, 0)].to_bits(),
            (ofg_density_at(0x0F6, 1, origin_x, origin_y + 1.0, origin_z) as f32).to_bits()
        );
        assert_eq!(
            buffer[terrain_chunk_sample_index(0, 0, 1)].to_bits(),
            (ofg_density_at(0x0F6, 1, origin_x, origin_y, origin_z + 1.0) as f32).to_bits()
        );
    }

    #[test]
    fn builds_renderable_chunk_mesh_buffers() {
        let index_count = ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0);
        let vertex_len = ofg_mesh_vertex_buffer_len() as usize;
        let index_len = ofg_mesh_index_buffer_len() as usize;
        let vertices =
            unsafe { std::slice::from_raw_parts(ofg_mesh_vertex_buffer_ptr(), vertex_len) };
        let indices = unsafe { std::slice::from_raw_parts(ofg_mesh_index_buffer_ptr(), index_len) };

        assert!(index_count > 0);
        assert!(vertex_len > 0);
        assert!(vertices.iter().all(|value| value.is_finite()));
        assert_eq!(index_count as usize, index_len);
        assert_eq!(vertex_len % FLOATS_PER_VERTEX, 0);
        assert_eq!(index_len % 3, 0);

        let vertex_count = vertex_len / FLOATS_PER_VERTEX;
        for index in indices {
            assert!((*index as usize) < vertex_count);
        }
    }
}
