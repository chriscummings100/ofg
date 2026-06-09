// Unit tests for Rust frame performance diagnostics and render debug options.

use crate::config::SHADOW_CASCADE_COUNT;
use crate::perf::*;
use crate::ShadowSunMode;

#[test]
fn ring_keeps_samples_in_frame_order_after_wraparound() {
    let mut ring = FramePerfRing::new(2);
    ring.push(sample(1, 1.0));
    ring.push(sample(2, 2.0));
    ring.push(sample(3, 3.0));

    let frames = ring
        .samples()
        .iter()
        .map(|sample| sample.frame_index)
        .collect::<Vec<_>>();

    assert_eq!(frames, vec![2, 3]);
}

#[test]
fn ring_clear_and_empty_report_keep_stable_defaults() {
    let mut ring = FramePerfRing::new(0);

    assert!(ring.is_empty());
    assert_eq!(ring.len(), 0);
    assert_eq!(ring.report().capacity, 1);
    assert_eq!(
        ring.report().shadow_cascade_counters.len(),
        SHADOW_CASCADE_COUNT
    );

    ring.push(sample(1, 2.0));
    assert!(!ring.is_empty());
    assert_eq!(ring.len(), 1);
    ring.clear();

    let report = ring.report();
    assert!(ring.is_empty());
    assert_eq!(report.sample_count, 0);
    assert_eq!(report.rust_cpu.total_frame_ms, NumericSummary::default());
    assert!(report.terrain_lod_counters.is_empty());
}

#[test]
fn report_summarizes_numeric_values_and_ignores_non_finite_values() {
    let mut ring = FramePerfRing::new(4);
    ring.push(sample(1, 1.0));
    ring.push(sample(2, f64::NAN));
    ring.push(sample(3, 5.0));

    let report = ring.report();

    assert_eq!(report.sample_count, 3);
    assert_eq!(report.rust_cpu.total_frame_ms.latest, 5.0);
    assert_eq!(report.rust_cpu.total_frame_ms.min, 1.0);
    assert_eq!(report.rust_cpu.total_frame_ms.max, 5.0);
    assert_eq!(report.rust_cpu.total_frame_ms.average, 3.0);
    assert_eq!(report.renderer_counters.frame_visible_draw_count.max, 3.0);
}

#[test]
fn report_summarizes_gpu_timings_and_latest_lod_counters() {
    let mut first = sample(1, 1.0);
    first.gpu_pass_timings.scene_ms = Some(2.0);
    first.gpu_pass_timings.bloom_ms = Some(0.25);
    first.gpu_pass_timings.post_process_ms = Some(0.5);
    first.gpu_pass_timings.total_measured_ms = Some(2.75);
    first.renderer_counters.record_scene_draw(12, 12, Some(0));

    let mut second = sample(2, 2.0);
    second.gpu_pass_timings.scene_ms = Some(4.0);
    second.gpu_pass_timings.shadow_cascade_ms[1] = Some(1.0);
    second.gpu_pass_timings.total_measured_ms = Some(5.0);
    second.renderer_counters.record_scene_draw(24, 24, Some(3));

    let mut ring = FramePerfRing::new(2);
    ring.push(first);
    ring.push(second);

    let report = ring.report();

    assert_eq!(report.gpu.scene_ms.average, 3.0);
    assert_eq!(report.gpu.bloom_ms.latest, 0.25);
    assert_eq!(report.gpu.post_process_ms.latest, 0.5);
    assert_eq!(report.gpu.shadow_cascade_ms[1].latest, 1.0);
    assert_eq!(report.gpu.total_measured_ms.max, 5.0);
    assert_eq!(report.terrain_lod_counters[0].lod, 3);
}

