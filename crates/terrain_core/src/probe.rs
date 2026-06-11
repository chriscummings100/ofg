// Rust-owned terrain probe helpers for editor/debug readouts. These functions
// sample the same height, density, material, and biome paths used by meshing.

use crate::*;

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
    pub sample_count: u32,
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

pub fn terrain_variant_probe_summary(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    center_x: f64,
    center_z: f64,
    radius: f64,
) -> Result<TerrainVariantProbeSummary, TerrainVariantValidationError> {
    descriptor.validate()?;
    let noise = SimplexNoise3D::new(seed);
    let sample_offsets = [
        (0.0, 0.0),
        (-radius, 0.0),
        (radius, 0.0),
        (0.0, -radius),
        (0.0, radius),
    ];
    let mut height_min = f64::INFINITY;
    let mut height_max = f64::NEG_INFINITY;
    let mut slope_min = f64::INFINITY;
    let mut slope_max = f64::NEG_INFINITY;

    for (offset_x, offset_z) in sample_offsets {
        let x = center_x + offset_x;
        let z = center_z + offset_z;
        let height = height_at_with_shape(seed, descriptor.shape, x, z);
        let macro_sample =
            sample_macro_terrain(&noise, descriptor.shape, seed, Vec3 { x, y: height, z });
        let density_sample = density_at_position_with_macro(
            &noise,
            descriptor.shape,
            Vec3 { x, y: height, z },
            macro_sample,
        );
        let normal = normalize_vec3(density_sample.gradient);
        let slope = clamp(1.0 - normal.y, 0.0, 1.0);

        height_min = height_min.min(height);
        height_max = height_max.max(height);
        slope_min = slope_min.min(slope);
        slope_max = slope_max.max(slope);
    }

    let center_height = height_at_with_shape(seed, descriptor.shape, center_x, center_z);
    let center_position = Vec3 {
        x: center_x,
        y: center_height,
        z: center_z,
    };
    let macro_sample = sample_macro_terrain(&noise, descriptor.shape, seed, center_position);
    let material = material_pack_at(
        &noise,
        descriptor.shape,
        descriptor.material_bias,
        seed,
        center_position,
    );
    let biome = biome_weights_at(
        &noise,
        descriptor.shape,
        seed,
        center_position,
        macro_sample,
    );

    Ok(TerrainVariantProbeSummary {
        sample_count: sample_offsets.len() as u32,
        height_min,
        height_max,
        slope_min,
        slope_max,
        macro_base_elevation: macro_sample.base_elevation,
        mountainness: macro_sample.mountainness,
        ridge: macro_sample.ridge,
        cellular_edge: macro_sample.cellular_edge,
        material_indices: material.indices.map(|index| index as u32),
        material_weights: material.weights.map(f64::from),
        biome_weights: TerrainBiomeWeightsProbe {
            grassland: biome.grassland,
            temperate_forest: biome.temperate_forest,
            wetland: biome.wetland,
            coast_beach: biome.coast_beach,
            dry_badland: biome.dry_badland,
            alpine_meadow: biome.alpine_meadow,
            high_mountain_rock: biome.high_mountain_rock,
            snow_tundra: biome.snow_tundra,
        },
    })
}
