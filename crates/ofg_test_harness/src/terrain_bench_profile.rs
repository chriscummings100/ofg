// Profiled terrain-node benchmark population and report helpers.
// This module keeps the detailed node distribution work out of the main
// terrain benchmark runner so the benchmark stays readable as the report grows.

use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;
use terrain_core::benchmark::{
    profile_node_mesh_build, reset_density_store, TerrainNodeBuildProfile,
};
use terrain_core::{
    height_at, terrain_chunk_coord_containing_position, terrain_node_key, TerrainChunkCoord,
    TerrainLodBand, TerrainNodeKey, TerrainStreamConfig, TerrainStreamScheduler,
};

const STREAMING_HORIZONTAL_RADIUS: i32 = 1;
const STREAMING_VERTICAL_CHUNK_OFFSETS: [i32; 4] = [-2, -1, 0, 1];
const STREAMING_FAR_VERTICAL_CHUNK_OFFSETS: [i32; 2] = [-1, 0];
const PROFILE_NODE_SAMPLES_PER_LOD_PER_SOURCE: usize = 1;
const PROFILE_SEED_XORS: [u32; 2] = [0, 0x9E37_79B9];

const PROFILE_PRESETS: [ProfilePreset; 4] = [
    ProfilePreset {
        id: "seed",
        code: 0,
    },
    ProfilePreset {
        id: "rollingHills",
        code: 1,
    },
    ProfilePreset {
        id: "mountainValley",
        code: 2,
    },
    ProfilePreset {
        id: "rockyHighland",
        code: 3,
    },
];

const PROFILE_MOVEMENT_POINTS: [ProfilePoint; 4] = [
    ProfilePoint {
        id: "initial-settle",
        x: 0.0,
        z: 0.0,
    },
    ProfilePoint {
        id: "run-east-96m",
        x: 96.0,
        z: 0.0,
    },
    ProfilePoint {
        id: "run-northeast-192m",
        x: 192.0,
        z: 64.0,
    },
    ProfilePoint {
        id: "run-far-320m",
        x: 320.0,
        z: 128.0,
    },
];

#[derive(Clone, Copy)]
struct ProfilePreset {
    id: &'static str,
    code: u32,
}