#[test]
fn render_counters_track_scene_shadow_lod_and_post_draws() {
    let mut counters = RenderCounterSample::default();
    counters.set_main_camera_candidates(3);
    counters.record_main_camera_cull();
    counters.record_scene_draw(20, 60, Some(2));
    counters.record_scene_draw(4, 6, None);
    counters.record_sky_draw();
    counters.record_post_process_draw();
    counters.set_shadow_cascade_candidates(0, true, 3);
    counters.record_shadow_cull(0);
    counters.record_shadow_draw(0, 20, 60);

    assert_eq!(counters.frame_candidate_count, 3);
    assert_eq!(counters.frame_culled_count, 1);
    assert_eq!(counters.frame_visible_draw_count, 2);
    assert_eq!(counters.terrain_draw_count, 1);
    assert_eq!(counters.model_draw_count, 1);
    assert_eq!(counters.sky_draw_count, 1);
    assert_eq!(counters.post_process_draw_count, 1);
    assert_eq!(counters.frame_shadow_draw_count, 1);
    assert_eq!(counters.terrain_lod_counters[0].lod, 2);
    assert_eq!(counters.terrain_lod_counters[0].triangle_count, 20);
    assert_eq!(counters.shadow_cascade_counters[0].candidate_count, 3);
    assert_eq!(counters.shadow_cascade_counters[0].culled_count, 1);
    assert_eq!(counters.shadow_cascade_counters[0].triangle_count, 20);
}

#[test]
fn render_counters_cap_lod_tracking_and_mark_disabled_cascades() {
    let mut counters = RenderCounterSample::default();
    for lod in 0..(MAX_TRACKED_TERRAIN_LODS + 2) {
        counters.record_scene_draw(3, 3, Some(lod as u8));
    }
    counters.set_shadow_cascade_candidates(1, false, 8);
    counters.set_shadow_cascade_candidates(SHADOW_CASCADE_COUNT + 1, false, 8);
    counters.record_shadow_draw(SHADOW_CASCADE_COUNT + 1, 9, 9);

    assert_eq!(
        counters.terrain_lod_counters.len(),
        MAX_TRACKED_TERRAIN_LODS
    );
    assert_eq!(
        counters.terrain_lod_counters[MAX_TRACKED_TERRAIN_LODS - 1].lod,
        (MAX_TRACKED_TERRAIN_LODS - 1) as u32
    );
    assert!(!counters.shadow_cascade_counters[1].enabled);
    assert_eq!(counters.shadow_cascade_counters[1].culled_count, 8);
    assert_eq!(counters.frame_shadow_draw_count, 1);
}

#[test]
fn debug_options_validate_masks_and_partial_updates() {
    let defaults = RenderDebugOptions::default();
    let updated = defaults
        .apply_update(RenderDebugOptionsUpdate {
            terrain_lod_mask: Some(0b101),
            sky_enabled: Some(false),
            sky_cloud_noise_enabled: Some(false),
            shadow_cascade_mask: Some(0b0011),
            shadow_sun_mode: Some(ShadowSunMode::Overhead),
            material_mode: Some(RenderMaterialDebugMode::Lambert),
            ..RenderDebugOptionsUpdate::default()
        })
        .unwrap();

    assert!(updated.terrain_lod_enabled(0));
    assert!(!updated.terrain_lod_enabled(1));
    assert!(updated.terrain_lod_enabled(2));
    assert!(!updated.sky_enabled);
    assert!(!updated.sky_cloud_noise_enabled);
    assert!(updated.shadow_cascade_enabled(1));
    assert!(!updated.shadow_cascade_enabled(3));
    assert_eq!(updated.shadow_sun_mode, ShadowSunMode::Overhead);
    assert_eq!(updated.material_mode_code(), 1.0);
    assert_eq!(
        defaults.apply_update(RenderDebugOptionsUpdate {
            terrain_lod_mask: Some(0),
            ..RenderDebugOptionsUpdate::default()
        }),
        Err(RenderDebugOptionsError::EmptyTerrainLodMask)
    );
    assert_eq!(
        defaults.apply_update(RenderDebugOptionsUpdate {
            shadow_cascade_mask: Some(1 << SHADOW_CASCADE_COUNT),
            ..RenderDebugOptionsUpdate::default()
        }),
        Err(RenderDebugOptionsError::UnsupportedShadowCascadeMask)
    );
}

