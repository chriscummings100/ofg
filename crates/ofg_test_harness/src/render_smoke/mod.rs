// Native Rust image smoke harness for OFG. It renders Rust-owned terrain meshes
// through wgpu into offscreen textures, writes PNGs, and emits an AI-readable
// JSON report without using the browser or TypeScript terrain clients.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use image::RgbaImage;

mod error;
mod renderer;
mod report;
mod scenarios;
mod shadow_debug;

pub use error::{HarnessError, HarnessResult};

use error::harness_error;
use renderer::{OffscreenRenderer, HEIGHT, WIDTH};
use report::{
    analyze_pixels, analyze_shadow_debug_pixels, assert_no_large_lower_center_sky_hole,
    assert_pixel_stats, assert_shadow_debug_layers, path_string, ImageReport, ShadowImageReport,
    SmokeReport,
};
use scenarios::{build_scenario_terrain, scenarios, Scenario, ScenarioFilter, ScenarioStreamMode};
use shadow_debug::ShadowDebugOutput;

struct Args {
    out_root: PathBuf,
    scenario: ScenarioFilter,
}

/// Parses args, renders selected scenarios, writes PNGs, and saves report JSON.
pub fn run() -> HarnessResult<()> {
    let args = parse_args()?;
    let run_dir = args.out_root.join(create_run_id()?);
    fs::create_dir_all(&run_dir)?;

    let renderer = pollster::block_on(OffscreenRenderer::new())?;
    let selected_scenarios = scenarios()
        .into_iter()
        .filter(|scenario| args.scenario.matches(scenario.group))
        .collect::<Vec<_>>();
    if selected_scenarios.is_empty() {
        return Err(harness_error("No Rust smoke scenarios matched the filter."));
    }

    let mut images = Vec::with_capacity(selected_scenarios.len());
    let mut shadow_images = Vec::new();
    for scenario in selected_scenarios {
        let rendered = render_scenario(&renderer, scenario, &run_dir)?;
        println!("Rust smoke image: {}", rendered.image.path);
        for shadow_image in &rendered.shadow_images {
            println!("Rust smoke shadow image: {}", shadow_image.path);
        }
        images.push(rendered.image);
        shadow_images.extend(rendered.shadow_images);
    }

    let report = SmokeReport {
        kind: "rust-offscreen-render",
        artifact_dir: path_string(&run_dir)?,
        renderer: renderer.report(),
        images,
        shadow_images,
    };
    let report_path = run_dir.join("report.json");
    let mut report_json = serde_json::to_vec_pretty(&report)?;
    report_json.push(b'\n');
    fs::write(&report_path, report_json)?;

    println!("Rust render smoke passed.");
    println!("Artifacts: {}", path_string(&run_dir)?);
    println!("Report: {}", path_string(&report_path)?);
    Ok(())
}

struct RenderedScenario {
    image: ImageReport,
    shadow_images: Vec<ShadowImageReport>,
}

impl Args {
    /// Returns default command arguments.
    fn default() -> Self {
        Self {
            out_root: PathBuf::from("artifacts/rust-smoke"),
            scenario: ScenarioFilter::All,
        }
    }
}

/// Parses command-line arguments.
fn parse_args() -> HarnessResult<Args> {
    parse_args_from(env::args().skip(1))
}

/// Parses command-line argument values.
fn parse_args_from<I>(values: I) -> HarnessResult<Args>
where
    I: IntoIterator,
    I::Item: Into<String>,
{
    let mut args = Args::default();
    let mut iter = values.into_iter().map(Into::into);
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--out" => {
                let value = iter
                    .next()
                    .ok_or_else(|| harness_error("--out requires a path value."))?;
                args.out_root = PathBuf::from(value);
            }
            "--scenario" => {
                let value = iter
                    .next()
                    .ok_or_else(|| harness_error("--scenario requires a filter value."))?;
                args.scenario = ScenarioFilter::parse(&value)?;
            }
            "--help" | "-h" => {
                println!(
                    "Usage: ofg-render-smoke [--out artifacts/rust-smoke] [--scenario all|boot|presets|seams|lods]"
                );
                std::process::exit(0);
            }
            _ => {
                return Err(harness_error(format!(
                    "Unknown argument '{arg}'. Use --help for usage."
                )));
            }
        }
    }

    Ok(args)
}

/// Runs one scenario through terrain streaming, rendering, PNG save, and stats.
fn render_scenario(
    renderer: &OffscreenRenderer,
    scenario: Scenario,
    run_dir: &Path,
) -> HarnessResult<RenderedScenario> {
    let terrain = build_scenario_terrain(scenario)?;
    let pixels = renderer.render(&terrain.camera, &terrain.meshes)?;
    let stats = analyze_pixels(&pixels, WIDTH, HEIGHT);
    assert_pixel_stats(stats, scenario.name)?;
    if scenario.stream_mode == ScenarioStreamMode::MultiLod {
        assert_no_large_lower_center_sky_hole(stats, scenario.name)?;
    }

    let image = RgbaImage::from_raw(WIDTH, HEIGHT, pixels)
        .ok_or_else(|| harness_error("Rust smoke could not create an RGBA image."))?;
    let image_path = run_dir.join(scenario.file_name);
    image.save(&image_path)?;

    let shadow_images = if scenario.shadow_debug {
        let shadow_debug = renderer.render_shadow_debug(&terrain.camera, &terrain.meshes)?;
        write_shadow_debug_images(shadow_debug, run_dir)?
    } else {
        Vec::new()
    };

    Ok(RenderedScenario {
        image: ImageReport {
            name: scenario.name.to_string(),
            path: path_string(&image_path)?,
            width: WIDTH,
            height: HEIGHT,
            pixel_stats: stats,
            debug: terrain.debug,
        },
        shadow_images,
    })
}

