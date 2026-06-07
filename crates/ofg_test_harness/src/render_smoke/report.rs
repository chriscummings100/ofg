// Report and pixel-stat helpers for native Rust image smoke artifacts.

use std::collections::HashMap;
use std::fs;
use std::path::Path;

use serde::Serialize;

use super::error::{harness_error, HarnessResult};
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SmokeReport {
    pub kind: &'static str,
    pub artifact_dir: String,
    pub renderer: RendererReport,
    pub images: Vec<ImageReport>,
    pub shadow_images: Vec<ShadowImageReport>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RendererReport {
    pub backend: String,
    pub device_type: String,
    pub name: String,
    pub width: u32,
    pub height: u32,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImageReport {
    pub name: String,
    pub path: String,
    pub width: u32,
    pub height: u32,
    pub pixel_stats: PixelStats,
    pub debug: ScenarioDebug,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ShadowImageReport {
    pub name: String,
    pub path: String,
    pub width: u32,
    pub height: u32,
    pub pixel_stats: PixelStats,
    pub shadow_stats: ShadowDebugStats,
}

#[derive(Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PixelStats {
    pub sampled_pixels: u32,
    pub opaque_pixels: u32,
    pub unique_color_buckets: usize,
    pub dominant_color_ratio: f32,
    pub mean_color: MeanColor,
    pub lower_center_sky_like_pixels: u32,
    pub lower_center_sampled_pixels: u32,
    pub lower_center_sky_like_ratio: f32,
}

#[derive(Clone, Copy, Serialize)]
pub struct MeanColor {
    pub r: f32,
    pub g: f32,
    pub b: f32,
}

#[derive(Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ShadowDebugStats {
    pub sampled_pixels: u32,
    pub non_black_pixels: u32,
    pub non_white_pixels: u32,
    pub unique_luma_buckets: usize,
    pub dominant_luma_ratio: f32,
    pub min_luma: u8,
    pub max_luma: u8,
    pub mean_luma: f32,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScenarioDebug {
    pub terrain_seed: u32,
    pub terrain_preset: &'static str,
    pub terrain_preset_code: u32,
    pub center: [f32; 3],
    pub camera_eye: [f32; 3],
    pub camera_target: [f32; 3],
    pub rendered_chunk_count: usize,
    pub loaded_chunk_count: usize,
    pub rendered_node_count: usize,
    pub loaded_node_count: usize,
    pub stream_pending: bool,
    pub desired_render_node_count: usize,
    pub empty_node_count: usize,
    pub missing_node_count: usize,
    pub max_rendered_lod: u8,
    pub rendered_lod_counts: Vec<LodCountReport>,
    pub vertex_count: usize,
    pub index_count: usize,
    pub rendered_chunk_keys: Vec<String>,
    pub rendered_node_keys: Vec<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LodCountReport {
    pub lod: u8,
    pub node_count: usize,
}

/// Computes pixel statistics for a rendered RGBA image.
pub fn analyze_pixels(pixels: &[u8], width: u32, height: u32) -> PixelStats {
    let mut buckets = HashMap::<(u8, u8, u8), u32>::new();
    let mut sampled_pixels = 0;
    let mut opaque_pixels = 0;
    let mut lower_center_sampled_pixels = 0;
    let mut lower_center_sky_like_pixels = 0;
    let mut sum_r = 0_u64;
    let mut sum_g = 0_u64;
    let mut sum_b = 0_u64;
    let lower_y = height * 45 / 100;
    let center_min_x = width / 5;
    let center_max_x = width * 4 / 5;

    for y in (0..height).step_by(4) {
        for x in (0..width).step_by(4) {
            let offset = ((y * width + x) * 4) as usize;
            let r = pixels[offset];
            let g = pixels[offset + 1];
            let b = pixels[offset + 2];
            let a = pixels[offset + 3];
            *buckets.entry((r >> 4, g >> 4, b >> 4)).or_insert(0) += 1;
            sampled_pixels += 1;
            if a > 0 {
                opaque_pixels += 1;
            }
            sum_r += u64::from(r);
            sum_g += u64::from(g);
            sum_b += u64::from(b);
            if y >= lower_y && x >= center_min_x && x < center_max_x {
                lower_center_sampled_pixels += 1;
                if is_sky_like_pixel(r, g, b, a) {
                    lower_center_sky_like_pixels += 1;
                }
            }
        }
    }

    let dominant_bucket_count = buckets.values().copied().max().unwrap_or(0);
    let lower_center_sky_like_ratio = if lower_center_sampled_pixels == 0 {
        0.0
    } else {
        lower_center_sky_like_pixels as f32 / lower_center_sampled_pixels as f32
    };
    PixelStats {
        sampled_pixels,
        opaque_pixels,
        unique_color_buckets: buckets.len(),
        dominant_color_ratio: dominant_bucket_count as f32 / sampled_pixels as f32,
        mean_color: MeanColor {
            r: sum_r as f32 / sampled_pixels as f32,
            g: sum_g as f32 / sampled_pixels as f32,
            b: sum_b as f32 / sampled_pixels as f32,
        },
        lower_center_sky_like_pixels,
        lower_center_sampled_pixels,
        lower_center_sky_like_ratio,
    }
}

fn is_sky_like_pixel(r: u8, g: u8, b: u8, a: u8) -> bool {
    a > 200 && r > 165 && g > 190 && b > 190 && g.abs_diff(b) < 45
}

/// Computes luma statistics for a grayscale shadow debug visualization.
pub fn analyze_shadow_debug_pixels(pixels: &[u8], width: u32, height: u32) -> ShadowDebugStats {
    let mut buckets = HashMap::<u8, u32>::new();
    let mut sampled_pixels = 0;
    let mut non_black_pixels = 0;
    let mut non_white_pixels = 0;
    let mut sum_luma = 0_u64;
    let mut min_luma = u8::MAX;
    let mut max_luma = u8::MIN;

    for y in (0..height).step_by(4) {
        for x in (0..width).step_by(4) {
            let offset = ((y * width + x) * 4) as usize;
            let luma = pixels[offset];
            *buckets.entry(luma >> 4).or_insert(0) += 1;
            sampled_pixels += 1;
            if luma > 0 {
                non_black_pixels += 1;
            }
            if luma < 255 {
                non_white_pixels += 1;
            }
            sum_luma += u64::from(luma);
            min_luma = min_luma.min(luma);
            max_luma = max_luma.max(luma);
        }
    }

    let dominant_bucket_count = buckets.values().copied().max().unwrap_or(0);
    ShadowDebugStats {
        sampled_pixels,
        non_black_pixels,
        non_white_pixels,
        unique_luma_buckets: buckets.len(),
        dominant_luma_ratio: dominant_bucket_count as f32 / sampled_pixels as f32,
        min_luma,
        max_luma,
        mean_luma: sum_luma as f32 / sampled_pixels as f32,
    }
}

/// Fails when all shadow debug layers look empty or uninformative.
pub fn assert_shadow_debug_layers(stats: &[ShadowDebugStats], label: &str) -> HarnessResult<()> {
    if stats.is_empty() {
        return Err(harness_error(format!(
            "{label} did not produce shadow debug layers."
        )));
    }

    let has_visible_depth = stats
        .iter()
        .any(|stats| stats.non_black_pixels > stats.sampled_pixels / 1000);
    let has_luma_variation = stats
        .iter()
        .any(|stats| stats.unique_luma_buckets > 1 && stats.max_luma > stats.min_luma);
    if !has_visible_depth || !has_luma_variation {
        return Err(harness_error(format!(
            "{label} shadow debug layers look blank: visibleDepth={has_visible_depth} variation={has_luma_variation}."
        )));
    }

    Ok(())
}

/// Fails when a rendered image looks blank, transparent, or solid.
pub fn assert_pixel_stats(stats: PixelStats, label: &str) -> HarnessResult<()> {
    if stats.opaque_pixels < stats.sampled_pixels * 99 / 100 {
        return Err(harness_error(format!(
            "{label} image is not mostly opaque: opaque={} sampled={}.",
            stats.opaque_pixels, stats.sampled_pixels
        )));
    }
    if stats.unique_color_buckets < 8 {
        return Err(harness_error(format!(
            "{label} image has too little color variation: {} buckets.",
            stats.unique_color_buckets
        )));
    }
    if stats.dominant_color_ratio > 0.92 {
        return Err(harness_error(format!(
            "{label} image looks like a solid fill: dominant ratio {}.",
            stats.dominant_color_ratio
        )));
    }

    Ok(())
}

/// Fails when the lower-center image region is dominated by sky-colored pixels.
pub fn assert_no_large_lower_center_sky_hole(
    stats: PixelStats,
    label: &str,
) -> HarnessResult<()> {
    if stats.lower_center_sampled_pixels > 0 && stats.lower_center_sky_like_ratio > 0.35 {
        return Err(harness_error(format!(
            "{label} image has a large lower-center sky-colored gap: ratio {}.",
            stats.lower_center_sky_like_ratio
        )));
    }

    Ok(())
}

/// Returns an absolute, forward-slash-normalized path string for reports.
pub fn path_string(path: &Path) -> HarnessResult<String> {
    let absolute = fs::canonicalize(path)?;
    let mut text = absolute.to_string_lossy().to_string();
    if let Some(stripped) = text.strip_prefix(r"\\?\") {
        text = stripped.to_string();
    }
    Ok(text.replace('\\', "/"))
}

#[cfg(test)]
mod tests {
    use super::super::renderer::{HEIGHT, WIDTH};
    use super::*;

    #[test]
    fn analyze_pixels_samples_rgba_images_and_buckets_colors() {
        let mut pixels = vec![0_u8; (WIDTH * HEIGHT * 4) as usize];
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                let offset = ((y * WIDTH + x) * 4) as usize;
                pixels[offset] = (x % 251) as u8;
                pixels[offset + 1] = (y % 241) as u8;
                pixels[offset + 2] = ((x + y) % 239) as u8;
                pixels[offset + 3] = 255;
            }
        }

        let stats = analyze_pixels(&pixels, WIDTH, HEIGHT);

        assert_eq!(stats.sampled_pixels, (WIDTH / 4) * (HEIGHT / 4));
        assert_eq!(stats.opaque_pixels, stats.sampled_pixels);
        assert!(stats.unique_color_buckets > 8);
        assert!(stats.dominant_color_ratio < 0.92);
        assert!(stats.mean_color.r > 0.0);
        assert!(stats.mean_color.g > 0.0);
        assert!(stats.mean_color.b > 0.0);
        assert!(assert_pixel_stats(stats, "varied").is_ok());
    }

    #[test]
    fn analyze_pixels_counts_sky_like_holes_in_lower_center_region() {
        let mut pixels = vec![0_u8; (WIDTH * HEIGHT * 4) as usize];
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                let offset = ((y * WIDTH + x) * 4) as usize;
                pixels[offset] = 24;
                pixels[offset + 1] = 80;
                pixels[offset + 2] = 34;
                pixels[offset + 3] = 255;
            }
        }

        let sample_x = (WIDTH / 2) / 4 * 4;
        let sample_y = (HEIGHT / 2) / 4 * 4;
        let offset = ((sample_y * WIDTH + sample_x) * 4) as usize;
        pixels[offset] = 205;
        pixels[offset + 1] = 238;
        pixels[offset + 2] = 242;

        let stats = analyze_pixels(&pixels, WIDTH, HEIGHT);

        assert_eq!(stats.lower_center_sky_like_pixels, 1);
        assert!(stats.lower_center_sampled_pixels > 1);
        assert!(stats.lower_center_sky_like_ratio > 0.0);
    }

    #[test]
    fn shadow_debug_stats_detect_visible_depth_layers() {
        let mut blank = vec![0_u8; (16 * 16 * 4) as usize];
        let blank_stats = analyze_shadow_debug_pixels(&blank, 16, 16);
        assert_eq!(blank_stats.non_black_pixels, 0);
        assert!(assert_shadow_debug_layers(&[blank_stats], "blank").is_err());

        for index in 0..16 {
            let offset = index * 4;
            blank[offset] = (index * 8) as u8;
            blank[offset + 1] = (index * 8) as u8;
            blank[offset + 2] = (index * 8) as u8;
            blank[offset + 3] = 255;
        }
        let varied_stats = analyze_shadow_debug_pixels(&blank, 16, 16);
        assert!(varied_stats.non_black_pixels > 0);
        assert!(varied_stats.unique_luma_buckets > 1);
        assert!(assert_shadow_debug_layers(&[varied_stats], "varied").is_ok());
    }

    #[test]
    fn assert_pixel_stats_rejects_transparent_flat_or_solid_images() {
        let mostly_transparent = PixelStats {
            sampled_pixels: 100,
            opaque_pixels: 98,
            unique_color_buckets: 16,
            dominant_color_ratio: 0.25,
            mean_color: MeanColor {
                r: 1.0,
                g: 2.0,
                b: 3.0,
            },
            lower_center_sky_like_pixels: 0,
            lower_center_sampled_pixels: 100,
            lower_center_sky_like_ratio: 0.0,
        };
        assert!(assert_pixel_stats(mostly_transparent, "transparent").is_err());

        let low_variation = PixelStats {
            sampled_pixels: 100,
            opaque_pixels: 100,
            unique_color_buckets: 7,
            dominant_color_ratio: 0.25,
            mean_color: MeanColor {
                r: 1.0,
                g: 2.0,
                b: 3.0,
            },
            lower_center_sky_like_pixels: 0,
            lower_center_sampled_pixels: 100,
            lower_center_sky_like_ratio: 0.0,
        };
        assert!(assert_pixel_stats(low_variation, "flat").is_err());

        let solid_fill = PixelStats {
            sampled_pixels: 100,
            opaque_pixels: 100,
            unique_color_buckets: 16,
            dominant_color_ratio: 0.93,
            mean_color: MeanColor {
                r: 1.0,
                g: 2.0,
                b: 3.0,
            },
            lower_center_sky_like_pixels: 0,
            lower_center_sampled_pixels: 100,
            lower_center_sky_like_ratio: 0.0,
        };
        assert!(assert_pixel_stats(solid_fill, "solid").is_err());
    }

    #[test]
    fn lower_center_sky_hole_assertion_rejects_large_gaps() {
        let small_gap = PixelStats {
            sampled_pixels: 100,
            opaque_pixels: 100,
            unique_color_buckets: 16,
            dominant_color_ratio: 0.25,
            mean_color: MeanColor {
                r: 1.0,
                g: 2.0,
                b: 3.0,
            },
            lower_center_sky_like_pixels: 20,
            lower_center_sampled_pixels: 100,
            lower_center_sky_like_ratio: 0.2,
        };
        assert!(assert_no_large_lower_center_sky_hole(small_gap, "small").is_ok());

        let large_gap = PixelStats {
            lower_center_sky_like_pixels: 40,
            lower_center_sky_like_ratio: 0.4,
            ..small_gap
        };
        assert!(assert_no_large_lower_center_sky_hole(large_gap, "large").is_err());
    }

    #[test]
    fn smoke_report_serializes_with_camel_case_fields() {
        let report = SmokeReport {
            kind: "rust-offscreen-render",
            artifact_dir: "artifacts/rust-smoke/run-test".to_string(),
            renderer: RendererReport {
                backend: "test".to_string(),
                device_type: "cpu".to_string(),
                name: "renderer".to_string(),
                width: WIDTH,
                height: HEIGHT,
            },
            images: vec![ImageReport {
                name: "boot-frame".to_string(),
                path: "artifacts/rust-smoke/run-test/boot-frame.png".to_string(),
                width: WIDTH,
                height: HEIGHT,
                pixel_stats: PixelStats {
                    sampled_pixels: 1,
                    opaque_pixels: 1,
                    unique_color_buckets: 1,
                    dominant_color_ratio: 1.0,
                    mean_color: MeanColor {
                        r: 1.0,
                        g: 2.0,
                        b: 3.0,
                    },
                    lower_center_sky_like_pixels: 0,
                    lower_center_sampled_pixels: 1,
                    lower_center_sky_like_ratio: 0.0,
                },
                debug: ScenarioDebug {
                    terrain_seed: 246,
                    terrain_preset: "rollingHills",
                    terrain_preset_code: 1,
                    center: [0.0, 1.0, 2.0],
                    camera_eye: [3.0, 4.0, 5.0],
                    camera_target: [6.0, 7.0, 8.0],
                    rendered_chunk_count: 1,
                    loaded_chunk_count: 2,
                    rendered_node_count: 1,
                    loaded_node_count: 2,
                    stream_pending: false,
                    desired_render_node_count: 1,
                    empty_node_count: 0,
                    missing_node_count: 0,
                    max_rendered_lod: 0,
                    rendered_lod_counts: vec![LodCountReport {
                        lod: 0,
                        node_count: 1,
                    }],
                    vertex_count: 3,
                    index_count: 6,
                    rendered_chunk_keys: vec!["0,0,0".to_string()],
                    rendered_node_keys: vec!["lod0:0,0,0".to_string()],
                },
            }],
            shadow_images: vec![ShadowImageReport {
                name: "shadow-cascade-0".to_string(),
                path: "artifacts/rust-smoke/run-test/shadow-cascade-0.png".to_string(),
                width: 16,
                height: 16,
                pixel_stats: PixelStats {
                    sampled_pixels: 1,
                    opaque_pixels: 1,
                    unique_color_buckets: 1,
                    dominant_color_ratio: 1.0,
                    mean_color: MeanColor {
                        r: 4.0,
                        g: 4.0,
                        b: 4.0,
                    },
                    lower_center_sky_like_pixels: 0,
                    lower_center_sampled_pixels: 1,
                    lower_center_sky_like_ratio: 0.0,
                },
                shadow_stats: ShadowDebugStats {
                    sampled_pixels: 1,
                    non_black_pixels: 1,
                    non_white_pixels: 1,
                    unique_luma_buckets: 1,
                    dominant_luma_ratio: 1.0,
                    min_luma: 4,
                    max_luma: 4,
                    mean_luma: 4.0,
                },
            }],
        };

        let value = serde_json::to_value(report).expect("report should serialize");

        assert_eq!(value["kind"], "rust-offscreen-render");
        assert!(value["artifactDir"].is_string());
        assert!(value["renderer"]["deviceType"].is_string());
        assert!(value["images"][0]["pixelStats"]["sampledPixels"].is_number());
        assert_eq!(value["images"][0]["debug"]["terrainPreset"], "rollingHills");
        assert!(value["shadowImages"][0]["shadowStats"]["nonBlackPixels"].is_number());
    }

    #[test]
    fn path_string_returns_absolute_forward_slash_paths() {
        let path =
            path_string(Path::new("Cargo.toml")).expect("workspace file should canonicalize");

        assert!(path.contains('/'));
        assert!(path.ends_with("Cargo.toml"));
        assert!(!path.contains("\\"));
    }
}