#[test]
fn debug_options_cover_sampling_effectiveness_and_error_messages() {
    let defaults = RenderDebugOptions::default();
    let updated = defaults
        .apply_update(RenderDebugOptionsUpdate {
            shadow_pass_enabled: Some(false),
            shadow_sampling_enabled: Some(true),
            white_textures_enabled: Some(true),
            material_mode: Some(RenderMaterialDebugMode::Full),
            ..RenderDebugOptionsUpdate::default()
        })
        .unwrap();

    assert!(!updated.effective_shadow_sampling_enabled());
    assert!(updated.white_textures_enabled);
    assert_eq!(updated.material_mode_code(), 0.0);
    assert!(!updated.terrain_lod_enabled(32));
    assert!(!updated.shadow_cascade_enabled(SHADOW_CASCADE_COUNT));
    assert_eq!(
        defaults.apply_update(RenderDebugOptionsUpdate {
            shadow_cascade_mask: Some(0),
            ..RenderDebugOptionsUpdate::default()
        }),
        Err(RenderDebugOptionsError::EmptyShadowCascadeMask)
    );
    assert_eq!(
        RenderDebugOptionsError::EmptyTerrainLodMask.to_string(),
        "empty terrain LOD debug mask"
    );
    assert_eq!(
        RenderDebugOptionsError::EmptyShadowCascadeMask.to_string(),
        "empty shadow cascade debug mask"
    );
    assert_eq!(
        RenderDebugOptionsError::UnsupportedShadowCascadeMask.to_string(),
        "unsupported shadow cascade debug mask"
    );
}

#[test]
fn gpu_timings_convert_timestamp_pairs_to_milliseconds() {
    let timings = GpuPassTimings::from_timestamp_pairs(
        2.0,
        &[10, 110, 200, 260],
        &[
            GpuTimestampPair {
                pass: GpuTimedPass::Scene,
                start_index: 0,
                end_index: 1,
            },
            GpuTimestampPair {
                pass: GpuTimedPass::ShadowCascade(0),
                start_index: 2,
                end_index: 3,
            },
        ],
    );

    assert_eq!(timings.scene_ms, Some(0.0002));
    assert_eq!(timings.shadow_cascade_ms[0], Some(0.00012));
    assert_eq!(timings.total_measured_ms, Some(0.00032));
}

#[test]
fn gpu_timings_ignore_invalid_pairs_and_cover_post_process_passes() {
    let invalid = GpuPassTimings::from_timestamp_pairs(
        1.0,
        &[30, 20],
        &[GpuTimestampPair {
            pass: GpuTimedPass::Scene,
            start_index: 0,
            end_index: 1,
        }],
    );
    let post = GpuPassTimings::from_timestamp_pairs(
        1.0,
        &[0, 1_000_000, 2_000_000, 3_000_000],
        &[
            GpuTimestampPair {
                pass: GpuTimedPass::Bloom,
                start_index: 0,
                end_index: 1,
            },
            GpuTimestampPair {
                pass: GpuTimedPass::PostProcess,
                start_index: 2,
                end_index: 3,
            },
        ],
    );

    assert_eq!(invalid, GpuPassTimings::default());
    assert_eq!(post.bloom_ms, Some(1.0));
    assert_eq!(post.post_process_ms, Some(1.0));
    assert_eq!(post.total_measured_ms, Some(2.0));
}

#[test]
fn parses_lod_from_stable_node_keys() {
    assert_eq!(terrain_lod_from_node_key("lod4:-1,0,3"), Some(4));
    assert_eq!(terrain_lod_from_node_key("0,0,0"), None);
    assert_eq!(terrain_lod_from_node_key("lodx:0,0,0"), None);
}

fn sample(frame_index: u32, total_frame_ms: f64) -> FramePerfSample {
    let mut counters = RenderCounterSample::default();
    for _ in 0..frame_index {
        counters.record_scene_draw(3, 3, None);
    }

    FramePerfSample {
        frame_index,
        rust_cpu: RustCpuFrameTimings {
            total_frame_ms,
            input_parse_ms: total_frame_ms * 0.1,
            game_state_tick_ms: total_frame_ms * 0.2,
            player_character_update_ms: total_frame_ms * 0.3,
            terrain_stream_update_ms: total_frame_ms * 0.4,
            render_frame_ms: total_frame_ms * 0.5,
            render_packet_build_ms: total_frame_ms * 0.6,
            renderer_prepare_ms: total_frame_ms * 0.7,
            renderer_shadow_cpu_ms: total_frame_ms * 0.8,
            renderer_scene_cpu_ms: total_frame_ms * 0.9,
            renderer_post_cpu_ms: total_frame_ms,
            renderer_submit_ms: total_frame_ms * 1.1,
        },
        renderer_counters: counters,
        gpu_pass_timings: GpuPassTimings::default(),
    }
}