/// Writes shadow cascade visualizations and an atlas for one debug scenario.
fn write_shadow_debug_images(
    output: ShadowDebugOutput,
    run_dir: &Path,
) -> HarnessResult<Vec<ShadowImageReport>> {
    let mut reports = Vec::with_capacity(output.layers.len() + 1);
    let mut layer_stats = Vec::with_capacity(output.layers.len());
    for layer in output.layers {
        let file_name = format!("shadow-cascade-{}.png", layer.cascade_index);
        let name = format!("shadow-cascade-{}", layer.cascade_index);
        let pixel_stats = analyze_pixels(
            &layer.pixels,
            engine_web::SHADOW_MAP_SIZE,
            engine_web::SHADOW_MAP_SIZE,
        );
        let shadow_stats = analyze_shadow_debug_pixels(
            &layer.pixels,
            engine_web::SHADOW_MAP_SIZE,
            engine_web::SHADOW_MAP_SIZE,
        );
        layer_stats.push(shadow_stats);
        let image = RgbaImage::from_raw(
            engine_web::SHADOW_MAP_SIZE,
            engine_web::SHADOW_MAP_SIZE,
            layer.pixels,
        )
        .ok_or_else(|| harness_error("Rust smoke could not create a shadow cascade image."))?;
        let image_path = run_dir.join(file_name);
        image.save(&image_path)?;

        reports.push(ShadowImageReport {
            name,
            path: path_string(&image_path)?,
            width: engine_web::SHADOW_MAP_SIZE,
            height: engine_web::SHADOW_MAP_SIZE,
            pixel_stats,
            shadow_stats,
        });
    }
    assert_shadow_debug_layers(&layer_stats, "boot-frame")?;

    let atlas_pixel_stats = analyze_pixels(&output.atlas, output.atlas_width, output.atlas_height);
    let atlas_shadow_stats =
        analyze_shadow_debug_pixels(&output.atlas, output.atlas_width, output.atlas_height);
    let atlas = RgbaImage::from_raw(output.atlas_width, output.atlas_height, output.atlas)
        .ok_or_else(|| harness_error("Rust smoke could not create a shadow atlas image."))?;
    let atlas_path = run_dir.join("shadow-atlas.png");
    atlas.save(&atlas_path)?;
    reports.push(ShadowImageReport {
        name: "shadow-atlas".to_string(),
        path: path_string(&atlas_path)?,
        width: output.atlas_width,
        height: output.atlas_height,
        pixel_stats: atlas_pixel_stats,
        shadow_stats: atlas_shadow_stats,
    });

    Ok(reports)
}

/// Creates a timestamp-like run id without pulling in a date-time dependency.
fn create_run_id() -> HarnessResult<String> {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| harness_error(format!("System clock is before UNIX epoch: {error}")))?;
    Ok(format!("run-{}-{:03}", now.as_secs(), now.subsec_millis()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_args_uses_defaults_and_accepts_explicit_values() {
        let defaults = parse_args_from(Vec::<String>::new()).expect("defaults should parse");
        assert_eq!(defaults.out_root, PathBuf::from("artifacts/rust-smoke"));
        assert!(defaults.scenario.matches(ScenarioFilter::Boot));
        assert!(defaults.scenario.matches(ScenarioFilter::Presets));
        assert!(defaults.scenario.matches(ScenarioFilter::Seams));

        let explicit = parse_args_from([
            "--out".to_string(),
            "artifacts/custom-rust-smoke".to_string(),
            "--scenario".to_string(),
            "seams".to_string(),
        ])
        .expect("explicit args should parse");
        assert_eq!(
            explicit.out_root,
            PathBuf::from("artifacts/custom-rust-smoke")
        );
        assert!(explicit.scenario.matches(ScenarioFilter::Seams));
        assert!(!explicit.scenario.matches(ScenarioFilter::Boot));
    }

    #[test]
    fn parse_args_rejects_missing_values_and_unknown_flags() {
        assert!(parse_args_from(["--out"]).is_err());
        assert!(parse_args_from(["--scenario"]).is_err());
        assert!(parse_args_from(["--scenario", "unknown"]).is_err());
        assert!(parse_args_from(["--bogus"]).is_err());
    }

    #[test]
    fn run_ids_have_the_report_directory_shape() {
        let run_id = create_run_id().expect("run id should be available");

        assert!(run_id.starts_with("run-"));
        assert_eq!(run_id.matches('-').count(), 2);
    }
}
