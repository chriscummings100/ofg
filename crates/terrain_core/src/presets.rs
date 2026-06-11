// Built-in terrain preset defaults. These values are tuned in world meters:
// frequency is cycles per meter, so 0.001 means a roughly 1 km wavelength.

use crate::*;

pub(crate) type TerrainPresetDefinition = TerrainShapeParameters;

pub(crate) const SEED_LARGE_FEATURE_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.0016,
    lacunarity: 2.0,
    persistence: 0.52,
};
pub(crate) const SEED_DENSITY_DETAIL_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.012,
    lacunarity: 2.15,
    persistence: 0.46,
};
pub(crate) const TERRAIN_PRESETS: [TerrainPresetDefinition; 4] = [
    TerrainShapeParameters {
        base_height: 2.0,
        height_scale: 34.0,
        large_feature_noise: SEED_LARGE_FEATURE_NOISE,
        ridge_height_scale: 0.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 1,
            frequency: 0.002,
            lacunarity: 2.0,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 1.0,
        },
        warp: DomainWarpOptions {
            octaves: 1,
            frequency: 0.0015,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 0.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.0035 },
        cellular_height_scale: 0.0,
        detail_noise: SEED_DENSITY_DETAIL_NOISE,
        detail_amplitude: 3.0,
    },
    TerrainShapeParameters {
        base_height: 4.0,
        height_scale: 28.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00105,
            lacunarity: 2.0,
            persistence: 0.48,
        },
        ridge_height_scale: 1.2,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 2,
            frequency: 0.0022,
            lacunarity: 2.1,
            persistence: 0.42,
            ridge_offset: 1.0,
            ridge_sharpness: 1.35,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.001,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 110.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.003 },
        cellular_height_scale: 0.8,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.011,
            lacunarity: 2.05,
            persistence: 0.42,
        },
        detail_amplitude: 2.0,
    },
    TerrainShapeParameters {
        base_height: -4.0,
        height_scale: 40.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00055,
            lacunarity: 2.0,
            persistence: 0.5,
        },
        ridge_height_scale: 46.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.00105,
            lacunarity: 2.05,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 2.1,
        },
        warp: DomainWarpOptions {
            octaves: 3,
            frequency: 0.00065,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 220.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.0018 },
        cellular_height_scale: 6.0,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0075,
            lacunarity: 2.1,
            persistence: 0.42,
        },
        detail_amplitude: 5.5,
    },
    TerrainShapeParameters {
        base_height: 8.0,
        height_scale: 38.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00095,
            lacunarity: 2.2,
            persistence: 0.46,
        },
        ridge_height_scale: 34.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.0024,
            lacunarity: 2.2,
            persistence: 0.46,
            ridge_offset: 1.0,
            ridge_sharpness: 1.55,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.0013,
            lacunarity: 2.1,
            persistence: 0.52,
            amplitude: 180.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.006 },
        cellular_height_scale: 14.0,
        detail_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.018,
            lacunarity: 2.2,
            persistence: 0.48,
        },
        detail_amplitude: 8.5,
    },
];

pub(crate) fn terrain_preset_index(preset: u32) -> u32 {
    if (preset as usize) < TERRAIN_PRESETS.len() {
        preset
    } else {
        DEFAULT_TERRAIN_PRESET
    }
}

pub(crate) fn terrain_preset(preset: u32) -> TerrainPresetDefinition {
    TERRAIN_PRESETS[terrain_preset_index(preset) as usize]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wavelength_meters(frequency: f64) -> f64 {
        1.0 / frequency
    }

    #[test]
    fn terrain_preset_index_accepts_known_presets_and_defaults_unknown_values() {
        assert_eq!(terrain_preset_index(0), 0);
        assert_eq!(terrain_preset_index(1), 1);
        assert_eq!(terrain_preset_index(2), 2);
        assert_eq!(terrain_preset_index(3), 3);
        assert_eq!(terrain_preset_index(999), DEFAULT_TERRAIN_PRESET);
    }

    #[test]
    fn terrain_preset_returns_default_definition_for_unknown_values() {
        let default = terrain_preset(DEFAULT_TERRAIN_PRESET);
        let unknown = terrain_preset(999);

        assert_eq!(unknown.base_height, default.base_height);
        assert_eq!(unknown.height_scale, default.height_scale);
        assert_eq!(unknown.detail_amplitude, default.detail_amplitude);
        assert_eq!(unknown.warp.amplitude, default.warp.amplitude);
    }

    #[test]
    fn terrain_preset_wavelengths_are_landform_scaled() {
        let seed = terrain_preset(0);
        let rolling = terrain_preset(1);
        let mountain = terrain_preset(2);
        let highland = terrain_preset(3);

        assert!(wavelength_meters(seed.large_feature_noise.frequency) >= 500.0);
        assert!(wavelength_meters(seed.detail_noise.frequency) >= 60.0);

        assert!(wavelength_meters(rolling.large_feature_noise.frequency) >= 800.0);
        assert!(wavelength_meters(rolling.ridge_noise.frequency) >= 300.0);
        assert!(wavelength_meters(rolling.detail_noise.frequency) >= 60.0);

        assert!(wavelength_meters(mountain.large_feature_noise.frequency) >= 1_500.0);
        assert!(wavelength_meters(mountain.ridge_noise.frequency) >= 700.0);
        assert!(wavelength_meters(mountain.detail_noise.frequency) >= 100.0);

        assert!(wavelength_meters(highland.large_feature_noise.frequency) >= 800.0);
        assert!(wavelength_meters(highland.ridge_noise.frequency) >= 300.0);
        assert!(wavelength_meters(highland.cellular.frequency) >= 120.0);
        assert!(wavelength_meters(highland.detail_noise.frequency) >= 45.0);
    }
}
