// Native Rust terrain benchmark for density filling, retained density windows,
// and chunk mesh generation. It replaces the old TypeScript/WASM benchmark so
// TypeScript never acts as a terrain client.

use std::env;
use std::error::Error;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use crate::terrain_bench_lod::{run_multi_lod_probe, MultiLodBenchmarkReport};
use serde::Serialize;
use terrain_core::benchmark::{
    density_chunk_sample_count, density_store_stats, fill_density_chunk,
    prepare_density_chunk_window, reset_density_store, DensityStoreStats, DensityWindowBounds,
};
use terrain_core::{build_chunk_mesh, TerrainChunkCoord, DEFAULT_TERRAIN_PRESET};

const DEFAULT_SEED: u32 = 0x0F6;
const DEFAULT_CELL_SIZE: f64 = 1.0;
const DEFAULT_ITERATIONS: usize = 6;
const DEFAULT_MESH_ITERATIONS: usize = 2;
const DEFAULT_WARMUP_ITERATIONS: usize = 1;
const STREAMING_HORIZONTAL_RADIUS: i32 = 1;
const STREAMING_VERTICAL_CHUNK_OFFSETS: [i32; 4] = [-2, -1, 0, 1];

const PRESETS: [PresetScenario; 4] = [
    PresetScenario {
        id: "seed",
        code: 0,
    },
    PresetScenario {
        id: "rollingHills",
        code: 1,
    },
    PresetScenario {
        id: "mountainValley",
        code: 2,
    },
    PresetScenario {
        id: "rockyHighland",
        code: 3,
    },
];

const CHUNK_COORDS: [TerrainChunkCoord; 3] = [
    TerrainChunkCoord { x: 0, y: 0, z: 0 },
    TerrainChunkCoord { x: -1, y: 0, z: 2 },
    TerrainChunkCoord { x: 3, y: -1, z: -2 },
];

const STREAMING_CENTERS: [TerrainChunkCoord; 4] = [
    TerrainChunkCoord { x: 0, y: 0, z: 0 },
    TerrainChunkCoord { x: 1, y: 0, z: 0 },
    TerrainChunkCoord { x: 2, y: 0, z: 0 },
    TerrainChunkCoord { x: 3, y: 0, z: 0 },
];

#[derive(Clone, Copy)]
struct PresetScenario {
    id: &'static str,
    code: u32,
}

