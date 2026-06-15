//! Sine-wave terrain heightfield for the rebuild baseline.

use crate::variant::{
    terrain_variant_for_preset, TerrainShapeParameters, TerrainVariantDescriptor,
    TerrainVariantValidationError,
};

/// Samples the active baseline heightfield for a preset code.
pub fn height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    height_at_for_variant(seed, terrain_variant_for_preset(preset), x, z).unwrap_or(0.0)
}

/// Samples the active baseline heightfield for a terrain variant.
pub fn height_at_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    x: f64,
    z: f64,
) -> Result<f64, TerrainVariantValidationError> {
    descriptor.validate()?;
    Ok(height_at_with_shape(seed, descriptor.shape, x, z))
}

/// Samples the sine-wave heightfield directly from shape parameters.
pub fn height_at_with_shape(seed: u32, shape: TerrainShapeParameters, x: f64, z: f64) -> f64 {
    let phase = f64::from(seed % 10_000) * 0.013;
    let wavelength = shape.wavelength_meters.max(1.0);
    let frequency = std::f64::consts::TAU / wavelength;
    let primary = ((x * frequency + phase).sin() + (z * frequency * 0.75 - phase).cos()) * 0.5;
    let secondary = ((x + z) * frequency * 0.5 + phase).sin() * shape.secondary_scale;
    shape.base_height + (primary + secondary) * shape.height_scale
}