#[derive(Clone, Copy)]
struct ProfilePoint {
    id: &'static str,
    x: f32,
    z: f32,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodeProfileScenario {
    pub(crate) seed: u32,
    pub(crate) preset: &'static str,
    pub(crate) preset_code: u32,
    pub(crate) source: &'static str,
    pub(crate) key: NodeReport,
    pub(crate) base_cell_size: f64,
}

#[derive(Clone, Copy, Serialize)]
pub(crate) struct NodeReport {
    pub(crate) lod: u8,
    pub(crate) x: i32,
    pub(crate) y: i32,
    pub(crate) z: i32,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct DurationDistributionReport {
    pub(crate) sample_count: usize,
    pub(crate) mean_ms: f64,
    pub(crate) median_ms: f64,
    pub(crate) p95_ms: f64,
    pub(crate) min_ms: f64,
    pub(crate) max_ms: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct ProfilePhaseDistributionReport {
    pub(crate) total: DurationDistributionReport,
    pub(crate) density: DurationDistributionReport,
    pub(crate) contouring: DurationDistributionReport,
    pub(crate) material: DurationDistributionReport,
    pub(crate) copy: DurationDistributionReport,
    pub(crate) mean_density_share_of_total: f64,
    pub(crate) mean_contouring_share_of_total: f64,
    pub(crate) mean_material_share_of_total: f64,
    pub(crate) mean_copy_share_of_total: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodeClassCountReport {
    pub(crate) class: &'static str,
    pub(crate) sample_count: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodeLodProfileReport {
    pub(crate) lod: u8,
    pub(crate) sample_count: usize,
    pub(crate) empty_node_count: usize,
    pub(crate) solid_node_count: usize,
    pub(crate) surface_node_count: usize,
    pub(crate) vertex_count_mean: f64,
    pub(crate) index_count_mean: f64,
    pub(crate) generated_density_chunk_mean: f64,
    pub(crate) byte_count_mean: f64,
    pub(crate) timings: ProfilePhaseDistributionReport,
    pub(crate) prepared_timings: ProfilePhaseDistributionReport,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodeClassProfileReport {
    pub(crate) class: &'static str,
    pub(crate) sample_count: usize,
    pub(crate) lod_count: usize,
    pub(crate) vertex_count_mean: f64,
    pub(crate) index_count_mean: f64,
    pub(crate) generated_density_chunk_mean: f64,
    pub(crate) timings: ProfilePhaseDistributionReport,
    pub(crate) prepared_timings: ProfilePhaseDistributionReport,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodeProfileReport {
    pub(crate) seed: u32,
    pub(crate) preset: &'static str,
    pub(crate) preset_code: u32,
    pub(crate) source: &'static str,
    pub(crate) key: NodeReport,
    pub(crate) key_text: String,
    pub(crate) cell_size: f64,
    pub(crate) class: &'static str,
    pub(crate) total_ms: f64,
    pub(crate) density_ms: f64,
    pub(crate) contouring_ms: f64,
    pub(crate) material_ms: f64,
    pub(crate) copy_ms: f64,
    pub(crate) prepared_total_ms: f64,
    pub(crate) prepared_density_ms: f64,
    pub(crate) prepared_contouring_ms: f64,
    pub(crate) prepared_material_ms: f64,
    pub(crate) prepared_copy_ms: f64,
    pub(crate) reused_density_chunks: u64,
    pub(crate) generated_density_chunks: u64,
    pub(crate) evicted_density_chunks: u64,
    pub(crate) prepared_reused_density_chunks: u64,
    pub(crate) prepared_generated_density_chunks: u64,
    pub(crate) prepared_evicted_density_chunks: u64,
    pub(crate) raw_vertex_count: usize,
    pub(crate) raw_index_count: usize,
    pub(crate) vertex_count: usize,
    pub(crate) index_count: usize,
    pub(crate) vertex_bytes: usize,
    pub(crate) index_bytes: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct TerrainNodePopulationProfileReport {
    pub(crate) sample_count: usize,
    pub(crate) unique_key_count: usize,
    pub(crate) seed_count: usize,
    pub(crate) preset_count: usize,
    pub(crate) source_count: usize,
    pub(crate) lod_count: usize,
    pub(crate) empty_node_count: usize,
    pub(crate) solid_node_count: usize,
    pub(crate) surface_node_count: usize,
    pub(crate) total_vertex_count: usize,
    pub(crate) total_index_count: usize,
    pub(crate) total_vertex_bytes: usize,
    pub(crate) total_index_bytes: usize,
    pub(crate) class_counts: Vec<TerrainNodeClassCountReport>,
    pub(crate) by_lod: Vec<TerrainNodeLodProfileReport>,
    pub(crate) by_class: Vec<TerrainNodeClassProfileReport>,
    pub(crate) timings: ProfilePhaseDistributionReport,
    pub(crate) prepared_timings: ProfilePhaseDistributionReport,
    pub(crate) profiles: Vec<TerrainNodeProfileReport>,
    pub(crate) scenarios: Vec<TerrainNodeProfileScenario>,
}

/// Builds and profiles a representative terrain node population.
pub(crate) fn run_profiled_node_population(
    seed: u32,
    cell_size: f64,
) -> TerrainNodePopulationProfileReport {
    let scenarios = build_profile_scenarios(seed, cell_size);
    profile_terrain_node_population(&scenarios)
}

/// Returns the number of node samples the profiled population will contain.
pub(crate) fn profile_scenario_count(seed: u32, cell_size: f64) -> usize {
    build_profile_scenarios(seed, cell_size).len()
}

/// Builds representative node-profile inputs from stream centers and class probes.
fn build_profile_scenarios(seed: u32, cell_size: f64) -> Vec<TerrainNodeProfileScenario> {
    let mut scenarios = Vec::new();
    let mut seen = BTreeSet::new();

    for profile_seed in profile_seeds(seed) {
        for preset in PROFILE_PRESETS {
            collect_stream_profile_scenarios(
                profile_seed,
                preset,
                cell_size,
                &mut scenarios,
                &mut seen,
            );
            collect_class_probe_profile_scenarios(
                profile_seed,
                preset,
                cell_size,
                &mut scenarios,
                &mut seen,
            );
        }
    }

    scenarios
}

/// Returns deterministic seed variants for the profile population.
fn profile_seeds(seed: u32) -> Vec<u32> {
    PROFILE_SEED_XORS
        .into_iter()
        .map(|xor| seed ^ xor)
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect()
}

/// Adds sampled nodes from the configured streaming bands during movement.
fn collect_stream_profile_scenarios(
    seed: u32,
    preset: ProfilePreset,
    cell_size: f64,
    scenarios: &mut Vec<TerrainNodeProfileScenario>,
    seen: &mut BTreeSet<(u32, u32, &'static str, u8, i32, i32, i32)>,
) {
    let mut scheduler = TerrainStreamScheduler::new(TerrainStreamConfig {
        lod_bands: benchmark_lod_bands(),
        max_in_flight_jobs: 6,
    })
    .expect("benchmark LOD bands should be valid");

    for (index, point) in PROFILE_MOVEMENT_POINTS.iter().enumerate() {
        let height = height_at(seed, preset.code, f64::from(point.x), f64::from(point.z)) as f32;
        let center = terrain_chunk_coord_containing_position(point.x, height, point.z, cell_size);
        if index == 0 {
            scheduler.reset(center);
        } else {
            scheduler.sync_center(center);
        }
        let desired = scheduler.desired_mesh_nodes();
        for key in sampled_nodes_by_lod(&desired, PROFILE_NODE_SAMPLES_PER_LOD_PER_SOURCE) {
            push_profile_scenario(seed, preset, point.id, key, cell_size, scenarios, seen);
        }
    }
}

/// Adds explicit high-air, low-solid, and surface probes for class coverage.
fn collect_class_probe_profile_scenarios(
    seed: u32,
    preset: ProfilePreset,
    cell_size: f64,
    scenarios: &mut Vec<TerrainNodeProfileScenario>,
    seen: &mut BTreeSet<(u32, u32, &'static str, u8, i32, i32, i32)>,
) {
    let surface_y = height_at(seed, preset.code, 0.0, 0.0) as f32;
    let surface_coord = terrain_chunk_coord_containing_position(0.0, surface_y, 0.0, cell_size);
    let probes = [
        (
            "class-air",
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: 10, z: 0 },
            },
        ),
        (
            "class-solid",
            TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord { x: 0, y: -10, z: 0 },
            },
        ),
        (
            "class-surface",
            TerrainNodeKey {
                lod: 0,
                coord: surface_coord,
            },
        ),
        (
            "class-coarse-air",
            TerrainNodeKey {
                lod: 4,
                coord: TerrainChunkCoord { x: 0, y: 3, z: 0 },
            },
        ),
        (
            "class-coarse-solid",
            TerrainNodeKey {
                lod: 4,
                coord: TerrainChunkCoord { x: 0, y: -3, z: 0 },
            },
        ),
    ];

    for (source, key) in probes {
        push_profile_scenario(seed, preset, source, key, cell_size, scenarios, seen);
    }
}

/// Returns the benchmark's copy of the current default LOD bands.
fn benchmark_lod_bands() -> Vec<TerrainLodBand> {
    vec![
        TerrainLodBand {
            lod: 0,
            horizontal_radius: STREAMING_HORIZONTAL_RADIUS,
            vertical_chunk_offsets: STREAMING_VERTICAL_CHUNK_OFFSETS.to_vec(),
        },
        TerrainLodBand {
            lod: 1,
            horizontal_radius: 2,
            vertical_chunk_offsets: STREAMING_VERTICAL_CHUNK_OFFSETS.to_vec(),
        },
        TerrainLodBand {
            lod: 2,
            horizontal_radius: 3,
            vertical_chunk_offsets: STREAMING_VERTICAL_CHUNK_OFFSETS.to_vec(),
        },
        TerrainLodBand {
            lod: 3,
            horizontal_radius: 2,
            vertical_chunk_offsets: STREAMING_FAR_VERTICAL_CHUNK_OFFSETS.to_vec(),
        },
        TerrainLodBand {
            lod: 4,
            horizontal_radius: 4,
            vertical_chunk_offsets: STREAMING_FAR_VERTICAL_CHUNK_OFFSETS.to_vec(),
        },
    ]
}

/// Picks deterministic center/median/far samples for each LOD.
fn sampled_nodes_by_lod(nodes: &[TerrainNodeKey], per_lod: usize) -> Vec<TerrainNodeKey> {
    let mut by_lod: BTreeMap<u8, Vec<TerrainNodeKey>> = BTreeMap::new();
    for key in nodes {
        by_lod.entry(key.lod).or_default().push(*key);
    }

    let mut sampled = Vec::new();
    for keys in by_lod.values_mut() {
        keys.sort_by_key(|key| {
            (
                key.coord.x.abs() + key.coord.y.abs() + key.coord.z.abs(),
                key.coord.x,
                key.coord.y,
                key.coord.z,
            )
        });
        let indexes = sample_indexes(keys.len(), per_lod);
        for index in indexes {
            sampled.push(keys[index]);
        }
    }

    sampled
}

/// Returns spread-out indexes across a sorted sample population.
fn sample_indexes(len: usize, count: usize) -> Vec<usize> {
    if len == 0 || count == 0 {
        return Vec::new();
    }

    if len <= count {
        return (0..len).collect();
    }

    let mut indexes = BTreeSet::new();
    indexes.insert(0);
    indexes.insert(len / 2);
    indexes.insert(len - 1);
    let mut cursor = 1;
    while indexes.len() < count {
        indexes.insert(cursor * len / count);
        cursor += 1;
    }

    indexes.into_iter().take(count).collect()
}

/// Adds a profile scenario while preserving deterministic de-duplication.
fn push_profile_scenario(
    seed: u32,
    preset: ProfilePreset,
    source: &'static str,
    key: TerrainNodeKey,
    cell_size: f64,
    scenarios: &mut Vec<TerrainNodeProfileScenario>,
    seen: &mut BTreeSet<(u32, u32, &'static str, u8, i32, i32, i32)>,
) {
    if !seen.insert((
        seed,
        preset.code,
        source,
        key.lod,
        key.coord.x,
        key.coord.y,
        key.coord.z,
    )) {
        return;
    }

    scenarios.push(TerrainNodeProfileScenario {
        seed,
        preset: preset.id,
        preset_code: preset.code,
        source,
        key: node_report(key),
        base_cell_size: cell_size,
    });
}

/// Profiles one realistic terrain node population and summarizes timing distributions.
fn profile_terrain_node_population(
    scenarios: &[TerrainNodeProfileScenario],
) -> TerrainNodePopulationProfileReport {
    let mut profiles = Vec::with_capacity(scenarios.len());

    for scenario in scenarios {
        reset_density_store();
        let profile = profile_node_mesh_build(
            scenario.seed,
            scenario.preset_code,
            terrain_node(scenario.key),
            scenario.base_cell_size,
        );
        profiles.push(profile_report(scenario, profile));
    }

    let mut key_set = BTreeSet::new();
    let mut seeds = BTreeSet::new();
    let mut presets = BTreeSet::new();
    let mut sources = BTreeSet::new();
    let mut lods = BTreeSet::new();
    let mut class_counts = BTreeMap::<&'static str, usize>::new();
    let mut total_vertex_count = 0;
    let mut total_index_count = 0;
    let mut total_vertex_bytes = 0;
    let mut total_index_bytes = 0;

    for profile in &profiles {
        key_set.insert((profile.key.lod, profile.key.x, profile.key.y, profile.key.z));
        seeds.insert(profile.seed);
        presets.insert(profile.preset_code);
        sources.insert(profile.source);
        lods.insert(profile.key.lod);
        *class_counts.entry(profile.class).or_default() += 1;
        total_vertex_count += profile.vertex_count;
        total_index_count += profile.index_count;
        total_vertex_bytes += profile.vertex_bytes;
        total_index_bytes += profile.index_bytes;
    }

    TerrainNodePopulationProfileReport {
        sample_count: profiles.len(),
        unique_key_count: key_set.len(),
        seed_count: seeds.len(),
        preset_count: presets.len(),
        source_count: sources.len(),
        lod_count: lods.len(),
        empty_node_count: profiles
            .iter()
            .filter(|profile| is_empty_class(profile.class))
            .count(),
        solid_node_count: profiles
            .iter()
            .filter(|profile| profile.class == "solid")
            .count(),
        surface_node_count: profiles
            .iter()
            .filter(|profile| is_surface_class(profile.class))
            .count(),
        total_vertex_count,
        total_index_count,
        total_vertex_bytes,
        total_index_bytes,
        class_counts: class_counts
            .into_iter()
            .map(|(class, sample_count)| TerrainNodeClassCountReport {
                class,
                sample_count,
            })
            .collect(),
        by_lod: profile_reports_by_lod(&profiles),
        by_class: profile_reports_by_class(&profiles),
        timings: profile_phase_distribution(&profiles),
        prepared_timings: prepared_profile_phase_distribution(&profiles),
        profiles,
        scenarios: scenarios.to_vec(),
    }
}

/// Converts one Rust profile into a serializable benchmark record.
fn profile_report(
    scenario: &TerrainNodeProfileScenario,
    profile: TerrainNodeBuildProfile,
) -> TerrainNodeProfileReport {
    let key = terrain_node(scenario.key);
    TerrainNodeProfileReport {
        seed: scenario.seed,
        preset: scenario.preset,
        preset_code: scenario.preset_code,
        source: scenario.source,
        key: scenario.key,
        key_text: terrain_node_key(key),
        cell_size: profile.cell_size,
        class: profile.class.as_str(),
        total_ms: profile.total_ms,
        density_ms: profile.density_ms,
        contouring_ms: profile.contouring_ms,
        material_ms: profile.material_ms,
        copy_ms: profile.copy_ms,
        prepared_total_ms: profile.prepared_total_ms,
        prepared_density_ms: profile.prepared_density_ms,
        prepared_contouring_ms: profile.prepared_contouring_ms,
        prepared_material_ms: profile.prepared_material_ms,
        prepared_copy_ms: profile.prepared_copy_ms,
        reused_density_chunks: profile.reused_density_chunks,
        generated_density_chunks: profile.generated_density_chunks,
        evicted_density_chunks: profile.evicted_density_chunks,
        prepared_reused_density_chunks: profile.prepared_reused_density_chunks,
        prepared_generated_density_chunks: profile.prepared_generated_density_chunks,
        prepared_evicted_density_chunks: profile.prepared_evicted_density_chunks,
        raw_vertex_count: profile.raw_vertex_count,
        raw_index_count: profile.raw_index_count,
        vertex_count: profile.vertex_count,
        index_count: profile.index_count,
        vertex_bytes: profile.vertex_bytes,
        index_bytes: profile.index_bytes,
    }
}

/// Groups node profile reports by LOD.
fn profile_reports_by_lod(
    profiles: &[TerrainNodeProfileReport],
) -> Vec<TerrainNodeLodProfileReport> {
    let mut groups = BTreeMap::<u8, Vec<TerrainNodeProfileReport>>::new();
    for profile in profiles {
        groups
            .entry(profile.key.lod)
            .or_default()
            .push(profile.clone());
    }

    groups
        .into_iter()
        .map(|(lod, profiles)| TerrainNodeLodProfileReport {
            lod,
            sample_count: profiles.len(),
            empty_node_count: profiles
                .iter()
                .filter(|profile| is_empty_class(profile.class))
                .count(),
            solid_node_count: profiles
                .iter()
                .filter(|profile| profile.class == "solid")
                .count(),
            surface_node_count: profiles
                .iter()
                .filter(|profile| is_surface_class(profile.class))
                .count(),
            vertex_count_mean: mean_usize(profiles.iter().map(|profile| profile.vertex_count)),
            index_count_mean: mean_usize(profiles.iter().map(|profile| profile.index_count)),
            generated_density_chunk_mean: mean_u64(
                profiles
                    .iter()
                    .map(|profile| profile.generated_density_chunks),
            ),
            byte_count_mean: mean_usize(
                profiles
                    .iter()
                    .map(|profile| profile.vertex_bytes + profile.index_bytes),
            ),
            timings: profile_phase_distribution(&profiles),
            prepared_timings: prepared_profile_phase_distribution(&profiles),
        })
        .collect()
}

/// Groups node profile reports by terrain class.
fn profile_reports_by_class(
    profiles: &[TerrainNodeProfileReport],
) -> Vec<TerrainNodeClassProfileReport> {
    let mut groups = BTreeMap::<&'static str, Vec<TerrainNodeProfileReport>>::new();
    for profile in profiles {
        groups
            .entry(profile.class)
            .or_default()
            .push(profile.clone());
    }

    groups
        .into_iter()
        .map(|(class, profiles)| {
            let lod_count = profiles
                .iter()
                .map(|profile| profile.key.lod)
                .collect::<BTreeSet<_>>()
                .len();
            TerrainNodeClassProfileReport {
                class,
                sample_count: profiles.len(),
                lod_count,
                vertex_count_mean: mean_usize(profiles.iter().map(|profile| profile.vertex_count)),
                index_count_mean: mean_usize(profiles.iter().map(|profile| profile.index_count)),
                generated_density_chunk_mean: mean_u64(
                    profiles
                        .iter()
                        .map(|profile| profile.generated_density_chunks),
                ),
                timings: profile_phase_distribution(&profiles),
                prepared_timings: prepared_profile_phase_distribution(&profiles),
            }
        })
        .collect()
}

/// Computes phase timing distributions for a profile group.
fn profile_phase_distribution(
    profiles: &[TerrainNodeProfileReport],
) -> ProfilePhaseDistributionReport {
    let total = duration_distribution(profiles.iter().map(|profile| profile.total_ms));
    ProfilePhaseDistributionReport {
        density: duration_distribution(profiles.iter().map(|profile| profile.density_ms)),
        contouring: duration_distribution(profiles.iter().map(|profile| profile.contouring_ms)),
        material: duration_distribution(profiles.iter().map(|profile| profile.material_ms)),
        copy: duration_distribution(profiles.iter().map(|profile| profile.copy_ms)),
        mean_density_share_of_total: mean_phase_share(profiles, |profile| profile.density_ms),
        mean_contouring_share_of_total: mean_phase_share(profiles, |profile| profile.contouring_ms),
        mean_material_share_of_total: mean_phase_share(profiles, |profile| profile.material_ms),
        mean_copy_share_of_total: mean_phase_share(profiles, |profile| profile.copy_ms),
        total,
    }
}

/// Computes prepared-density phase timing distributions for a profile group.
fn prepared_profile_phase_distribution(
    profiles: &[TerrainNodeProfileReport],
) -> ProfilePhaseDistributionReport {
    let total = duration_distribution(profiles.iter().map(|profile| profile.prepared_total_ms));
    ProfilePhaseDistributionReport {
        density: duration_distribution(profiles.iter().map(|profile| profile.prepared_density_ms)),
        contouring: duration_distribution(
            profiles
                .iter()
                .map(|profile| profile.prepared_contouring_ms),
        ),
        material: duration_distribution(
            profiles.iter().map(|profile| profile.prepared_material_ms),
        ),
        copy: duration_distribution(profiles.iter().map(|profile| profile.prepared_copy_ms)),
        mean_density_share_of_total: mean_prepared_phase_share(profiles, |profile| {
            profile.prepared_density_ms
        }),
        mean_contouring_share_of_total: mean_prepared_phase_share(profiles, |profile| {
            profile.prepared_contouring_ms
        }),
        mean_material_share_of_total: mean_prepared_phase_share(profiles, |profile| {
            profile.prepared_material_ms
        }),
        mean_copy_share_of_total: mean_prepared_phase_share(profiles, |profile| {
            profile.prepared_copy_ms
        }),
        total,
    }
}

/// Computes a timing distribution from arbitrary duration samples.
fn duration_distribution(samples: impl Iterator<Item = f64>) -> DurationDistributionReport {
    let mut values = samples.collect::<Vec<_>>();
    if values.is_empty() {
        return DurationDistributionReport {
            sample_count: 0,
            mean_ms: 0.0,
            median_ms: 0.0,
            p95_ms: 0.0,
            min_ms: 0.0,
            max_ms: 0.0,
        };
    }

    values.sort_by(|left, right| left.total_cmp(right));
    let sum = values.iter().sum::<f64>();
    DurationDistributionReport {
        sample_count: values.len(),
        mean_ms: sum / values.len() as f64,
        median_ms: percentile(&values, 0.5),
        p95_ms: percentile(&values, 0.95),
        min_ms: values[0],
        max_ms: values[values.len() - 1],
    }
}

/// Returns a percentile from a sorted duration list.
fn percentile(sorted: &[f64], fraction: f64) -> f64 {
    let index = ((sorted.len() as f64 * fraction).ceil() as usize)
        .saturating_sub(1)
        .min(sorted.len() - 1);
    sorted[index]
}

/// Returns the mean per-profile share for one phase.
fn mean_phase_share(
    profiles: &[TerrainNodeProfileReport],
    phase: impl Fn(&TerrainNodeProfileReport) -> f64,
) -> f64 {
    mean_phase_share_with_total(profiles, phase, |profile| profile.total_ms)
}

/// Returns the mean per-profile prepared share for one phase.
fn mean_prepared_phase_share(
    profiles: &[TerrainNodeProfileReport],
    phase: impl Fn(&TerrainNodeProfileReport) -> f64,
) -> f64 {
    mean_phase_share_with_total(profiles, phase, |profile| profile.prepared_total_ms)
}

/// Returns the mean per-profile share for one phase with a caller-provided total.
fn mean_phase_share_with_total(
    profiles: &[TerrainNodeProfileReport],
    phase: impl Fn(&TerrainNodeProfileReport) -> f64,
    total: impl Fn(&TerrainNodeProfileReport) -> f64,
) -> f64 {
    if profiles.is_empty() {
        return 0.0;
    }

    profiles
        .iter()
        .map(|profile| ratio(phase(profile), total(profile)))
        .sum::<f64>()
        / profiles.len() as f64
}

/// Returns a ratio that stays finite for empty timings.
fn ratio(numerator: f64, denominator: f64) -> f64 {
    if denominator <= 0.0 {
        0.0
    } else {
        numerator / denominator
    }
}

/// Returns whether a terrain class has no rendered mesh.
fn is_empty_class(class: &'static str) -> bool {
    class == "emptyAir" || class == "solid"
}

/// Returns whether a terrain class produced surface triangles.
fn is_surface_class(class: &'static str) -> bool {
    matches!(class, "surfaceSparse" | "surfaceHeavy" | "surfaceComplex")
}

/// Returns the mean of usize samples.
fn mean_usize(samples: impl Iterator<Item = usize>) -> f64 {
    mean_f64(samples.map(|sample| sample as f64))
}

/// Returns the mean of u64 samples.
fn mean_u64(samples: impl Iterator<Item = u64>) -> f64 {
    mean_f64(samples.map(|sample| sample as f64))
}

/// Returns the mean of f64 samples.
fn mean_f64(samples: impl Iterator<Item = f64>) -> f64 {
    let values = samples.collect::<Vec<_>>();
    if values.is_empty() {
        return 0.0;
    }

    values.iter().sum::<f64>() / values.len() as f64
}

/// Converts a terrain node into a serializable report value.
fn node_report(key: TerrainNodeKey) -> NodeReport {
    NodeReport {
        lod: key.lod,
        x: key.coord.x,
        y: key.coord.y,
        z: key.coord.z,
    }
}

/// Converts a serializable report value back to a terrain node key.
fn terrain_node(key: NodeReport) -> TerrainNodeKey {
    TerrainNodeKey {
        lod: key.lod,
        coord: TerrainChunkCoord {
            x: key.x,
            y: key.y,
            z: key.z,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_profile_scenarios_from_streaming_and_class_probes() {
        let scenarios = build_profile_scenarios(0x0F6, 1.0);
        let seeds = scenarios
            .iter()
            .map(|scenario| scenario.seed)
            .collect::<BTreeSet<_>>();
        let lods = scenarios
            .iter()
            .map(|scenario| scenario.key.lod)
            .collect::<BTreeSet<_>>();
        let sources = scenarios
            .iter()
            .map(|scenario| scenario.source)
            .collect::<BTreeSet<_>>();

        assert!(scenarios.len() > PROFILE_PRESETS.len() * PROFILE_MOVEMENT_POINTS.len());
        assert_eq!(seeds.len(), 2);
        assert!(lods.contains(&0));
        assert!(lods.contains(&1));
        assert!(lods.contains(&2));
        assert!(lods.contains(&3));
        assert!(lods.contains(&4));
        assert!(sources.contains("initial-settle"));
        assert!(sources.contains("class-air"));
        assert!(sources.contains("class-solid"));
    }

    #[test]
    fn profile_distribution_reports_classes_lods_and_phase_shares() {
        let profiles = vec![
            fake_profile(0, "emptyAir", 10.0, 4.0, 3.0, 2.0, 1.0),
            fake_profile(1, "surfaceHeavy", 20.0, 8.0, 6.0, 4.0, 2.0),
        ];

        let timings = profile_phase_distribution(&profiles);
        let prepared_timings = prepared_profile_phase_distribution(&profiles);
        let by_lod = profile_reports_by_lod(&profiles);
        let by_class = profile_reports_by_class(&profiles);

        assert_eq!(timings.total.sample_count, 2);
        assert_eq!(timings.total.median_ms, 10.0);
        assert_eq!(timings.total.p95_ms, 20.0);
        assert_eq!(timings.mean_density_share_of_total, 0.4);
        assert_eq!(prepared_timings.total.median_ms, 5.0);
        assert_eq!(prepared_timings.mean_density_share_of_total, 0.4);
        assert_eq!(by_lod.len(), 2);
        assert_eq!(by_class.len(), 2);
        assert_eq!(by_lod[0].empty_node_count, 1);
        assert_eq!(by_lod[1].surface_node_count, 1);
    }

    fn fake_profile(
        lod: u8,
        class: &'static str,
        total_ms: f64,
        density_ms: f64,
        contouring_ms: f64,
        material_ms: f64,
        copy_ms: f64,
    ) -> TerrainNodeProfileReport {
        TerrainNodeProfileReport {
            seed: 0x0F6,
            preset: "unit",
            preset_code: 1,
            source: "unit",
            key: NodeReport {
                lod,
                x: i32::from(lod),
                y: 0,
                z: 0,
            },
            key_text: format!("lod{lod}:0,0,0"),
            cell_size: 1.0,
            class,
            total_ms,
            density_ms,
            contouring_ms,
            material_ms,
            copy_ms,
            prepared_total_ms: total_ms / 2.0,
            prepared_density_ms: density_ms / 2.0,
            prepared_contouring_ms: contouring_ms / 2.0,
            prepared_material_ms: material_ms / 2.0,
            prepared_copy_ms: copy_ms / 2.0,
            reused_density_chunks: 0,
            generated_density_chunks: 8,
            evicted_density_chunks: 0,
            prepared_reused_density_chunks: 8,
            prepared_generated_density_chunks: 0,
            prepared_evicted_density_chunks: 0,
            raw_vertex_count: 1,
            raw_index_count: 3,
            vertex_count: 3,
            index_count: 3,
            vertex_bytes: 3 * 19 * 4,
            index_bytes: 3 * 4,
        }
    }
}
