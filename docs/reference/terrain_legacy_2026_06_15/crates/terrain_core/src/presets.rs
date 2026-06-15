// Built-in terrain preset defaults. These values are tuned in world meters:
// frequency is cycles per meter, so 0.001 means a roughly 1 km wavelength.

use crate::*;

pub(crate) type TerrainPresetDefinition = TerrainShapeParameters;

pub(crate) const LOWLAND_LARGE_FEATURE_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.00014,
    lacunarity: 2.0,
    persistence: 0.45,
};
pub(crate) const LOWLAND_DENSITY_DETAIL_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.006,
    lacunarity: 2.1,
    persistence: 0.42,
};
pub(crate) const TERRAIN_PRESETS: [TerrainPresetDefinition; 4] = [
    TerrainShapeParameters {
        base_height: 4.0,
        height_scale: 16.0,
        large_feature_noise: LOWLAND_LARGE_FEATURE_NOISE,
        ridge_height_scale: 0.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 1,
            frequency: 0.00035,
            lacunarity: 2.0,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 1.0,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.00012,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 60.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.0005 },
        cellular_height_scale: 0.0,
        detail_noise: LOWLAND_DENSITY_DETAIL_NOISE,
        detail_amplitude: 1.5,
    },
    TerrainShapeParameters {
        base_height: 12.0,
        height_scale: 42.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00025,
            lacunarity: 2.0,
            persistence: 0.48,
        },
        ridge_height_scale: 8.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 2,
            frequency: 0.0007,
            lacunarity: 2.0,
            persistence: 0.42,
            ridge_offset: 1.0,
            ridge_sharpness: 1.2,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.00022,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 140.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.0008 },
        cellular_height_scale: 2.0,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0065,
            lacunarity: 2.05,
            persistence: 0.42,
        },
        detail_amplitude: 3.0,
    },
    TerrainShapeParameters {
        base_height: 24.0,
        height_scale: 110.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00011,
            lacunarity: 2.0,
            persistence: 0.5,
        },
        ridge_height_scale: 105.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.00032,
            lacunarity: 2.05,
            persistence: 0.5,
            ridge_offset: 1.0,
            ridge_sharpness: 2.2,
        },
        warp: DomainWarpOptions {
            octaves: 3,
            frequency: 0.00016,
            lacunarity: 2.0,
            persistence: 0.5,
            amplitude: 240.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.00055 },
        cellular_height_scale: 14.0,
        detail_noise: FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0045,
            lacunarity: 2.1,
            persistence: 0.42,
        },
        detail_amplitude: 6.0,
    },
    TerrainShapeParameters {
        base_height: 18.0,
        height_scale: 78.0,
        large_feature_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.00017,
            lacunarity: 2.1,
            persistence: 0.46,
        },
        ridge_height_scale: 72.0,
        ridge_noise: RidgedFractalNoiseOptions {
            octaves: 4,
            frequency: 0.00062,
            lacunarity: 2.15,
            persistence: 0.46,
            ridge_offset: 1.0,
            ridge_sharpness: 1.75,
        },
        warp: DomainWarpOptions {
            octaves: 2,
            frequency: 0.00028,
            lacunarity: 2.1,
            persistence: 0.52,
            amplitude: 190.0,
        },
        cellular: CellularNoiseOptions { frequency: 0.0011 },
        cellular_height_scale: 22.0,
        detail_noise: FractalNoiseOptions {
            octaves: 4,
            frequency: 0.0085,
            lacunarity: 2.2,
            persistence: 0.48,
        },
        detail_amplitude: 8.0,
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

        assert!(wavelength_meters(seed.large_feature_noise.frequency) >= 7_000.0);
        assert!(wavelength_meters(seed.detail_noise.frequency) >= 150.0);

        assert!(wavelength_meters(rolling.large_feature_noise.frequency) >= 4_000.0);
        assert!(wavelength_meters(rolling.ridge_noise.frequency) >= 1_400.0);
        assert!(wavelength_meters(rolling.detail_noise.frequency) >= 150.0);

        assert!(wavelength_meters(mountain.large_feature_noise.frequency) >= 9_000.0);
        assert!(wavelength_meters(mountain.ridge_noise.frequency) >= 3_000.0);
        assert!(wavelength_meters(mountain.detail_noise.frequency) >= 200.0);

        assert!(wavelength_meters(highland.large_feature_noise.frequency) >= 5_800.0);
        assert!(wavelength_meters(highland.ridge_noise.frequency) >= 1_600.0);
        assert!(wavelength_meters(highland.cellular.frequency) >= 900.0);
        assert!(wavelength_meters(highland.detail_noise.frequency) >= 100.0);
    }
}