#[derive(Clone)]
struct Args {
    output: Option<PathBuf>,
    iterations: usize,
    mesh_iterations: usize,
    warmup_iterations: usize,
    seed: u32,
    cell_size: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct BenchmarkScenario {
    seed: u32,
    preset: &'static str,
    preset_code: u32,
    chunk: ChunkReport,
    cell_size: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct StreamingWindowScenario {
    seed: u32,
    preset: &'static str,
    preset_code: u32,
    center: ChunkReport,
    bounds: DensityWindowReport,
    cell_size: f64,
}

#[derive(Clone, Copy, Serialize)]
struct ChunkReport {
    x: i32,
    y: i32,
    z: i32,
}

#[derive(Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
struct DensityWindowReport {
    min_x: i32,
    min_y: i32,
    min_z: i32,
    max_x: i32,
    max_y: i32,
    max_z: i32,
}

#[derive(Clone, Copy, Serialize)]
#[serde(rename_all = "camelCase")]
struct StoreStatsReport {
    entries: usize,
    max_entries: usize,
    reuses: u64,
    generations: u64,
    evictions: u64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct BenchmarkResult {
    name: &'static str,
    chunk_count: usize,
    total_ms: f64,
    mean_ms: f64,
    median_ms: f64,
    p95_ms: f64,
    min_ms: f64,
    max_ms: f64,
    checksum: f64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct BenchmarkResults {
    fill_only: BenchmarkResult,
    fill_and_copy: BenchmarkResult,
    apron_fill_only: BenchmarkResult,
    density_window_prepare_retained: BenchmarkResult,
    mesh_build_and_copy_cold: BenchmarkResult,
    mesh_build_and_copy_prepared: BenchmarkResult,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DensityStoreReport {
    after_retained_window_prepare: StoreStatsReport,
    after_prepared_mesh: StoreStatsReport,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct PhaseEstimate {
    median_apron_density_share_of_mesh: f64,
    median_prepared_mesh_share_of_cold_mesh: f64,
    median_mesh_residual_ms: f64,
    mean_apron_density_share_of_mesh: f64,
    mean_prepared_mesh_share_of_cold_mesh: f64,
    mean_mesh_residual_ms: f64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct TerrainBenchmarkReport {
    benchmark: &'static str,
    implementation: &'static str,
    artifact_dir: String,
    seed: u32,
    cell_size: f64,
    sample_count: usize,
    samples_per_chunk: &'static str,
    scenario_count: usize,
    iterations: usize,
    mesh_iterations: usize,
    warmup_iterations: usize,
    chunks_per_benchmark: usize,
    chunks_per_mesh_benchmark: usize,
    streaming_window_count: usize,
    streaming_horizontal_radius: i32,
    streaming_vertical_chunk_offsets: Vec<i32>,
    results: BenchmarkResults,
    density_store: DensityStoreReport,
    phase_estimate: PhaseEstimate,
    multi_lod: MultiLodBenchmarkReport,
    scenarios: Vec<BenchmarkScenario>,
    streaming_windows: Vec<StreamingWindowScenario>,
}

/// Parses CLI args, runs terrain benchmark phases, and writes report JSON.
pub fn run() -> Result<(), Box<dyn Error>> {
    let args = parse_args()?;
    let scenarios = build_scenarios(args.seed, args.cell_size);
    let streaming_windows = build_streaming_windows(args.seed, args.cell_size);
    let output_path = args.output.unwrap_or_else(default_output_path);
    let output_dir = output_path
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));
    fs::create_dir_all(&output_dir)?;

    println!(
        "Benchmarking {} Rust terrain density chunk scenarios.",
        scenarios.len()
    );
    println!(
        "Warmup: {} pass(es). Density iterations: {}. Mesh iterations: {}.",
        args.warmup_iterations, args.iterations, args.mesh_iterations
    );
    warm_up(&scenarios, args.warmup_iterations);
    reset_density_store();
    println!("Warmup complete.");

    println!("Running fill-only benchmark...");
    let fill_only = benchmark("fillOnly", &scenarios, args.iterations, |scenario| {
        let densities = fill_density_chunk(
            scenario.seed,
            scenario.preset_code,
            terrain_coord(scenario.chunk),
            scenario.cell_size,
        );
        density_checksum(&densities)
    });

    println!("Running fill-plus-copy benchmark...");
    let fill_and_copy = benchmark("fillAndCopy", &scenarios, args.iterations, |scenario| {
        let densities = fill_density_chunk(
            scenario.seed,
            scenario.preset_code,
            terrain_coord(scenario.chunk),
            scenario.cell_size,
        );
        let copy = densities.clone();
        density_checksum(&copy)
    });

    println!("Running neighbor-apron fill benchmark...");
    let apron_fill_only = benchmark("apronFillOnly", &scenarios, args.iterations, |scenario| {
        neighbor_apron_chunks(terrain_coord(scenario.chunk))
            .iter()
            .map(|coord| {
                let densities = fill_density_chunk(
                    scenario.seed,
                    scenario.preset_code,
                    *coord,
                    scenario.cell_size,
                );
                density_checksum(&densities)
            })
            .sum()
    });

    println!("Running retained density-window prepare benchmark...");
    reset_density_store();
    let density_window_prepare_retained = benchmark(
        "densityWindowPrepareRetained",
        &streaming_windows,
        args.iterations,
        |scenario| {
            let prepared = prepare_density_chunk_window(
                scenario.seed,
                scenario.preset_code,
                density_bounds(scenario.bounds),
                scenario.cell_size,
            );
            let stats = density_store_stats();
            prepared as f64 + stats.entries as f64 + stats.reuses as f64 + stats.generations as f64
                - stats.evictions as f64
        },
    );
    let density_window_store_stats = store_stats_report(density_store_stats());

    println!("Running cold mesh-build-plus-copy benchmark...");
    let mesh_build_and_copy_cold = benchmark(
        "meshBuildAndCopyCold",
        &scenarios,
        args.mesh_iterations,
        |scenario| {
            reset_density_store();
            mesh_build_and_copy_checksum(scenario)
        },
    );

    println!("Running prepared mesh-build-plus-copy benchmark...");
    let mesh_build_and_copy_prepared = benchmark_with_before(
        "meshBuildAndCopyPrepared",
        &scenarios,
        args.mesh_iterations,
        |scenario| {
            reset_density_store();
            let coord = terrain_coord(scenario.chunk);
            prepare_density_chunk_window(
                scenario.seed,
                scenario.preset_code,
                DensityWindowBounds {
                    min_x: coord.x,
                    min_y: coord.y,
                    min_z: coord.z,
                    max_x: coord.x + 1,
                    max_y: coord.y + 1,
                    max_z: coord.z + 1,
                },
                scenario.cell_size,
            );
        },
        mesh_build_and_copy_checksum,
    );
    let prepared_mesh_store_stats = store_stats_report(density_store_stats());

    let phase_estimate = phase_estimate(
        &apron_fill_only,
        &mesh_build_and_copy_cold,
        &mesh_build_and_copy_prepared,
    );
    println!("Running multi-LOD stream probe...");
    let multi_lod = run_multi_lod_probe(args.seed, DEFAULT_TERRAIN_PRESET);
    let report = TerrainBenchmarkReport {
        benchmark: "terrain-rust-chunk-pipeline",
        implementation: "terrain_core-rlib",
        artifact_dir: path_string(&output_dir)?,
        seed: args.seed,
        cell_size: args.cell_size,
        sample_count: density_chunk_sample_count(),
        samples_per_chunk: "33x33x33",
        scenario_count: scenarios.len(),
        iterations: args.iterations,
        mesh_iterations: args.mesh_iterations,
        warmup_iterations: args.warmup_iterations,
        chunks_per_benchmark: scenarios.len() * args.iterations,
        chunks_per_mesh_benchmark: scenarios.len() * args.mesh_iterations,
        streaming_window_count: streaming_windows.len(),
        streaming_horizontal_radius: STREAMING_HORIZONTAL_RADIUS,
        streaming_vertical_chunk_offsets: STREAMING_VERTICAL_CHUNK_OFFSETS.to_vec(),
        results: BenchmarkResults {
            fill_only,
            fill_and_copy,
            apron_fill_only,
            density_window_prepare_retained,
            mesh_build_and_copy_cold,
            mesh_build_and_copy_prepared,
        },
        density_store: DensityStoreReport {
            after_retained_window_prepare: density_window_store_stats,
            after_prepared_mesh: prepared_mesh_store_stats,
        },
        phase_estimate,
        multi_lod,
        scenarios,
        streaming_windows,
    };

    let mut report_json = serde_json::to_vec_pretty(&report)?;
    report_json.push(b'\n');
    fs::write(&output_path, report_json)?;

    print_summary(&report, &output_path)?;
    Ok(())
}

/// Parses command-line arguments.
fn parse_args() -> Result<Args, Box<dyn Error>> {
    parse_args_from(env::args().skip(1))
}

/// Parses command-line arguments from a caller-provided iterator.
fn parse_args_from(raw_args: impl IntoIterator<Item = String>) -> Result<Args, Box<dyn Error>> {
    let mut args = Args {
        output: None,
        iterations: DEFAULT_ITERATIONS,
        mesh_iterations: DEFAULT_MESH_ITERATIONS,
        warmup_iterations: DEFAULT_WARMUP_ITERATIONS,
        seed: DEFAULT_SEED,
        cell_size: DEFAULT_CELL_SIZE,
    };
    let mut iter = raw_args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--output" => args.output = Some(PathBuf::from(read_arg_value(&mut iter, "--output")?)),
            "--iterations" => {
                args.iterations = read_positive_usize(&mut iter, "--iterations")?;
            }
            "--mesh-iterations" => {
                args.mesh_iterations = read_positive_usize(&mut iter, "--mesh-iterations")?;
            }
            "--warmup" => {
                args.warmup_iterations = read_positive_usize(&mut iter, "--warmup")?;
            }
            "--seed" => {
                args.seed = read_non_negative_u32(&mut iter, "--seed")?;
            }
            "--cell-size" => {
                args.cell_size = read_positive_f64(&mut iter, "--cell-size")?;
            }
            "--help" | "-h" => {
                println!("Usage: ofg-terrain-bench [--output artifacts/terrain-bench/<run>/report.json] [--iterations N] [--mesh-iterations N] [--warmup N] [--seed N] [--cell-size N]");
                std::process::exit(0);
            }
            _ => {
                return Err(invalid_input(format!(
                    "Unknown argument '{arg}'. Use --help for usage."
                )));
            }
        }
    }

    Ok(args)
}

/// Reads the next CLI argument value.
fn read_arg_value(
    iter: &mut impl Iterator<Item = String>,
    name: &str,
) -> Result<String, Box<dyn Error>> {
    let value = iter
        .next()
        .ok_or_else(|| invalid_input(format!("{name} requires a value.")))?;
    if value.starts_with("--") {
        return Err(invalid_input(format!("{name} requires a value.")));
    }

    Ok(value)
}

/// Reads a positive usize CLI value.
fn read_positive_usize(
    iter: &mut impl Iterator<Item = String>,
    name: &str,
) -> Result<usize, Box<dyn Error>> {
    let value = read_arg_value(iter, name)?;
    let number = value.parse::<usize>()?;
    if number == 0 {
        return Err(invalid_input(format!("{name} must be a positive integer.")));
    }

    Ok(number)
}

/// Reads a non-negative u32 CLI value.
fn read_non_negative_u32(
    iter: &mut impl Iterator<Item = String>,
    name: &str,
) -> Result<u32, Box<dyn Error>> {
    let value = read_arg_value(iter, name)?;
    Ok(value.parse::<u32>()?)
}

/// Reads a positive f64 CLI value.
fn read_positive_f64(
    iter: &mut impl Iterator<Item = String>,
    name: &str,
) -> Result<f64, Box<dyn Error>> {
    let value = read_arg_value(iter, name)?;
    let number = value.parse::<f64>()?;
    if !number.is_finite() || number <= 0.0 {
        return Err(invalid_input(format!("{name} must be a positive number.")));
    }

    Ok(number)
}

/// Builds a benchmark input row for every preset/chunk pair.
fn build_scenarios(seed: u32, cell_size: f64) -> Vec<BenchmarkScenario> {
    PRESETS
        .iter()
        .flat_map(|preset| {
            CHUNK_COORDS.iter().map(move |chunk| BenchmarkScenario {
                seed,
                preset: preset.id,
                preset_code: preset.code,
                chunk: chunk_report(*chunk),
                cell_size,
            })
        })
        .collect()
}

/// Builds retained streaming-window benchmark inputs.
fn build_streaming_windows(seed: u32, cell_size: f64) -> Vec<StreamingWindowScenario> {
    PRESETS
        .iter()
        .flat_map(|preset| {
            STREAMING_CENTERS
                .iter()
                .map(move |center| StreamingWindowScenario {
                    seed,
                    preset: preset.id,
                    preset_code: preset.code,
                    center: chunk_report(*center),
                    bounds: density_window_bounds(*center),
                    cell_size,
                })
        })
        .collect()
}

/// Runs warmup density fills before timed benchmark phases.
fn warm_up(scenarios: &[BenchmarkScenario], passes: usize) {
    for _ in 0..passes {
        for scenario in scenarios {
            let _ = fill_density_chunk(
                scenario.seed,
                scenario.preset_code,
                terrain_coord(scenario.chunk),
                scenario.cell_size,
            );
        }
    }
}

/// Runs a timed benchmark over a scenario list.
fn benchmark<T>(
    name: &'static str,
    scenarios: &[T],
    iterations: usize,
    mut run_scenario: impl FnMut(&T) -> f64,
) -> BenchmarkResult {
    benchmark_with_before(
        name,
        scenarios,
        iterations,
        |_| {},
        |scenario| run_scenario(scenario),
    )
}

/// Runs a timed benchmark with per-scenario setup outside the measured section.
fn benchmark_with_before<T>(
    name: &'static str,
    scenarios: &[T],
    iterations: usize,
    mut before_scenario: impl FnMut(&T),
    mut run_scenario: impl FnMut(&T) -> f64,
) -> BenchmarkResult {
    let mut durations = Vec::with_capacity(scenarios.len() * iterations);
    let mut checksum = 0.0;
    let started_at = Instant::now();

    for _ in 0..iterations {
        for scenario in scenarios {
            before_scenario(scenario);
            let start = Instant::now();
            checksum += run_scenario(scenario);
            durations.push(start.elapsed().as_secs_f64() * 1000.0);
        }
    }

    let total_ms = started_at.elapsed().as_secs_f64() * 1000.0;
    let mut sorted = durations.clone();
    sorted.sort_by(|left, right| left.total_cmp(right));
    let sum = durations.iter().sum::<f64>();

    BenchmarkResult {
        name,
        chunk_count: durations.len(),
        total_ms,
        mean_ms: sum / durations.len() as f64,
        median_ms: percentile(&sorted, 0.5),
        p95_ms: percentile(&sorted, 0.95),
        min_ms: sorted[0],
        max_ms: sorted[sorted.len() - 1],
        checksum,
    }
}

/// Returns a coarse checksum that keeps benchmark work observable.
fn density_checksum(densities: &[f32]) -> f64 {
    if densities.is_empty() {
        return 0.0;
    }

    let middle = densities.len() / 2;
    f64::from(densities[0] + densities[middle] + densities[densities.len() - 1])
}

/// Builds a mesh, copies its buffers, and returns a checksum.
fn mesh_build_and_copy_checksum(scenario: &BenchmarkScenario) -> f64 {
    let mesh = build_chunk_mesh(
        scenario.seed,
        scenario.preset_code,
        terrain_coord(scenario.chunk),
        scenario.cell_size,
    );
    let vertices = mesh.vertices.clone();
    let indices = mesh.indices.clone();

    vertices.len() as f64
        + indices.len() as f64
        + vertices.first().copied().unwrap_or(0.0) as f64
        + indices.first().copied().unwrap_or(0) as f64
}

/// Returns the eight density chunks needed by the chunk-mesh apron.
fn neighbor_apron_chunks(chunk: TerrainChunkCoord) -> Vec<TerrainChunkCoord> {
    let mut chunks = Vec::with_capacity(8);
    for dz in 0..=1 {
        for dy in 0..=1 {
            for dx in 0..=1 {
                chunks.push(TerrainChunkCoord {
                    x: chunk.x + dx,
                    y: chunk.y + dy,
                    z: chunk.z + dz,
                });
            }
        }
    }

    chunks
}

/// Builds retained-density bounds around a stream center.
fn density_window_bounds(center: TerrainChunkCoord) -> DensityWindowReport {
    let min_vertical_offset = STREAMING_VERTICAL_CHUNK_OFFSETS
        .iter()
        .copied()
        .min()
        .unwrap_or(0);
    let max_vertical_offset = STREAMING_VERTICAL_CHUNK_OFFSETS
        .iter()
        .copied()
        .max()
        .unwrap_or(0);

    DensityWindowReport {
        min_x: center.x - STREAMING_HORIZONTAL_RADIUS,
        min_y: center.y + min_vertical_offset,
        min_z: center.z - STREAMING_HORIZONTAL_RADIUS,
        max_x: center.x + STREAMING_HORIZONTAL_RADIUS + 1,
        max_y: center.y + max_vertical_offset + 1,
        max_z: center.z + STREAMING_HORIZONTAL_RADIUS + 1,
    }
}

/// Computes high-level phase ratios from benchmark medians and means.
fn phase_estimate(
    apron_fill: &BenchmarkResult,
    cold_mesh: &BenchmarkResult,
    prepared_mesh: &BenchmarkResult,
) -> PhaseEstimate {
    PhaseEstimate {
        median_apron_density_share_of_mesh: ratio(apron_fill.median_ms, cold_mesh.median_ms),
        median_prepared_mesh_share_of_cold_mesh: ratio(
            prepared_mesh.median_ms,
            cold_mesh.median_ms,
        ),
        median_mesh_residual_ms: prepared_mesh.median_ms,
        mean_apron_density_share_of_mesh: ratio(apron_fill.mean_ms, cold_mesh.mean_ms),
        mean_prepared_mesh_share_of_cold_mesh: ratio(prepared_mesh.mean_ms, cold_mesh.mean_ms),
        mean_mesh_residual_ms: prepared_mesh.mean_ms,
    }
}

/// Returns a ratio that stays finite for empty timings.
fn ratio(numerator: f64, denominator: f64) -> f64 {
    if denominator <= 0.0 {
        0.0
    } else {
        numerator / denominator
    }
}

/// Returns a percentile from a sorted duration list.
fn percentile(sorted: &[f64], fraction: f64) -> f64 {
    let index = ((sorted.len() as f64 * fraction).ceil() as usize)
        .saturating_sub(1)
        .min(sorted.len() - 1);
    sorted[index]
}

/// Converts chunk coord into a serializable report value.
fn chunk_report(coord: TerrainChunkCoord) -> ChunkReport {
    ChunkReport {
        x: coord.x,
        y: coord.y,
        z: coord.z,
    }
}

/// Converts a serializable report value back to a terrain coord.
fn terrain_coord(coord: ChunkReport) -> TerrainChunkCoord {
    TerrainChunkCoord {
        x: coord.x,
        y: coord.y,
        z: coord.z,
    }
}

/// Converts a serializable report value back to density window bounds.
fn density_bounds(bounds: DensityWindowReport) -> DensityWindowBounds {
    DensityWindowBounds {
        min_x: bounds.min_x,
        min_y: bounds.min_y,
        min_z: bounds.min_z,
        max_x: bounds.max_x,
        max_y: bounds.max_y,
        max_z: bounds.max_z,
    }
}

/// Converts terrain store counters into serializable report counters.
fn store_stats_report(stats: DensityStoreStats) -> StoreStatsReport {
    StoreStatsReport {
        entries: stats.entries,
        max_entries: stats.max_entries,
        reuses: stats.reuses,
        generations: stats.generations,
        evictions: stats.evictions,
    }
}

/// Prints a short console summary after writing JSON.
fn print_summary(
    report: &TerrainBenchmarkReport,
    output_path: &Path,
) -> Result<(), Box<dyn Error>> {
    println!(
        "Terrain Rust chunk benchmark ({} density chunks)",
        report.chunks_per_benchmark
    );
    println!(
        "  fill only:    median {:.3} ms/chunk, p95 {:.3}, mean {:.3}",
        report.results.fill_only.median_ms,
        report.results.fill_only.p95_ms,
        report.results.fill_only.mean_ms
    );
    println!(
        "  fill + copy:  median {:.3} ms/chunk, p95 {:.3}, mean {:.3}",
        report.results.fill_and_copy.median_ms,
        report.results.fill_and_copy.p95_ms,
        report.results.fill_and_copy.mean_ms
    );
    println!(
        "  apron fill:   median {:.3} ms/mesh, p95 {:.3}, mean {:.3}",
        report.results.apron_fill_only.median_ms,
        report.results.apron_fill_only.p95_ms,
        report.results.apron_fill_only.mean_ms
    );
    println!(
        "  density window prepare: median {:.3} ms/window, p95 {:.3}, mean {:.3}",
        report.results.density_window_prepare_retained.median_ms,
        report.results.density_window_prepare_retained.p95_ms,
        report.results.density_window_prepare_retained.mean_ms
    );
    println!(
        "  mesh + copy cold:       median {:.3} ms/chunk, p95 {:.3}, mean {:.3}",
        report.results.mesh_build_and_copy_cold.median_ms,
        report.results.mesh_build_and_copy_cold.p95_ms,
        report.results.mesh_build_and_copy_cold.mean_ms
    );
    println!(
        "  mesh + copy prepared:   median {:.3} ms/chunk, p95 {:.3}, mean {:.3}",
        report.results.mesh_build_and_copy_prepared.median_ms,
        report.results.mesh_build_and_copy_prepared.p95_ms,
        report.results.mesh_build_and_copy_prepared.mean_ms
    );
    println!(
        "  prepared mesh residual: median {:.3} ms/chunk ({:.1}% of cold median mesh time)",
        report.phase_estimate.median_mesh_residual_ms,
        report
            .phase_estimate
            .median_prepared_mesh_share_of_cold_mesh
            * 100.0
    );
    println!(
        "  retained density store: {} entries, {} reuses, {} generations, {} evictions",
        report.density_store.after_retained_window_prepare.entries,
        report.density_store.after_retained_window_prepare.reuses,
        report
            .density_store
            .after_retained_window_prepare
            .generations,
        report.density_store.after_retained_window_prepare.evictions
    );
    println!(
        "  multi-LOD stream: {} rendered nodes, max LOD {}, span {:.0}m x {:.0}m",
        report.multi_lod.rendered_node_count,
        report.multi_lod.max_rendered_lod,
        report.multi_lod.visible_world_span_x_meters,
        report.multi_lod.visible_world_span_z_meters
    );
    println!("Report: {}", path_string(output_path)?);
    Ok(())
}

/// Returns the default report path for one benchmark run.
fn default_output_path() -> PathBuf {
    PathBuf::from("artifacts/terrain-bench")
        .join(create_run_id().unwrap_or_else(|_| "run-unknown".to_string()))
        .join("report.json")
}

/// Creates a timestamp-like run id without pulling in a date-time dependency.
fn create_run_id() -> io::Result<String> {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidInput, error.to_string()))?;
    Ok(format!("run-{}-{:03}", now.as_secs(), now.subsec_millis()))
}

/// Returns an absolute, forward-slash-normalized path string for reports.
fn path_string(path: &Path) -> io::Result<String> {
    let absolute = fs::canonicalize(path)?;
    let mut text = absolute.to_string_lossy().to_string();
    if let Some(stripped) = text.strip_prefix(r"\\?\") {
        text = stripped.to_string();
    }
    Ok(text.replace('\\', "/"))
}

/// Builds a boxed invalid-input error.
fn invalid_input(message: impl Into<String>) -> Box<dyn Error> {
    Box::new(io::Error::new(io::ErrorKind::InvalidInput, message.into()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_expected_scenario_matrix() {
        let scenarios = build_scenarios(0x0F6, 1.0);

        assert_eq!(scenarios.len(), PRESETS.len() * CHUNK_COORDS.len());
        assert_eq!(scenarios[0].preset, "seed");
        assert_eq!(scenarios[0].chunk.x, 0);
        assert_eq!(scenarios[3].preset, "rollingHills");
    }

    #[test]
    fn parses_defaults_and_explicit_cli_values() {
        let defaults = parse_args_from(Vec::<String>::new()).unwrap();
        assert_eq!(defaults.output, None);
        assert_eq!(defaults.iterations, DEFAULT_ITERATIONS);
        assert_eq!(defaults.mesh_iterations, DEFAULT_MESH_ITERATIONS);
        assert_eq!(defaults.warmup_iterations, DEFAULT_WARMUP_ITERATIONS);
        assert_eq!(defaults.seed, DEFAULT_SEED);
        assert_eq!(defaults.cell_size, DEFAULT_CELL_SIZE);

        let explicit = parse_args_from(
            [
                "--output",
                "artifacts/terrain-bench/unit/report.json",
                "--iterations",
                "3",
                "--mesh-iterations",
                "2",
                "--warmup",
                "1",
                "--seed",
                "123",
                "--cell-size",
                "0.5",
            ]
            .into_iter()
            .map(String::from),
        )
        .unwrap();

        assert_eq!(
            explicit.output,
            Some(PathBuf::from("artifacts/terrain-bench/unit/report.json"))
        );
        assert_eq!(explicit.iterations, 3);
        assert_eq!(explicit.mesh_iterations, 2);
        assert_eq!(explicit.warmup_iterations, 1);
        assert_eq!(explicit.seed, 123);
        assert_eq!(explicit.cell_size, 0.5);
    }

    #[test]
    fn parser_rejects_missing_unknown_and_non_positive_values() {
        assert!(parse_args_from(["--output"].into_iter().map(String::from)).is_err());
        assert!(parse_args_from(["--wat"].into_iter().map(String::from)).is_err());
        assert!(parse_args_from(["--iterations", "0"].into_iter().map(String::from)).is_err());
        assert!(parse_args_from(["--cell-size", "-1"].into_iter().map(String::from)).is_err());
    }

    #[test]
    fn builds_expected_streaming_windows() {
        let windows = build_streaming_windows(0x0F6, 1.0);
        let first = &windows[0];

        assert_eq!(windows.len(), PRESETS.len() * STREAMING_CENTERS.len());
        assert_eq!(first.preset, "seed");
        assert_eq!(first.center.x, 0);
        assert_eq!(first.bounds.min_x, -1);
        assert_eq!(first.bounds.min_y, -2);
        assert_eq!(first.bounds.min_z, -1);
        assert_eq!(first.bounds.max_x, 2);
        assert_eq!(first.bounds.max_y, 2);
        assert_eq!(first.bounds.max_z, 2);
    }

    #[test]
    fn benchmark_result_reports_sorted_duration_stats() {
        let scenarios = [1.0, 2.0, 3.0];
        let result = benchmark("unit", &scenarios, 1, |value| *value);

        assert_eq!(result.name, "unit");
        assert_eq!(result.chunk_count, 3);
        assert_eq!(result.checksum, 6.0);
        assert!(result.mean_ms >= 0.0);
        assert!(result.median_ms >= 0.0);
        assert!(result.p95_ms >= 0.0);
    }

    #[test]
    fn helper_conversions_and_checksums_are_stable() {
        let coord = TerrainChunkCoord { x: -2, y: 3, z: 4 };
        let report = chunk_report(coord);
        assert_eq!(terrain_coord(report), coord);

        let bounds = density_window_bounds(TerrainChunkCoord { x: 1, y: 2, z: 3 });
        let converted = density_bounds(bounds);
        assert_eq!(converted.min_x, 0);
        assert_eq!(converted.min_y, 0);
        assert_eq!(converted.min_z, 2);
        assert_eq!(converted.max_x, 3);
        assert_eq!(converted.max_y, 4);
        assert_eq!(converted.max_z, 5);

        assert_eq!(density_checksum(&[]), 0.0);
        assert_eq!(density_checksum(&[1.0, 2.0, 3.0]), 6.0);

        let apron = neighbor_apron_chunks(TerrainChunkCoord { x: 5, y: 6, z: 7 });
        assert_eq!(apron.len(), 8);
        assert_eq!(apron[0], TerrainChunkCoord { x: 5, y: 6, z: 7 });
        assert_eq!(apron[7], TerrainChunkCoord { x: 6, y: 7, z: 8 });
    }

    #[test]
    fn phase_and_store_reports_keep_expected_fields() {
        let apron = BenchmarkResult {
            name: "apron",
            chunk_count: 1,
            total_ms: 4.0,
            mean_ms: 4.0,
            median_ms: 4.0,
            p95_ms: 4.0,
            min_ms: 4.0,
            max_ms: 4.0,
            checksum: 0.0,
        };
        let cold = BenchmarkResult {
            name: "cold",
            chunk_count: 1,
            total_ms: 8.0,
            mean_ms: 8.0,
            median_ms: 8.0,
            p95_ms: 8.0,
            min_ms: 8.0,
            max_ms: 8.0,
            checksum: 0.0,
        };
        let prepared = BenchmarkResult {
            name: "prepared",
            chunk_count: 1,
            total_ms: 2.0,
            mean_ms: 2.0,
            median_ms: 2.0,
            p95_ms: 2.0,
            min_ms: 2.0,
            max_ms: 2.0,
            checksum: 0.0,
        };
        let estimate = phase_estimate(&apron, &cold, &prepared);

        assert_eq!(estimate.median_apron_density_share_of_mesh, 0.5);
        assert_eq!(estimate.mean_prepared_mesh_share_of_cold_mesh, 0.25);
        assert_eq!(ratio(1.0, 0.0), 0.0);
        assert_eq!(percentile(&[1.0, 2.0, 3.0, 4.0], 0.5), 2.0);

        let stats = store_stats_report(DensityStoreStats {
            entries: 1,
            max_entries: 2,
            reuses: 3,
            generations: 4,
            evictions: 5,
        });
        assert_eq!(stats.entries, 1);
        assert_eq!(stats.max_entries, 2);
        assert_eq!(stats.reuses, 3);
        assert_eq!(stats.generations, 4);
        assert_eq!(stats.evictions, 5);
    }
}
