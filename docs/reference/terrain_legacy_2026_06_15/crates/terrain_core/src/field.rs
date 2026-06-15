use crate::*;

#[derive(Clone, Copy)]
#[allow(dead_code)]
pub(crate) struct MacroTerrainSample {
    pub(crate) base_elevation: f64,
    pub(crate) large_feature: f64,
    pub(crate) mountainness: f64,
    pub(crate) continentality: f64,
    pub(crate) erosion_susceptibility: f64,
    pub(crate) ridge: f64,
    pub(crate) warp: Vec3,
    pub(crate) gradient_x: f64,
    pub(crate) gradient_z: f64,
    pub(crate) cellular_edge: f64,
}

#[derive(Clone, Copy)]
#[allow(dead_code)]
pub(crate) struct DensitySample {
    pub(crate) density: f64,
    pub(crate) gradient: Vec3,
}

pub fn height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    height_at_with_shape(seed, terrain_preset(preset), x, z)
}

pub fn height_at_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    x: f64,
    z: f64,
) -> Result<f64, TerrainVariantValidationError> {
    descriptor.validate()?;
    Ok(height_at_with_shape(seed, descriptor.shape, x, z))
}

pub fn height_at_with_shape(seed: u32, shape: TerrainShapeParameters, x: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let macro_sample = sample_macro_terrain(&noise, shape, seed, Vec3 { x, y: 0.0, z });
    let search_half_extent = surface_search_half_extent_m(shape);
    let mut upper_y = macro_sample.base_elevation + search_half_extent;
    let mut upper_density =
        density_at_position(&noise, shape, seed, Vec3 { x, y: upper_y, z }).density;
    let mut lower_y = upper_y - SURFACE_SEARCH_STEP;
    let minimum_y = macro_sample.base_elevation - search_half_extent;

    while lower_y >= minimum_y {
        let lower_density =
            density_at_position(&noise, shape, seed, Vec3 { x, y: lower_y, z }).density;
        if lower_density <= 0.0 && upper_density > 0.0 {
            return refine_surface_height(&noise, shape, seed, x, z, lower_y, upper_y);
        }

        upper_y = lower_y;
        upper_density = lower_density;
        lower_y -= SURFACE_SEARCH_STEP;
    }

    macro_sample.base_elevation
}

/// Returns a shape-relative vertical bracket for locating the surface height.
fn surface_search_half_extent_m(shape: TerrainShapeParameters) -> f64 {
    (shape.detail_amplitude.abs() * 2.0).max(8.0)
}

pub(crate) fn refine_surface_height(
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

pub(crate) fn density_at_position(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    position: Vec3,
) -> DensitySample {
    let macro_sample = sample_macro_terrain(noise, preset, seed, position);

    density_at_position_with_macro(noise, preset, position, macro_sample)
}

pub(crate) fn density_at_position_with_macro(
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

pub(crate) fn sample_macro_terrain(
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
