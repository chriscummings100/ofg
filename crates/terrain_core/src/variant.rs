//! Minimal terrain variant metadata for the sine-wave baseline.

use crate::node::DEFAULT_TERRAIN_PRESET;

pub const TERRAIN_VARIANT_DESCRIPTOR_VERSION: u32 = 2;
pub const TERRAIN_VARIANT_FLAT_VALUE_COUNT: usize = 8;
pub const TERRAIN_BASE_HEIGHT_MIN: f64 = -4096.0;
pub const TERRAIN_BASE_HEIGHT_MAX: f64 = 4096.0;
pub const TERRAIN_HEIGHT_SCALE_MIN: f64 = 0.0;
pub const TERRAIN_HEIGHT_SCALE_MAX: f64 = 2048.0;
pub const TERRAIN_RIDGE_HEIGHT_SCALE_MAX: f64 = 0.0;
pub const TERRAIN_CELLULAR_HEIGHT_SCALE_MAX: f64 = 0.0;
pub const TERRAIN_DETAIL_AMPLITUDE_MAX: f64 = 0.0;
pub const TERRAIN_WARP_AMPLITUDE_MAX: f64 = 0.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainVariantDescriptor {
    pub version: u32,
    pub preset: u32,
    pub shape: TerrainShapeParameters,
    pub material_bias: TerrainMaterialBias,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainShapeParameters {
    pub base_height: f64,
    pub height_scale: f64,
    pub wavelength_meters: f64,
    pub secondary_scale: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct TerrainMaterialBias {
    pub grass: f64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainPresetMetadata {
    pub code: u32,
    pub id: &'static str,
    pub name: &'static str,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainBiomeWeightsProbe {
    pub grassland: f64,
    pub temperate_forest: f64,
    pub wetland: f64,
    pub coast_beach: f64,
    pub dry_badland: f64,
    pub alpine_meadow: f64,
    pub high_mountain_rock: f64,
    pub snow_tundra: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainVariantProbeSummary {
    pub sample_count: usize,
    pub height_min: f64,
    pub height_max: f64,
    pub slope_min: f64,
    pub slope_max: f64,
    pub macro_base_elevation: f64,
    pub mountainness: f64,
    pub ridge: f64,
    pub cellular_edge: f64,
    pub material_indices: [u32; 4],
    pub material_weights: [f64; 4],
    pub biome_weights: TerrainBiomeWeightsProbe,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerrainVariantValidationError {
    message: String,
}

impl TerrainVariantValidationError {
    /// Creates a validation error with a stable diagnostic string.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl std::fmt::Display for TerrainVariantValidationError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for TerrainVariantValidationError {}

pub const TERRAIN_PRESET_METADATA: [TerrainPresetMetadata; 1] = [TerrainPresetMetadata {
    code: 0,
    id: "sineGrass",
    name: "Sine Grass",
}];

/// Returns the number of active baseline terrain presets.
pub fn terrain_preset_count() -> u32 {
    TERRAIN_PRESET_METADATA.len() as u32
}

/// Returns preset metadata, defaulting to the sine baseline for unknown codes.
pub fn terrain_preset_metadata(code: u32) -> TerrainPresetMetadata {
    TERRAIN_PRESET_METADATA
        .iter()
        .copied()
        .find(|preset| preset.code == code)
        .unwrap_or(TERRAIN_PRESET_METADATA[0])
}

/// Returns the baseline terrain variant for a preset code.
pub fn terrain_variant_for_preset(preset: u32) -> TerrainVariantDescriptor {
    let _metadata = terrain_preset_metadata(preset);
    TerrainVariantDescriptor {
        version: TERRAIN_VARIANT_DESCRIPTOR_VERSION,
        preset: DEFAULT_TERRAIN_PRESET,
        shape: TerrainShapeParameters {
            base_height: 0.0,
            height_scale: 10.0,
            wavelength_meters: 128.0,
            secondary_scale: 0.35,
        },
        material_bias: TerrainMaterialBias { grass: 1.0 },
    }
}

impl TerrainVariantDescriptor {
    /// Validates that the baseline descriptor has finite, usable values.
    pub fn validate(self) -> Result<(), TerrainVariantValidationError> {
        if self.version != TERRAIN_VARIANT_DESCRIPTOR_VERSION {
            return Err(TerrainVariantValidationError::new(
                "unsupported terrain variant descriptor version",
            ));
        }
        if !self.shape.base_height.is_finite()
            || !self.shape.height_scale.is_finite()
            || !self.shape.wavelength_meters.is_finite()
            || self.shape.height_scale < 0.0
            || self.shape.wavelength_meters <= 0.0
        {
            return Err(TerrainVariantValidationError::new(
                "invalid sine terrain variant shape",
            ));
        }
        Ok(())
    }
}

/// Serializes a terrain descriptor to the flat browser editor shape.
pub fn terrain_variant_flat_values(
    descriptor: TerrainVariantDescriptor,
) -> [f64; TERRAIN_VARIANT_FLAT_VALUE_COUNT] {
    [
        descriptor.version as f64,
        descriptor.preset as f64,
        descriptor.shape.base_height,
        descriptor.shape.height_scale,
        descriptor.shape.wavelength_meters,
        descriptor.shape.secondary_scale,
        descriptor.material_bias.grass,
        terrain_variant_cache_key(descriptor) as f64,
    ]
}

/// Parses a flat terrain descriptor from browser values.
pub fn terrain_variant_from_flat_values(
    values: &[f64],
) -> Result<TerrainVariantDescriptor, TerrainVariantValidationError> {
    if values.len() != TERRAIN_VARIANT_FLAT_VALUE_COUNT {
        return Err(TerrainVariantValidationError::new(
            "terrain variant flat value count mismatch",
        ));
    }
    let descriptor = TerrainVariantDescriptor {
        version: values[0] as u32,
        preset: values[1] as u32,
        shape: TerrainShapeParameters {
            base_height: values[2],
            height_scale: values[3],
            wavelength_meters: values[4],
            secondary_scale: values[5],
        },
        material_bias: TerrainMaterialBias { grass: values[6] },
    };
    descriptor.validate()?;
    Ok(descriptor)
}

/// Returns a small deterministic descriptor cache key.
pub fn terrain_variant_cache_key(descriptor: TerrainVariantDescriptor) -> u64 {
    let values = terrain_variant_flat_values_without_key(descriptor);
    values.iter().fold(0xcbf29ce484222325_u64, |hash, value| {
        (hash ^ value.to_bits()).wrapping_mul(0x100000001b3)
    })
}

/// Builds a probe summary for the current sine baseline.
pub fn terrain_variant_probe_summary(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    x: f64,
    z: f64,
    _radius_meters: f64,
) -> Result<TerrainVariantProbeSummary, TerrainVariantValidationError> {
    let center = crate::heightfield::height_at_for_variant(seed, descriptor, x, z)?;
    Ok(TerrainVariantProbeSummary {
        sample_count: 1,
        height_min: center,
        height_max: center,
        slope_min: 0.0,
        slope_max: 0.0,
        macro_base_elevation: descriptor.shape.base_height,
        mountainness: 0.0,
        ridge: 0.0,
        cellular_edge: 0.0,
        material_indices: [0, 0, 0, 0],
        material_weights: [1.0, 0.0, 0.0, 0.0],
        biome_weights: TerrainBiomeWeightsProbe {
            grassland: 1.0,
            temperate_forest: 0.0,
            wetland: 0.0,
            coast_beach: 0.0,
            dry_badland: 0.0,
            alpine_meadow: 0.0,
            high_mountain_rock: 0.0,
            snow_tundra: 0.0,
        },
    })
}

fn terrain_variant_flat_values_without_key(descriptor: TerrainVariantDescriptor) -> [f64; 7] {
    [
        descriptor.version as f64,
        descriptor.preset as f64,
        descriptor.shape.base_height,
        descriptor.shape.height_scale,
        descriptor.shape.wavelength_meters,
        descriptor.shape.secondary_scale,
        descriptor.material_bias.grass,
    ]
}
