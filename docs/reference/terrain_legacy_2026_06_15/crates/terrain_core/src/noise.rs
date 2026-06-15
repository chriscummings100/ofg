use crate::*;

pub(crate) struct NoiseSample {
    pub(crate) value: f64,
    pub(crate) gradient: Vec3,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FractalNoiseOptions {
    pub octaves: u32,
    pub frequency: f64,
    pub lacunarity: f64,
    pub persistence: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RidgedFractalNoiseOptions {
    pub octaves: u32,
    pub frequency: f64,
    pub lacunarity: f64,
    pub persistence: f64,
    pub ridge_offset: f64,
    pub ridge_sharpness: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DomainWarpOptions {
    pub octaves: u32,
    pub frequency: f64,
    pub lacunarity: f64,
    pub persistence: f64,
    pub amplitude: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CellularNoiseOptions {
    pub frequency: f64,
}

#[derive(Clone, Copy)]
pub(crate) struct CellularNoiseSample {
    pub(crate) edge_distance: f64,
}

#[derive(Clone, Copy)]
pub(crate) struct DomainWarpSample {
    pub(crate) position: Vec3,
    pub(crate) offset: Vec3,
}

#[derive(Clone, Copy)]
pub(crate) struct SimplexCornerOffset {
    pub(crate) x: i32,
    pub(crate) y: i32,
    pub(crate) z: i32,
    pub(crate) unskew: f64,
}

pub(crate) struct SimplexNoise3D {
    pub(crate) perm: [u8; 512],
}

pub(crate) const GRADIENTS: [Vec3; 12] = [
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

pub(crate) fn sample_fractal_simplex_3d(
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

pub(crate) fn sample_ridged_fractal_simplex_3d(
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

pub(crate) fn sample_domain_warp_2d(
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

pub(crate) fn sample_cellular_2d(
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
    pub(crate) fn new(seed: u32) -> Self {
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

pub(crate) fn simplex_corner_offsets(x0: f64, y0: f64, z0: f64) -> [SimplexCornerOffset; 4] {
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

pub(crate) fn corner_contribution(gradient: Vec3, x: f64, y: f64, z: f64) -> NoiseSample {
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

pub(crate) fn build_permutation(seed: u32) -> [u8; 512] {
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

pub(crate) struct Mulberry32 {
    pub(crate) state: u32,
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

pub(crate) fn hash01(x: i32, z: i32, seed: u32, salt: u32) -> f64 {
    hash_uint32(x, z, seed, salt) as f64 * UINT32_SCALE
}

pub(crate) fn hash_uint32(x: i32, z: i32, seed: u32, salt: u32) -> u32 {
    let mut value = seed ^ salt;
    value ^= (x as u32).wrapping_mul(0x85EBCA6B);
    value = (value ^ (value >> 13)).wrapping_mul(0xC2B2AE35);
    value ^= (z as u32).wrapping_mul(0x27D4EB2F);
    value = (value ^ (value >> 16)).wrapping_mul(0x165667B1);
    value ^ (value >> 15)
}

pub(crate) fn fast_floor(value: f64) -> i32 {
    value.floor() as i32
}
