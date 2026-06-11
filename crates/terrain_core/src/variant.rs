// Defines the terrain variant descriptor that editor tooling will tune.
// Shape parameters are the Rust-owned source of truth for macro elevation,
// density detail, and the preset catalog exposed to browser/debug surfaces.

use crate::*;

pub const TERRAIN_VARIANT_DESCRIPTOR_VERSION: u32 = 1;
pub const TERRAIN_VARIANT_FLAT_VALUE_COUNT: usize = 32;
const FNV_OFFSET_BASIS: u64 = 14_695_981_039_346_656_037;
const FNV_PRIME: u64 = 1_099_511_628_211;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainPresetMetadata {
    pub code: u32,
    pub id: &'static str,
    pub name: &'static str,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainShapeParameters {
    pub base_height: f64,
    pub height_scale: f64,
    pub large_feature_noise: FractalNoiseOptions,
    pub ridge_height_scale: f64,
    pub ridge_noise: RidgedFractalNoiseOptions,
    pub warp: DomainWarpOptions,
    pub cellular: CellularNoiseOptions,
    pub cellular_height_scale: f64,
    pub detail_noise: FractalNoiseOptions,
    pub detail_amplitude: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainMaterialBias {
    pub meadow: f64,
    pub dry_ground: f64,
    pub wetland: f64,
    pub rock: f64,
    pub snow: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainVariantDescriptor {
    pub version: u32,
    pub preset: u32,
    pub shape: TerrainShapeParameters,
    pub material_bias: TerrainMaterialBias,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainVariantValidationError {
    InvalidFlatValueCount,
    UnsupportedVersion,
    InvalidPreset,
    InvalidBaseHeight,
    InvalidHeightScale,
    InvalidRidgeHeightScale,
    InvalidCellularHeightScale,
    InvalidDetailAmplitude,
    InvalidFractalNoise,
    InvalidRidgedNoise,
    InvalidWarpNoise,
    InvalidCellularNoise,
    InvalidMaterialBias,
}

impl std::fmt::Display for TerrainVariantValidationError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            TerrainVariantValidationError::InvalidFlatValueCount => {
                "terrain variant flat value count is invalid"
            }
            TerrainVariantValidationError::UnsupportedVersion => {
                "terrain variant descriptor version is unsupported"
            }
            TerrainVariantValidationError::InvalidPreset => {
                "terrain variant preset code is invalid"
            }
            TerrainVariantValidationError::InvalidBaseHeight => {
                "terrain variant base height is invalid"
            }
            TerrainVariantValidationError::InvalidHeightScale => {
                "terrain variant height scale is invalid"
            }
            TerrainVariantValidationError::InvalidRidgeHeightScale => {
                "terrain variant ridge height scale is invalid"
            }
            TerrainVariantValidationError::InvalidCellularHeightScale => {
                "terrain variant cellular height scale is invalid"
            }
            TerrainVariantValidationError::InvalidDetailAmplitude => {
                "terrain variant detail amplitude is invalid"
            }
            TerrainVariantValidationError::InvalidFractalNoise => {
                "terrain variant fractal noise options are invalid"
            }
            TerrainVariantValidationError::InvalidRidgedNoise => {
                "terrain variant ridged noise options are invalid"
            }
            TerrainVariantValidationError::InvalidWarpNoise => {
                "terrain variant domain warp options are invalid"
            }
            TerrainVariantValidationError::InvalidCellularNoise => {
                "terrain variant cellular noise options are invalid"
            }
            TerrainVariantValidationError::InvalidMaterialBias => {
                "terrain variant material bias is invalid"
            }
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for TerrainVariantValidationError {}

pub(crate) const TERRAIN_PRESET_METADATA: [TerrainPresetMetadata; 4] = [
    TerrainPresetMetadata {
        code: 0,
        id: "seed",
        name: "Seed",
    },
    TerrainPresetMetadata {
        code: 1,
        id: "rollingHills",
        name: "Rolling Hills",
    },
    TerrainPresetMetadata {
        code: 2,
        id: "mountainValley",
        name: "Mountain Valley",
    },
    TerrainPresetMetadata {
        code: 3,
        id: "rockyHighland",
        name: "Rocky Highland",
    },
];

impl Default for TerrainMaterialBias {
    fn default() -> Self {
        Self {
            meadow: 1.0,
            dry_ground: 1.0,
            wetland: 1.0,
            rock: 1.0,
            snow: 1.0,
        }
    }
}

impl TerrainVariantDescriptor {
    pub fn validate(&self) -> Result<(), TerrainVariantValidationError> {
        if self.version != TERRAIN_VARIANT_DESCRIPTOR_VERSION {
            return Err(TerrainVariantValidationError::UnsupportedVersion);
        }
        if (self.preset as usize) >= terrain_preset_count() as usize {
            return Err(TerrainVariantValidationError::InvalidPreset);
        }
        self.shape.validate()?;
        self.material_bias.validate()
    }
}

impl TerrainShapeParameters {
    pub fn validate(&self) -> Result<(), TerrainVariantValidationError> {
        validate_finite_range(self.base_height, -512.0, 512.0)
            .ok_or(TerrainVariantValidationError::InvalidBaseHeight)?;
        validate_finite_range(self.height_scale, -256.0, 256.0)
            .ok_or(TerrainVariantValidationError::InvalidHeightScale)?;
        validate_finite_range(self.ridge_height_scale, 0.0, 256.0)
            .ok_or(TerrainVariantValidationError::InvalidRidgeHeightScale)?;
        validate_finite_range(self.cellular_height_scale, 0.0, 256.0)
            .ok_or(TerrainVariantValidationError::InvalidCellularHeightScale)?;
        validate_finite_range(self.detail_amplitude, 0.0, 128.0)
            .ok_or(TerrainVariantValidationError::InvalidDetailAmplitude)?;
        validate_fractal_noise(self.large_feature_noise)
            .ok_or(TerrainVariantValidationError::InvalidFractalNoise)?;
        validate_ridged_noise(self.ridge_noise)
            .ok_or(TerrainVariantValidationError::InvalidRidgedNoise)?;
        validate_warp_noise(self.warp).ok_or(TerrainVariantValidationError::InvalidWarpNoise)?;
        validate_cellular_noise(self.cellular)
            .ok_or(TerrainVariantValidationError::InvalidCellularNoise)?;
        validate_fractal_noise(self.detail_noise)
            .ok_or(TerrainVariantValidationError::InvalidFractalNoise)
    }
}

impl TerrainMaterialBias {
    pub fn validate(&self) -> Result<(), TerrainVariantValidationError> {
        for value in [
            self.meadow,
            self.dry_ground,
            self.wetland,
            self.rock,
            self.snow,
        ] {
            validate_finite_range(value, 0.0, 4.0)
                .ok_or(TerrainVariantValidationError::InvalidMaterialBias)?;
        }

        Ok(())
    }
}

pub fn terrain_preset_count() -> u32 {
    TERRAIN_PRESET_METADATA.len() as u32
}

pub fn terrain_preset_metadata(preset: u32) -> TerrainPresetMetadata {
    TERRAIN_PRESET_METADATA[terrain_preset_index(preset) as usize]
}

pub fn terrain_variant_for_preset(preset: u32) -> TerrainVariantDescriptor {
    let preset = terrain_preset_index(preset);
    TerrainVariantDescriptor {
        version: TERRAIN_VARIANT_DESCRIPTOR_VERSION,
        preset,
        shape: terrain_preset(preset),
        material_bias: TerrainMaterialBias::default(),
    }
}

pub fn terrain_variant_flat_values(
    descriptor: TerrainVariantDescriptor,
) -> [f64; TERRAIN_VARIANT_FLAT_VALUE_COUNT] {
    let shape = descriptor.shape;
    let material = descriptor.material_bias;

    [
        descriptor.version as f64,
        descriptor.preset as f64,
        shape.base_height,
        shape.height_scale,
        shape.large_feature_noise.octaves as f64,
        shape.large_feature_noise.frequency,
        shape.large_feature_noise.lacunarity,
        shape.large_feature_noise.persistence,
        shape.ridge_height_scale,
        shape.ridge_noise.octaves as f64,
        shape.ridge_noise.frequency,
        shape.ridge_noise.lacunarity,
        shape.ridge_noise.persistence,
        shape.ridge_noise.ridge_offset,
        shape.ridge_noise.ridge_sharpness,
        shape.warp.octaves as f64,
        shape.warp.frequency,
        shape.warp.lacunarity,
        shape.warp.persistence,
        shape.warp.amplitude,
        shape.cellular.frequency,
        shape.cellular_height_scale,
        shape.detail_noise.octaves as f64,
        shape.detail_noise.frequency,
        shape.detail_noise.lacunarity,
        shape.detail_noise.persistence,
        shape.detail_amplitude,
        material.meadow,
        material.dry_ground,
        material.wetland,
        material.rock,
        material.snow,
    ]
}

pub fn terrain_variant_from_flat_values(
    values: &[f64],
) -> Result<TerrainVariantDescriptor, TerrainVariantValidationError> {
    if values.len() != TERRAIN_VARIANT_FLAT_VALUE_COUNT {
        return Err(TerrainVariantValidationError::InvalidFlatValueCount);
    }

    let mut cursor = 0;
    let descriptor = TerrainVariantDescriptor {
        version: read_flat_u32(
            values,
            &mut cursor,
            TerrainVariantValidationError::UnsupportedVersion,
        )?,
        preset: read_flat_u32(
            values,
            &mut cursor,
            TerrainVariantValidationError::InvalidPreset,
        )?,
        shape: TerrainShapeParameters {
            base_height: read_flat_f64(values, &mut cursor),
            height_scale: read_flat_f64(values, &mut cursor),
            large_feature_noise: FractalNoiseOptions {
                octaves: read_flat_u32(
                    values,
                    &mut cursor,
                    TerrainVariantValidationError::InvalidFractalNoise,
                )?,
                frequency: read_flat_f64(values, &mut cursor),
                lacunarity: read_flat_f64(values, &mut cursor),
                persistence: read_flat_f64(values, &mut cursor),
            },
            ridge_height_scale: read_flat_f64(values, &mut cursor),
            ridge_noise: RidgedFractalNoiseOptions {
                octaves: read_flat_u32(
                    values,
                    &mut cursor,
                    TerrainVariantValidationError::InvalidRidgedNoise,
                )?,
                frequency: read_flat_f64(values, &mut cursor),
                lacunarity: read_flat_f64(values, &mut cursor),
                persistence: read_flat_f64(values, &mut cursor),
                ridge_offset: read_flat_f64(values, &mut cursor),
                ridge_sharpness: read_flat_f64(values, &mut cursor),
            },
            warp: DomainWarpOptions {
                octaves: read_flat_u32(
                    values,
                    &mut cursor,
                    TerrainVariantValidationError::InvalidWarpNoise,
                )?,
                frequency: read_flat_f64(values, &mut cursor),
                lacunarity: read_flat_f64(values, &mut cursor),
                persistence: read_flat_f64(values, &mut cursor),
                amplitude: read_flat_f64(values, &mut cursor),
            },
            cellular: CellularNoiseOptions {
                frequency: read_flat_f64(values, &mut cursor),
            },
            cellular_height_scale: read_flat_f64(values, &mut cursor),
            detail_noise: FractalNoiseOptions {
                octaves: read_flat_u32(
                    values,
                    &mut cursor,
                    TerrainVariantValidationError::InvalidFractalNoise,
                )?,
                frequency: read_flat_f64(values, &mut cursor),
                lacunarity: read_flat_f64(values, &mut cursor),
                persistence: read_flat_f64(values, &mut cursor),
            },
            detail_amplitude: read_flat_f64(values, &mut cursor),
        },
        material_bias: TerrainMaterialBias {
            meadow: read_flat_f64(values, &mut cursor),
            dry_ground: read_flat_f64(values, &mut cursor),
            wetland: read_flat_f64(values, &mut cursor),
            rock: read_flat_f64(values, &mut cursor),
            snow: read_flat_f64(values, &mut cursor),
        },
    };

    descriptor.validate()?;
    Ok(descriptor)
}

pub fn terrain_variant_cache_key(descriptor: TerrainVariantDescriptor) -> u64 {
    let mut hash = FNV_OFFSET_BASIS;
    for value in terrain_variant_flat_values(descriptor) {
        hash ^= value.to_bits();
        hash = hash.wrapping_mul(FNV_PRIME);
    }

    hash
}

fn validate_fractal_noise(options: FractalNoiseOptions) -> Option<()> {
    validate_octaves(options.octaves)?;
    validate_finite_range(options.frequency, 0.000001, 1.0)?;
    validate_finite_range(options.lacunarity, 1.0, 8.0)?;
    validate_finite_range(options.persistence, 0.0, 1.5)
}

fn validate_ridged_noise(options: RidgedFractalNoiseOptions) -> Option<()> {
    validate_octaves(options.octaves)?;
    validate_finite_range(options.frequency, 0.000001, 1.0)?;
    validate_finite_range(options.lacunarity, 1.0, 8.0)?;
    validate_finite_range(options.persistence, 0.0, 1.5)?;
    validate_finite_range(options.ridge_offset, 0.000001, 4.0)?;
    validate_finite_range(options.ridge_sharpness, 0.1, 8.0)
}

fn validate_warp_noise(options: DomainWarpOptions) -> Option<()> {
    validate_octaves(options.octaves)?;
    validate_finite_range(options.frequency, 0.000001, 1.0)?;
    validate_finite_range(options.lacunarity, 1.0, 8.0)?;
    validate_finite_range(options.persistence, 0.0, 1.5)?;
    validate_finite_range(options.amplitude, 0.0, 256.0)
}

fn validate_cellular_noise(options: CellularNoiseOptions) -> Option<()> {
    validate_finite_range(options.frequency, 0.000001, 1.0)
}

fn validate_octaves(octaves: u32) -> Option<()> {
    if (1..=8).contains(&octaves) {
        Some(())
    } else {
        None
    }
}

fn validate_finite_range(value: f64, min: f64, max: f64) -> Option<()> {
    if value.is_finite() && value >= min && value <= max {
        Some(())
    } else {
        None
    }
}

fn read_flat_f64(values: &[f64], cursor: &mut usize) -> f64 {
    let value = values[*cursor];
    *cursor += 1;
    value
}

fn read_flat_u32(
    values: &[f64],
    cursor: &mut usize,
    error: TerrainVariantValidationError,
) -> Result<u32, TerrainVariantValidationError> {
    let value = read_flat_f64(values, cursor);
    if value.is_finite() && value.fract() == 0.0 && value >= 0.0 && value <= u32::MAX as f64 {
        Ok(value as u32)
    } else {
        Err(error)
    }
}
