// Performance diagnostics shared by the Rust browser game facade and renderer.
// The types here are intentionally small data containers so frame timing,
// render counters, and debug render options stay testable outside WebGPU.

use crate::config::SHADOW_CASCADE_COUNT;
use crate::shadows::ShadowSunMode;

pub const PERF_HISTORY_CAPACITY: usize = 600;
pub const MAX_TRACKED_TERRAIN_LODS: usize = 8;
pub const DEFAULT_TERRAIN_LOD_MASK: u32 = 0xFFFF_FFFF;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct NumericSummary {
    pub latest: f64,
    pub min: f64,
    pub max: f64,
    pub average: f64,
    pub p95: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct RustCpuFrameTimings {
    pub total_frame_ms: f64,
    pub input_parse_ms: f64,
    pub game_state_tick_ms: f64,
    pub player_character_update_ms: f64,
    pub terrain_completion_ingest_ms: f64,
    pub terrain_stream_update_ms: f64,
    pub terrain_stream_tick_ms: f64,
    pub terrain_stream_sync_ms: f64,
    pub terrain_stream_scheduler_ms: f64,
    pub terrain_stream_worker_queue_ms: f64,
    pub terrain_stream_visibility_ms: f64,
    pub terrain_stream_visibility_select_ms: f64,
    pub terrain_stream_visibility_status_ms: f64,
    pub terrain_stream_visibility_apply_ms: f64,
    pub terrain_mesh_destroy_ms: f64,
    pub terrain_mesh_upload_ms: f64,
    pub render_frame_ms: f64,
    pub render_packet_build_ms: f64,
    pub renderer_prepare_ms: f64,
    pub renderer_shadow_cpu_ms: f64,
    pub renderer_scene_cpu_ms: f64,
    pub renderer_post_cpu_ms: f64,
    pub renderer_submit_ms: f64,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RustCpuFrameSummary {
    pub total_frame_ms: NumericSummary,
    pub input_parse_ms: NumericSummary,
    pub game_state_tick_ms: NumericSummary,
    pub player_character_update_ms: NumericSummary,
    pub terrain_completion_ingest_ms: NumericSummary,
    pub terrain_stream_update_ms: NumericSummary,
    pub terrain_stream_tick_ms: NumericSummary,
    pub terrain_stream_sync_ms: NumericSummary,
    pub terrain_stream_scheduler_ms: NumericSummary,
    pub terrain_stream_worker_queue_ms: NumericSummary,
    pub terrain_stream_visibility_ms: NumericSummary,
    pub terrain_stream_visibility_select_ms: NumericSummary,
    pub terrain_stream_visibility_status_ms: NumericSummary,
    pub terrain_stream_visibility_apply_ms: NumericSummary,
    pub terrain_mesh_destroy_ms: NumericSummary,
    pub terrain_mesh_upload_ms: NumericSummary,
    pub render_frame_ms: NumericSummary,
    pub render_packet_build_ms: NumericSummary,
    pub renderer_prepare_ms: NumericSummary,
    pub renderer_shadow_cpu_ms: NumericSummary,
    pub renderer_scene_cpu_ms: NumericSummary,
    pub renderer_post_cpu_ms: NumericSummary,
    pub renderer_submit_ms: NumericSummary,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct TerrainLodCounter {
    pub lod: u32,
    pub draw_count: u64,
    pub vertex_count: u64,
    pub index_count: u64,
    pub triangle_count: u64,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ShadowCascadeCounter {
    pub cascade_index: u32,
    pub enabled: bool,
    pub candidate_count: u64,
    pub visible_count: u64,
    pub culled_count: u64,
    pub draw_count: u64,
    pub vertex_count: u64,
    pub index_count: u64,
    pub triangle_count: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RenderCounterSample {
    pub frame_candidate_count: u64,
    pub frame_visible_draw_count: u64,
    pub frame_culled_count: u64,
    pub frame_shadow_draw_count: u64,
    pub terrain_draw_count: u64,
    pub model_draw_count: u64,
    pub sky_draw_count: u64,
    pub post_process_draw_count: u64,
    pub submitted_vertex_count: u64,
    pub submitted_index_count: u64,
    pub submitted_triangle_count: u64,
    pub terrain_lod_counters: Vec<TerrainLodCounter>,
    pub shadow_cascade_counters: Vec<ShadowCascadeCounter>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RenderCounterSummary {
    pub frame_candidate_count: NumericSummary,
    pub frame_visible_draw_count: NumericSummary,
    pub frame_culled_count: NumericSummary,
    pub frame_shadow_draw_count: NumericSummary,
    pub terrain_draw_count: NumericSummary,
    pub model_draw_count: NumericSummary,
    pub sky_draw_count: NumericSummary,
    pub post_process_draw_count: NumericSummary,
    pub submitted_vertex_count: NumericSummary,
    pub submitted_index_count: NumericSummary,
    pub submitted_triangle_count: NumericSummary,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GpuTimerStatus {
    pub available: bool,
    pub unavailable_reason: &'static str,
    pub timestamp_period_ns: f64,
    pub pending_readback_count: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct GpuPassTimings {
    pub shadow_cascade_ms: [Option<f64>; SHADOW_CASCADE_COUNT],
    pub scene_ms: Option<f64>,
    pub bloom_ms: Option<f64>,
    pub post_process_ms: Option<f64>,
    pub total_measured_ms: Option<f64>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct GpuPassTimingSummary {
    pub shadow_cascade_ms: [NumericSummary; SHADOW_CASCADE_COUNT],
    pub scene_ms: NumericSummary,
    pub bloom_ms: NumericSummary,
    pub post_process_ms: NumericSummary,
    pub total_measured_ms: NumericSummary,
}

#[derive(Clone, Debug, PartialEq)]
pub struct FramePerfSample {
    pub frame_index: u32,
    pub rust_cpu: RustCpuFrameTimings,
    pub renderer_counters: RenderCounterSample,
    pub gpu_pass_timings: GpuPassTimings,
}

#[derive(Clone, Debug, PartialEq)]
pub struct FramePerfReport {
    pub sample_count: usize,
    pub capacity: usize,
    pub latest: Option<FramePerfSample>,
    pub rust_cpu: RustCpuFrameSummary,
    pub renderer_counters: RenderCounterSummary,
    pub gpu: GpuPassTimingSummary,
    pub terrain_lod_counters: Vec<TerrainLodCounter>,
    pub shadow_cascade_counters: Vec<ShadowCascadeCounter>,
}

#[derive(Clone, Debug)]
pub struct FramePerfRing {
    capacity: usize,
    samples: Vec<FramePerfSample>,
    next_index: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RenderMaterialDebugMode {
    Full,
    Lambert,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RenderDebugOptions {
    pub terrain_lod_mask: u32,
    pub sky_enabled: bool,
    pub sky_cloud_noise_enabled: bool,
    pub shadow_pass_enabled: bool,
    pub shadow_cascade_mask: u32,
    pub shadow_sampling_enabled: bool,
    pub shadow_sun_mode: ShadowSunMode,
    pub white_textures_enabled: bool,
    pub material_mode: RenderMaterialDebugMode,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct RenderDebugOptionsUpdate {
    pub terrain_lod_mask: Option<u32>,
    pub sky_enabled: Option<bool>,
    pub sky_cloud_noise_enabled: Option<bool>,
    pub shadow_pass_enabled: Option<bool>,
    pub shadow_cascade_mask: Option<u32>,
    pub shadow_sampling_enabled: Option<bool>,
    pub shadow_sun_mode: Option<ShadowSunMode>,
    pub white_textures_enabled: Option<bool>,
    pub material_mode: Option<RenderMaterialDebugMode>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RenderDebugOptionsError {
    EmptyTerrainLodMask,
    EmptyShadowCascadeMask,
    UnsupportedShadowCascadeMask,
}

impl Default for FramePerfRing {
    fn default() -> Self {
        Self::new(PERF_HISTORY_CAPACITY)
    }
}

impl Default for RenderCounterSample {
    fn default() -> Self {
        Self {
            frame_candidate_count: 0,
            frame_visible_draw_count: 0,
            frame_culled_count: 0,
            frame_shadow_draw_count: 0,
            terrain_draw_count: 0,
            model_draw_count: 0,
            sky_draw_count: 0,
            post_process_draw_count: 0,
            submitted_vertex_count: 0,
            submitted_index_count: 0,
            submitted_triangle_count: 0,
            terrain_lod_counters: Vec::new(),
            shadow_cascade_counters: (0..SHADOW_CASCADE_COUNT)
                .map(|index| ShadowCascadeCounter {
                    cascade_index: index as u32,
                    ..ShadowCascadeCounter::default()
                })
                .collect(),
        }
    }
}

impl Default for GpuTimerStatus {
    fn default() -> Self {
        Self {
            available: false,
            unavailable_reason: "timestamp queries unavailable",
            timestamp_period_ns: 0.0,
            pending_readback_count: 0,
        }
    }
}

impl Default for RenderMaterialDebugMode {
    fn default() -> Self {
        Self::Full
    }
}

impl Default for RenderDebugOptions {
    fn default() -> Self {
        Self {
            terrain_lod_mask: DEFAULT_TERRAIN_LOD_MASK,
            sky_enabled: true,
            sky_cloud_noise_enabled: true,
            shadow_pass_enabled: true,
            shadow_cascade_mask: default_shadow_cascade_mask(),
            shadow_sampling_enabled: true,
            shadow_sun_mode: ShadowSunMode::Production,
            white_textures_enabled: false,
            material_mode: RenderMaterialDebugMode::Full,
        }
    }
}

impl std::fmt::Display for RenderDebugOptionsError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::EmptyTerrainLodMask => "empty terrain LOD debug mask",
            Self::EmptyShadowCascadeMask => "empty shadow cascade debug mask",
            Self::UnsupportedShadowCascadeMask => "unsupported shadow cascade debug mask",
        };
        formatter.write_str(message)
    }
}

impl FramePerfRing {
    /// Creates an empty fixed-capacity frame history.
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity: capacity.max(1),
            samples: Vec::new(),
            next_index: 0,
        }
    }

    /// Removes all retained frame samples without changing the capacity.
    pub fn clear(&mut self) {
        self.samples.clear();
        self.next_index = 0;
    }

    /// Adds a frame sample, replacing the oldest sample after the ring fills.
    pub fn push(&mut self, sample: FramePerfSample) {
        if self.samples.len() < self.capacity {
            self.samples.push(sample);
            self.next_index = self.samples.len() % self.capacity;
            return;
        }

        self.samples[self.next_index] = sample;
        self.next_index = (self.next_index + 1) % self.capacity;
    }

    /// Returns the number of retained frame samples.
    pub fn len(&self) -> usize {
        self.samples.len()
    }

    /// Returns true when no frame samples have been retained.
    pub fn is_empty(&self) -> bool {
        self.samples.is_empty()
    }

    /// Returns samples from oldest to newest.
    pub fn samples(&self) -> Vec<&FramePerfSample> {
        if self.samples.len() < self.capacity {
            return self.samples.iter().collect();
        }

        self.samples[self.next_index..]
            .iter()
            .chain(self.samples[..self.next_index].iter())
            .collect()
    }

    /// Builds a summary report over the retained frame samples.
    pub fn report(&self) -> FramePerfReport {
        let samples = self.samples();
        let latest = samples.last().map(|sample| (*sample).clone());
        let terrain_lod_counters = latest
            .as_ref()
            .map(|sample| sample.renderer_counters.terrain_lod_counters.clone())
            .unwrap_or_default();
        let shadow_cascade_counters = latest
            .as_ref()
            .map(|sample| sample.renderer_counters.shadow_cascade_counters.clone())
            .unwrap_or_else(default_shadow_cascade_counters);

        FramePerfReport {
            sample_count: samples.len(),
            capacity: self.capacity,
            latest,
            rust_cpu: summarize_rust_cpu(&samples),
            renderer_counters: summarize_render_counters(&samples),
            gpu: summarize_gpu_pass_timings(&samples),
            terrain_lod_counters,
            shadow_cascade_counters,
        }
    }
}

impl RenderCounterSample {
    /// Records one submitted scene draw and updates aggregate counters.
    pub fn record_scene_draw(
        &mut self,
        vertex_count: u64,
        index_count: u64,
        terrain_lod: Option<u8>,
    ) {
        self.frame_visible_draw_count = self.frame_visible_draw_count.saturating_add(1);
        self.submitted_vertex_count = self.submitted_vertex_count.saturating_add(vertex_count);
        self.submitted_index_count = self.submitted_index_count.saturating_add(index_count);
        self.submitted_triangle_count = self
            .submitted_triangle_count
            .saturating_add(index_count / 3);

        if let Some(lod) = terrain_lod {
            self.terrain_draw_count = self.terrain_draw_count.saturating_add(1);
            self.record_terrain_lod_draw(lod, vertex_count, index_count);
        } else {
            self.model_draw_count = self.model_draw_count.saturating_add(1);
        }
    }

    /// Records one submitted shadow draw for a cascade.
    pub fn record_shadow_draw(
        &mut self,
        cascade_index: usize,
        vertex_count: u64,
        index_count: u64,
    ) {
        self.frame_shadow_draw_count = self.frame_shadow_draw_count.saturating_add(1);
        if let Some(cascade) = self.shadow_cascade_counters.get_mut(cascade_index) {
            cascade.draw_count = cascade.draw_count.saturating_add(1);
            cascade.visible_count = cascade.visible_count.saturating_add(1);
            cascade.vertex_count = cascade.vertex_count.saturating_add(vertex_count);
            cascade.index_count = cascade.index_count.saturating_add(index_count);
            cascade.triangle_count = cascade.triangle_count.saturating_add(index_count / 3);
        }
    }

    /// Marks one candidate as culled before shadow submission.
    pub fn record_shadow_cull(&mut self, cascade_index: usize) {
        if let Some(cascade) = self.shadow_cascade_counters.get_mut(cascade_index) {
            cascade.culled_count = cascade.culled_count.saturating_add(1);
        }
    }

    /// Records a submitted sky full-screen draw.
    pub fn record_sky_draw(&mut self) {
        self.sky_draw_count = self.sky_draw_count.saturating_add(1);
    }

    /// Records a submitted post-process full-screen draw.
    pub fn record_post_process_draw(&mut self) {
        self.post_process_draw_count = self.post_process_draw_count.saturating_add(1);
    }

    /// Marks the number of render items considered for main-camera drawing.
    pub fn set_main_camera_candidates(&mut self, candidate_count: u64) {
        self.frame_candidate_count = candidate_count;
    }

    /// Marks one main-camera item as culled.
    pub fn record_main_camera_cull(&mut self) {
        self.frame_culled_count = self.frame_culled_count.saturating_add(1);
    }

    /// Marks the number of render items considered by one shadow cascade.
    pub fn set_shadow_cascade_candidates(
        &mut self,
        cascade_index: usize,
        enabled: bool,
        candidate_count: u64,
    ) {
        if let Some(cascade) = self.shadow_cascade_counters.get_mut(cascade_index) {
            cascade.enabled = enabled;
            cascade.candidate_count = candidate_count;
            if !enabled {
                cascade.culled_count = candidate_count;
            }
        }
    }

    fn record_terrain_lod_draw(&mut self, lod: u8, vertex_count: u64, index_count: u64) {
        let lod = u32::from(lod);
        if let Some(counter) = self
            .terrain_lod_counters
            .iter_mut()
            .find(|counter| counter.lod == lod)
        {
            counter.draw_count = counter.draw_count.saturating_add(1);
            counter.vertex_count = counter.vertex_count.saturating_add(vertex_count);
            counter.index_count = counter.index_count.saturating_add(index_count);
            counter.triangle_count = counter.triangle_count.saturating_add(index_count / 3);
            return;
        }

        if self.terrain_lod_counters.len() < MAX_TRACKED_TERRAIN_LODS {
            self.terrain_lod_counters.push(TerrainLodCounter {
                lod,
                draw_count: 1,
                vertex_count,
                index_count,
                triangle_count: index_count / 3,
            });
            self.terrain_lod_counters
                .sort_by(|left, right| left.lod.cmp(&right.lod));
        }
    }
}

impl GpuPassTimings {
    /// Builds GPU timings from timestamp query values and pass index pairs.
    pub fn from_timestamp_pairs(
        timestamp_period_ns: f64,
        timestamps: &[u64],
        pairs: &[GpuTimestampPair],
    ) -> Self {
        let mut timings = Self::default();
        let mut total_ms = 0.0;
        let mut measured_count = 0_u32;
        for pair in pairs {
            let Some(start) = timestamps.get(pair.start_index as usize) else {
                continue;
            };
            let Some(end) = timestamps.get(pair.end_index as usize) else {
                continue;
            };
            if end < start {
                continue;
            }
            let elapsed_ms = (*end - *start) as f64 * timestamp_period_ns / 1_000_000.0;
            if !elapsed_ms.is_finite() {
                continue;
            }
            match pair.pass {
                GpuTimedPass::ShadowCascade(index) if index < SHADOW_CASCADE_COUNT => {
                    timings.shadow_cascade_ms[index] = Some(elapsed_ms)
                }
                GpuTimedPass::Scene => timings.scene_ms = Some(elapsed_ms),
                GpuTimedPass::Bloom => timings.bloom_ms = Some(elapsed_ms),
                GpuTimedPass::PostProcess => timings.post_process_ms = Some(elapsed_ms),
                GpuTimedPass::ShadowCascade(_) => {}
            }
            total_ms += elapsed_ms;
            measured_count = measured_count.saturating_add(1);
        }
        if measured_count > 0 {
            timings.total_measured_ms = Some(total_ms);
        }

        timings
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GpuTimedPass {
    ShadowCascade(usize),
    Scene,
    Bloom,
    PostProcess,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GpuTimestampPair {
    pub pass: GpuTimedPass,
    pub start_index: u32,
    pub end_index: u32,
}

impl RenderDebugOptions {
    /// Applies a validated partial update to these render debug options.
    pub fn apply_update(
        mut self,
        update: RenderDebugOptionsUpdate,
    ) -> Result<Self, RenderDebugOptionsError> {
        if let Some(mask) = update.terrain_lod_mask {
            if mask == 0 {
                return Err(RenderDebugOptionsError::EmptyTerrainLodMask);
            }
            self.terrain_lod_mask = mask;
        }
        if let Some(enabled) = update.sky_enabled {
            self.sky_enabled = enabled;
        }
        if let Some(enabled) = update.sky_cloud_noise_enabled {
            self.sky_cloud_noise_enabled = enabled;
        }
        if let Some(enabled) = update.shadow_pass_enabled {
            self.shadow_pass_enabled = enabled;
        }
        if let Some(mask) = update.shadow_cascade_mask {
            if mask == 0 {
                return Err(RenderDebugOptionsError::EmptyShadowCascadeMask);
            }
            if mask & !default_shadow_cascade_mask() != 0 {
                return Err(RenderDebugOptionsError::UnsupportedShadowCascadeMask);
            }
            self.shadow_cascade_mask = mask;
        }
        if let Some(enabled) = update.shadow_sampling_enabled {
            self.shadow_sampling_enabled = enabled;
        }
        if let Some(mode) = update.shadow_sun_mode {
            self.shadow_sun_mode = mode;
        }
        if let Some(enabled) = update.white_textures_enabled {
            self.white_textures_enabled = enabled;
        }
        if let Some(mode) = update.material_mode {
            self.material_mode = mode;
        }

        Ok(self)
    }

    /// Returns true when a terrain LOD should be sent to the renderer.
    pub fn terrain_lod_enabled(self, lod: u8) -> bool {
        let lod = u32::from(lod);
        lod < u32::BITS && (self.terrain_lod_mask & (1_u32 << lod)) != 0
    }

    /// Returns true when a shadow cascade should render.
    pub fn shadow_cascade_enabled(self, cascade_index: usize) -> bool {
        cascade_index < SHADOW_CASCADE_COUNT
            && (self.shadow_cascade_mask & (1_u32 << cascade_index)) != 0
    }

    /// Returns true when the main scene shader should sample shadow maps.
    pub fn effective_shadow_sampling_enabled(self) -> bool {
        self.shadow_pass_enabled && self.shadow_sampling_enabled
    }

    /// Returns the WGSL debug material mode code.
    pub fn material_mode_code(self) -> f32 {
        match self.material_mode {
            RenderMaterialDebugMode::Full => 0.0,
            RenderMaterialDebugMode::Lambert => 1.0,
        }
    }
}

/// Returns the default shadow cascade bit mask for all configured cascades.
pub fn default_shadow_cascade_mask() -> u32 {
    if SHADOW_CASCADE_COUNT >= u32::BITS as usize {
        u32::MAX
    } else {
        (1_u32 << SHADOW_CASCADE_COUNT) - 1
    }
}

/// Parses a stable terrain node key such as `lod2:-1,0,4`.
pub fn terrain_lod_from_node_key(node_key: &str) -> Option<u8> {
    let rest = node_key.strip_prefix("lod")?;
    let (lod, _coord) = rest.split_once(':')?;
    lod.parse::<u8>().ok()
}

fn summarize_rust_cpu(samples: &[&FramePerfSample]) -> RustCpuFrameSummary {
    RustCpuFrameSummary {
        total_frame_ms: summarize_numeric(
            samples.iter().map(|sample| sample.rust_cpu.total_frame_ms),
        ),
        input_parse_ms: summarize_numeric(
            samples.iter().map(|sample| sample.rust_cpu.input_parse_ms),
        ),
        game_state_tick_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.game_state_tick_ms),
        ),
        player_character_update_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.player_character_update_ms),
        ),
        terrain_completion_ingest_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_completion_ingest_ms),
        ),
        terrain_stream_update_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_update_ms),
        ),
        terrain_stream_tick_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_tick_ms),
        ),
        terrain_stream_sync_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_sync_ms),
        ),
        terrain_stream_scheduler_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_scheduler_ms),
        ),
        terrain_stream_worker_queue_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_worker_queue_ms),
        ),
        terrain_stream_visibility_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_visibility_ms),
        ),
        terrain_stream_visibility_select_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_visibility_select_ms),
        ),
        terrain_stream_visibility_status_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_visibility_status_ms),
        ),
        terrain_stream_visibility_apply_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_stream_visibility_apply_ms),
        ),
        terrain_mesh_destroy_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_mesh_destroy_ms),
        ),
        terrain_mesh_upload_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.terrain_mesh_upload_ms),
        ),
        render_frame_ms: summarize_numeric(
            samples.iter().map(|sample| sample.rust_cpu.render_frame_ms),
        ),
        render_packet_build_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.render_packet_build_ms),
        ),
        renderer_prepare_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.renderer_prepare_ms),
        ),
        renderer_shadow_cpu_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.renderer_shadow_cpu_ms),
        ),
        renderer_scene_cpu_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.renderer_scene_cpu_ms),
        ),
        renderer_post_cpu_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.renderer_post_cpu_ms),
        ),
        renderer_submit_ms: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.rust_cpu.renderer_submit_ms),
        ),
    }
}

