// Renderer-facing sea water settings, status, and bathymetry metadata.
// This module deliberately keeps water math and debug contracts independent of
// WebGPU so command validation and status serialization stay testable outside
// the browser renderer.

pub const WATER_RUNTIME: &str = "rust-wgpu";
pub const WATER_BATHYMETRY_RUNTIME: &str = "rust-heightfield";
pub const SEA_LEVEL_METERS: f32 = terrain_core::SEA_LEVEL_METERS as f32;
pub const DEFAULT_WATER_BATHYMETRY_GRID_SIZE: u32 = terrain_core::WATER_NODE_BATHYMETRY_TEXEL_COUNT;
pub const DEFAULT_WATER_BATHYMETRY_WORLD_SPAN_METERS: f32 =
    terrain_core::TERRAIN_CHUNK_CELLS_PER_AXIS as f32;
pub const DEFAULT_WATER_OPEN_PATH_METERS: f32 = 96.0;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WaterDebugView {
    Final,
    BottomDepth,
    PathLength,
    Fresnel,
    Reflection,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WaterSettings {
    pub enabled: bool,
    pub reflection_enabled: bool,
    pub sea_level_meters: f32,
    pub shallow_depth_meters: f32,
    pub deep_depth_meters: f32,
    pub absorption_rgb: [f32; 3],
    pub shallow_color: [f32; 3],
    pub deep_color: [f32; 3],
    pub wave_scale: f32,
    pub wave_strength: f32,
    pub open_water_path_meters: f32,
    pub debug_view: WaterDebugView,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WaterSettingsUpdate {
    pub enabled: Option<bool>,
    pub reflection_enabled: Option<bool>,
    pub sea_level_meters: Option<f32>,
    pub shallow_depth_meters: Option<f32>,
    pub deep_depth_meters: Option<f32>,
    pub wave_scale: Option<f32>,
    pub wave_strength: Option<f32>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WaterStatus {
    pub runtime: &'static str,
    pub enabled: bool,
    pub reflection_enabled: bool,
    pub sea_level_meters: f32,
    pub bathymetry_runtime: &'static str,
    pub bathymetry_grid_size: u32,
    pub bathymetry_world_span_meters: f32,
    pub bathymetry_center_x: f32,
    pub bathymetry_center_z: f32,
    pub reflection_width: u32,
    pub reflection_height: u32,
    pub debug_view: WaterDebugView,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct WaterBathymetryCoverage {
    pub texel_count: u32,
    pub world_span_meters: f32,
    pub center_x: f32,
    pub center_z: f32,
    pub patch_count: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WaterSettingsError {
    InvalidSeaLevel,
    InvalidShallowDepth,
    InvalidDeepDepth,
    InvalidDepthOrder,
    InvalidWaveScale,
    InvalidWaveStrength,
    InvalidOpenWaterPath,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WaterBathymetryError {
    InvalidWaterPacket,
    AtlasFull,
}

impl WaterDebugView {
    /// Parses stable browser/debug view names.
    pub fn from_browser_name(name: &str) -> Option<Self> {
        match name {
            "final" => Some(Self::Final),
            "bottomDepth" => Some(Self::BottomDepth),
            "pathLength" => Some(Self::PathLength),
            "fresnel" => Some(Self::Fresnel),
            "reflection" => Some(Self::Reflection),
            _ => None,
        }
    }

    /// Returns the stable browser/debug name for this water view.
    pub fn browser_name(self) -> &'static str {
        match self {
            Self::Final => "final",
            Self::BottomDepth => "bottomDepth",
            Self::PathLength => "pathLength",
            Self::Fresnel => "fresnel",
            Self::Reflection => "reflection",
        }
    }

    /// Returns the WGSL debug view code consumed by the water shader.
    pub fn shader_code(self) -> f32 {
        match self {
            Self::Final => 0.0,
            Self::BottomDepth => 1.0,
            Self::PathLength => 2.0,
            Self::Fresnel => 3.0,
            Self::Reflection => 4.0,
        }
    }
}

impl WaterSettings {
    /// Returns production defaults for sea rendering.
    pub fn new() -> Self {
        Self {
            enabled: true,
            reflection_enabled: false,
            sea_level_meters: SEA_LEVEL_METERS,
            shallow_depth_meters: 1.25,
            deep_depth_meters: 18.0,
            absorption_rgb: [0.18, 0.075, 0.030],
            shallow_color: [0.10, 0.52, 0.62],
            deep_color: [0.008, 0.085, 0.22],
            wave_scale: 0.11,
            wave_strength: 0.34,
            open_water_path_meters: DEFAULT_WATER_OPEN_PATH_METERS,
            debug_view: WaterDebugView::Final,
        }
    }

    /// Applies a partial browser/debug update after validating the resulting settings.
    pub fn apply_update(self, update: WaterSettingsUpdate) -> Result<Self, WaterSettingsError> {
        let next = Self {
            enabled: update.enabled.unwrap_or(self.enabled),
            reflection_enabled: update.reflection_enabled.unwrap_or(self.reflection_enabled),
            sea_level_meters: update.sea_level_meters.unwrap_or(self.sea_level_meters),
            shallow_depth_meters: update
                .shallow_depth_meters
                .unwrap_or(self.shallow_depth_meters),
            deep_depth_meters: update.deep_depth_meters.unwrap_or(self.deep_depth_meters),
            absorption_rgb: self.absorption_rgb,
            shallow_color: self.shallow_color,
            deep_color: self.deep_color,
            wave_scale: update.wave_scale.unwrap_or(self.wave_scale),
            wave_strength: update.wave_strength.unwrap_or(self.wave_strength),
            open_water_path_meters: self.open_water_path_meters,
            debug_view: self.debug_view,
        };
        next.validate()?;
        Ok(next)
    }

    /// Sets the active water debug view.
    pub fn with_debug_view(mut self, debug_view: WaterDebugView) -> Self {
        self.debug_view = debug_view;
        self
    }

    /// Returns the status fields exposed through renderer debug snapshots.
    pub fn status(self) -> WaterStatus {
        WaterStatus {
            runtime: WATER_RUNTIME,
            enabled: self.enabled,
            reflection_enabled: self.reflection_enabled,
            sea_level_meters: self.sea_level_meters,
            bathymetry_runtime: WATER_BATHYMETRY_RUNTIME,
            bathymetry_grid_size: DEFAULT_WATER_BATHYMETRY_GRID_SIZE,
            bathymetry_world_span_meters: DEFAULT_WATER_BATHYMETRY_WORLD_SPAN_METERS,
            bathymetry_center_x: 0.0,
            bathymetry_center_z: 0.0,
            reflection_width: 0,
            reflection_height: 0,
            debug_view: self.debug_view,
        }
    }

    fn validate(&self) -> Result<(), WaterSettingsError> {
        if !(-512.0..=512.0).contains(&self.sea_level_meters) || !self.sea_level_meters.is_finite()
        {
            return Err(WaterSettingsError::InvalidSeaLevel);
        }
        if !(0.01..=64.0).contains(&self.shallow_depth_meters)
            || !self.shallow_depth_meters.is_finite()
        {
            return Err(WaterSettingsError::InvalidShallowDepth);
        }
        if !(0.01..=512.0).contains(&self.deep_depth_meters) || !self.deep_depth_meters.is_finite()
        {
            return Err(WaterSettingsError::InvalidDeepDepth);
        }
        if self.shallow_depth_meters >= self.deep_depth_meters {
            return Err(WaterSettingsError::InvalidDepthOrder);
        }
        if !(0.0001..=4.0).contains(&self.wave_scale) || !self.wave_scale.is_finite() {
            return Err(WaterSettingsError::InvalidWaveScale);
        }
        if !(0.0..=4.0).contains(&self.wave_strength) || !self.wave_strength.is_finite() {
            return Err(WaterSettingsError::InvalidWaveStrength);
        }
        if !(1.0..=2048.0).contains(&self.open_water_path_meters)
            || !self.open_water_path_meters.is_finite()
        {
            return Err(WaterSettingsError::InvalidOpenWaterPath);
        }
        Ok(())
    }
}

impl WaterStatus {
    /// Returns this status with current bathymetry coverage and reflection size.
    #[cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]
    pub(crate) fn with_runtime_resources(
        mut self,
        bathymetry_coverage: Option<WaterBathymetryCoverage>,
        reflection_width: u32,
        reflection_height: u32,
    ) -> Self {
        if let Some(coverage) = bathymetry_coverage {
            self.bathymetry_grid_size = coverage.texel_count;
            self.bathymetry_world_span_meters = coverage.world_span_meters;
            self.bathymetry_center_x = coverage.center_x;
            self.bathymetry_center_z = coverage.center_z;
        }
        self.reflection_width = reflection_width;
        self.reflection_height = reflection_height;
        self
    }
}

impl Default for WaterSettings {
    fn default() -> Self {
        Self::new()
    }
}

impl Default for WaterSettingsUpdate {
    fn default() -> Self {
        Self {
            enabled: None,
            reflection_enabled: None,
            sea_level_meters: None,
            shallow_depth_meters: None,
            deep_depth_meters: None,
            wave_scale: None,
            wave_strength: None,
        }
    }
}

impl std::fmt::Display for WaterSettingsError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidSeaLevel => "water sea level is invalid",
            Self::InvalidShallowDepth => "water shallow depth is invalid",
            Self::InvalidDeepDepth => "water deep depth is invalid",
            Self::InvalidDepthOrder => "water shallow depth must be less than deep depth",
            Self::InvalidWaveScale => "water wave scale is invalid",
            Self::InvalidWaveStrength => "water wave strength is invalid",
            Self::InvalidOpenWaterPath => "water open-water path length is invalid",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for WaterSettingsError {}

impl std::fmt::Display for WaterBathymetryError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidWaterPacket => formatter.write_str("water bathymetry packet is invalid"),
            Self::AtlasFull => formatter.write_str("water bathymetry atlas is full"),
        }
    }
}

impl std::error::Error for WaterBathymetryError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn water_debug_view_round_trips_browser_names() {
        let cases = [
            ("final", WaterDebugView::Final),
            ("bottomDepth", WaterDebugView::BottomDepth),
            ("pathLength", WaterDebugView::PathLength),
            ("fresnel", WaterDebugView::Fresnel),
            ("reflection", WaterDebugView::Reflection),
        ];

        for (name, view) in cases {
            assert_eq!(WaterDebugView::from_browser_name(name), Some(view));
            assert_eq!(view.browser_name(), name);
            assert!(view.shader_code() >= 0.0);
        }
        assert_eq!(WaterDebugView::from_browser_name("unknown"), None);
    }

    #[test]
    fn water_settings_defaults_expose_status() {
        let settings = WaterSettings::default();
        let status = settings.status();

        assert!(settings.enabled);
        assert!(!settings.reflection_enabled);
        assert_eq!(settings.sea_level_meters, SEA_LEVEL_METERS);
        assert_eq!(settings.shallow_depth_meters, 1.25);
        assert_eq!(settings.deep_depth_meters, 18.0);
        assert_eq!(settings.absorption_rgb, [0.18, 0.075, 0.030]);
        assert_eq!(settings.wave_scale, 0.11);
        assert_eq!(settings.wave_strength, 0.34);
        assert_eq!(status.runtime, WATER_RUNTIME);
        assert_eq!(status.bathymetry_runtime, WATER_BATHYMETRY_RUNTIME);
        assert_eq!(
            status.bathymetry_grid_size,
            DEFAULT_WATER_BATHYMETRY_GRID_SIZE
        );
        assert_eq!(
            status.bathymetry_world_span_meters,
            DEFAULT_WATER_BATHYMETRY_WORLD_SPAN_METERS
        );
        assert_eq!(status.reflection_width, 0);
        assert_eq!(status.reflection_height, 0);
        assert_eq!(status.debug_view, WaterDebugView::Final);
    }

    #[test]
    fn water_settings_apply_valid_partial_updates() {
        let settings = WaterSettings::default()
            .apply_update(WaterSettingsUpdate {
                enabled: Some(false),
                reflection_enabled: Some(true),
                sea_level_meters: Some(1.25),
                shallow_depth_meters: Some(3.0),
                deep_depth_meters: Some(48.0),
                wave_scale: Some(0.2),
                wave_strength: Some(0.35),
            })
            .unwrap()
            .with_debug_view(WaterDebugView::PathLength);

        assert!(!settings.enabled);
        assert!(settings.reflection_enabled);
        assert_eq!(settings.sea_level_meters, 1.25);
        assert_eq!(settings.shallow_depth_meters, 3.0);
        assert_eq!(settings.deep_depth_meters, 48.0);
        assert_eq!(settings.wave_scale, 0.2);
        assert_eq!(settings.wave_strength, 0.35);
        assert_eq!(
            settings.open_water_path_meters,
            DEFAULT_WATER_OPEN_PATH_METERS
        );
        assert_eq!(settings.debug_view, WaterDebugView::PathLength);
    }

    #[test]
    fn water_settings_reject_invalid_ranges() {
        let defaults = WaterSettings::default();

        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                sea_level_meters: Some(f32::NAN),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidSeaLevel)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                sea_level_meters: Some(513.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidSeaLevel)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                shallow_depth_meters: Some(f32::NAN),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidShallowDepth)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                shallow_depth_meters: Some(0.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidShallowDepth)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                shallow_depth_meters: Some(25.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidDepthOrder)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                deep_depth_meters: Some(f32::NAN),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidDeepDepth)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                deep_depth_meters: Some(0.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidDeepDepth)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                wave_scale: Some(0.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidWaveScale)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                wave_scale: Some(f32::NAN),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidWaveScale)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                wave_strength: Some(5.0),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidWaveStrength)
        );
        assert_eq!(
            defaults.apply_update(WaterSettingsUpdate {
                wave_strength: Some(f32::NAN),
                ..WaterSettingsUpdate::default()
            }),
            Err(WaterSettingsError::InvalidWaveStrength)
        );
    }

    #[test]
    fn water_status_applies_runtime_resource_metadata() {
        let status = WaterSettings::default().status().with_runtime_resources(
            Some(WaterBathymetryCoverage {
                texel_count: 32,
                world_span_meters: 128.0,
                center_x: 12.5,
                center_z: -9.25,
                patch_count: 7,
            }),
            640,
            360,
        );

        assert_eq!(status.bathymetry_grid_size, 32);
        assert_eq!(status.bathymetry_world_span_meters, 128.0);
        assert_eq!(status.bathymetry_center_x, 12.5);
        assert_eq!(status.bathymetry_center_z, -9.25);
        assert_eq!(status.reflection_width, 640);
        assert_eq!(status.reflection_height, 360);
    }

    #[test]
    fn water_status_keeps_default_coverage_without_runtime_bathymetry() {
        let status = WaterSettings::default()
            .status()
            .with_runtime_resources(None, 320, 180);

        assert_eq!(
            status.bathymetry_grid_size,
            DEFAULT_WATER_BATHYMETRY_GRID_SIZE
        );
        assert_eq!(
            status.bathymetry_world_span_meters,
            DEFAULT_WATER_BATHYMETRY_WORLD_SPAN_METERS
        );
        assert_eq!(status.bathymetry_center_x, 0.0);
        assert_eq!(status.bathymetry_center_z, 0.0);
        assert_eq!(status.reflection_width, 320);
        assert_eq!(status.reflection_height, 180);
    }

    #[test]
    fn water_settings_error_messages_are_stable() {
        let cases = [
            (
                WaterSettingsError::InvalidSeaLevel,
                "water sea level is invalid",
            ),
            (
                WaterSettingsError::InvalidShallowDepth,
                "water shallow depth is invalid",
            ),
            (
                WaterSettingsError::InvalidDeepDepth,
                "water deep depth is invalid",
            ),
            (
                WaterSettingsError::InvalidDepthOrder,
                "water shallow depth must be less than deep depth",
            ),
            (
                WaterSettingsError::InvalidWaveScale,
                "water wave scale is invalid",
            ),
            (
                WaterSettingsError::InvalidWaveStrength,
                "water wave strength is invalid",
            ),
            (
                WaterSettingsError::InvalidOpenWaterPath,
                "water open-water path length is invalid",
            ),
        ];

        for (error, message) in cases {
            assert_eq!(error.to_string(), message);
        }
    }

    #[test]
    fn water_bathymetry_error_messages_are_stable() {
        assert_eq!(
            WaterBathymetryError::InvalidWaterPacket.to_string(),
            "water bathymetry packet is invalid"
        );
        assert_eq!(
            WaterBathymetryError::AtlasFull.to_string(),
            "water bathymetry atlas is full"
        );
    }
}
