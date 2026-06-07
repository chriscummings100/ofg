use crate::*;

#[derive(Clone, Copy)]
pub(crate) struct TerrainPresetDefinition {
    pub(crate) base_height: f64,
    pub(crate) height_scale: f64,
    pub(crate) large_feature_noise: FractalNoiseOptions,
    pub(crate) ridge_height_scale: f64,
    pub(crate) ridge_noise: RidgedFractalNoiseOptions,
    pub(crate) warp: DomainWarpOptions,
    pub(crate) cellular: CellularNoiseOptions,
    pub(crate) cellular_height_scale: f64,
    pub(crate) detail_noise: FractalNoiseOptions,
    pub(crate) detail_amplitude: f64,
}

pub(crate) const SEED_LARGE_FEATURE_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.0065,
    lacunarity: 2.0,
    persistence: 0.52,
};
pub(crate) const SEED_DENSITY_DETAIL_NOISE: FractalNoiseOptions = FractalNoiseOptions {
    octaves: 3,
    frequency: 0.035,
    lacunarity: 2.15,
    persistence: 0.46,
};
pub(crate) const TERRAIN_PRESETS: [TerrainPresetDefinition; 4] = [
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
}