fn summarize_render_counters(samples: &[&FramePerfSample]) -> RenderCounterSummary {
    RenderCounterSummary {
        frame_candidate_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.frame_candidate_count as f64),
        ),
        frame_visible_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.frame_visible_draw_count as f64),
        ),
        frame_culled_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.frame_culled_count as f64),
        ),
        frame_shadow_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.frame_shadow_draw_count as f64),
        ),
        terrain_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.terrain_draw_count as f64),
        ),
        model_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.model_draw_count as f64),
        ),
        sky_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.sky_draw_count as f64),
        ),
        post_process_draw_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.post_process_draw_count as f64),
        ),
        submitted_vertex_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.submitted_vertex_count as f64),
        ),
        submitted_index_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.submitted_index_count as f64),
        ),
        submitted_triangle_count: summarize_numeric(
            samples
                .iter()
                .map(|sample| sample.renderer_counters.submitted_triangle_count as f64),
        ),
    }
}

fn summarize_gpu_pass_timings(samples: &[&FramePerfSample]) -> GpuPassTimingSummary {
    GpuPassTimingSummary {
        shadow_cascade_ms: std::array::from_fn(|cascade_index| {
            summarize_numeric(
                samples
                    .iter()
                    .filter_map(|sample| sample.gpu_pass_timings.shadow_cascade_ms[cascade_index]),
            )
        }),
        scene_ms: summarize_numeric(
            samples
                .iter()
                .filter_map(|sample| sample.gpu_pass_timings.scene_ms),
        ),
        bloom_ms: summarize_numeric(
            samples
                .iter()
                .filter_map(|sample| sample.gpu_pass_timings.bloom_ms),
        ),
        post_process_ms: summarize_numeric(
            samples
                .iter()
                .filter_map(|sample| sample.gpu_pass_timings.post_process_ms),
        ),
        total_measured_ms: summarize_numeric(
            samples
                .iter()
                .filter_map(|sample| sample.gpu_pass_timings.total_measured_ms),
        ),
    }
}

fn summarize_numeric(values: impl IntoIterator<Item = f64>) -> NumericSummary {
    let mut finite_values = values
        .into_iter()
        .filter(|value| value.is_finite())
        .collect::<Vec<_>>();
    if finite_values.is_empty() {
        return NumericSummary::default();
    }

    let latest = *finite_values.last().unwrap_or(&0.0);
    let sum = finite_values.iter().sum::<f64>();
    finite_values
        .sort_by(|left, right| left.partial_cmp(right).unwrap_or(std::cmp::Ordering::Equal));
    let p95_index = finite_values.len().saturating_mul(95).saturating_add(99) / 100;
    let p95_index = p95_index.saturating_sub(1).min(finite_values.len() - 1);

    NumericSummary {
        latest,
        min: finite_values[0],
        max: finite_values[finite_values.len() - 1],
        average: sum / finite_values.len() as f64,
        p95: finite_values[p95_index],
    }
}

fn default_shadow_cascade_counters() -> Vec<ShadowCascadeCounter> {
    (0..SHADOW_CASCADE_COUNT)
        .map(|index| ShadowCascadeCounter {
            cascade_index: index as u32,
            ..ShadowCascadeCounter::default()
        })
        .collect()
}
