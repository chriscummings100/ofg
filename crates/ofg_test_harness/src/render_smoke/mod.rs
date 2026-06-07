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

pub use error::{HarnessError, HarnessResult};

use error::harness_error;
use renderer::{OffscreenRenderer, HEIGHT, WIDTH};
use report::{analyze_pixels, assert_pixel_stats, path_string, ImageReport, SmokeReport};
use scenarios::{build_scenario_terrain, scenarios, Scenario, ScenarioFilter};

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
    for scenario in selected_scenarios {
        let rendered = render_scenario(&renderer, scenario, &run_dir)?;
        println!("Rust smoke image: {}", rendered.path);
        images.push(rendered);
    }

    let report = SmokeReport {
        kind: "rust-offscreen-render",
        artifact_dir: path_string(&run_dir)?,
        renderer: renderer.report(),
        images,
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
                    "Usage: ofg-render-smoke [--out artifacts/rust-smoke] [--scenario all|boot|presets|seams]"
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
) -> HarnessResult<ImageReport> {
    let terrain = build_scenario_terrain(scenario)?;
    let pixels = renderer.render(&terrain.camera, &terrain.meshes)?;
    let stats = analyze_pixels(&pixels);
    assert_pixel_stats(stats, scenario.name)?;

    let image = RgbaImage::from_raw(WIDTH, HEIGHT, pixels)
        .ok_or_else(|| harness_error("Rust smoke could not create an RGBA image."))?;
    let image_path = run_dir.join(scenario.file_name);
    image.save(&image_path)?;

    Ok(ImageReport {
        name: scenario.name.to_string(),
        path: path_string(&image_path)?,
        width: WIDTH,
        height: HEIGHT,
        pixel_stats: stats,
        debug: terrain.debug,
    })
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
