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
}
