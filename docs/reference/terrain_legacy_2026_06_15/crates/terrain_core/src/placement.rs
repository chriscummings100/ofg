// Rust-owned terrain placement sampling built on exact polygonized surface
// queries.

use crate::*;

const DEFAULT_CANDIDATE_GRID_AXIS: u16 = 8;
const MAX_CANDIDATE_GRID_AXIS: u16 = 128;
const DEFAULT_MIN_NORMAL_Y: f64 = 0.45;
const DEFAULT_QUERY_MIN_Y: f64 = -100_000.0;
const DEFAULT_QUERY_MAX_Y: f64 = 100_000.0;
const STABLE_ID_OFFSET_BASIS: u64 = 0xCBF2_9CE4_8422_2325;
const STABLE_ID_PRIME: u64 = 0x0000_0100_0000_01B3;

/// One accepted mesh-backed placement sample for future foliage or props.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainPlacementSample {
    pub stable_id: u64,
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub material_indices: [u8; 4],
    pub material_weights: [f32; 4],
}

/// Filters and deterministic candidate settings for terrain placement sampling.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainPlacementSamplingConfig {
    pub candidate_grid_axis: u16,
    pub min_y: f64,
    pub max_y: f64,
    pub sea_level_meters: f64,
    pub min_normal_y: f64,
}

/// A compact placement sample packet plus counters for rejected candidates.
#[derive(Clone, Debug, PartialEq)]
pub struct TerrainPlacementSamplePacket {
    pub node_key: TerrainNodeKey,
    pub candidate_count: usize,
    pub accepted_count: usize,
    pub missed_surface_count: usize,
    pub rejected_below_water_count: usize,
    pub rejected_slope_count: usize,
    pub samples: Vec<TerrainPlacementSample>,
}

impl Default for TerrainPlacementSamplingConfig {
    /// Returns conservative default filters for early foliage-adjacent surface samples.
    fn default() -> Self {
        Self {
            candidate_grid_axis: DEFAULT_CANDIDATE_GRID_AXIS,
            min_y: DEFAULT_QUERY_MIN_Y,
            max_y: DEFAULT_QUERY_MAX_Y,
            sea_level_meters: SEA_LEVEL_METERS,
            min_normal_y: DEFAULT_MIN_NORMAL_Y,
        }
    }
}

/// Builds a node mesh, indexes its exact triangles, and samples placement points.
pub fn build_node_surface_placement_samples_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> TerrainPlacementSamplePacket {
    build_node_surface_placement_samples_for_variant_with_config(
        seed,
        descriptor,
        key,
        base_cell_size,
        TerrainPlacementSamplingConfig::default(),
    )
}

/// Builds a node mesh and samples placement points with caller-provided filters.
pub fn build_node_surface_placement_samples_for_variant_with_config(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    key: TerrainNodeKey,
    base_cell_size: f64,
    config: TerrainPlacementSamplingConfig,
) -> TerrainPlacementSamplePacket {
    let candidates = terrain_placement_candidates_for_node(
        seed,
        key,
        base_cell_size,
        config.candidate_grid_axis,
    );
    let candidate_count = candidates.len();
    let output = build_node_mesh_and_surface_for_variant(seed, descriptor, key, base_cell_size);
    let Some(surface) = output.surface.as_ref() else {
        return TerrainPlacementSamplePacket::empty(key, candidate_count);
    };

    sample_terrain_placements_from_candidates(seed, surface, &candidates, config)
}

/// Generates deterministic node-local candidate XZ points inside half-open node bounds.
pub fn terrain_placement_candidates_for_node(
    seed: u32,
    key: TerrainNodeKey,
    base_cell_size: f64,
    candidate_grid_axis: u16,
) -> Vec<[f64; 2]> {
    if candidate_grid_axis == 0 || candidate_grid_axis > MAX_CANDIDATE_GRID_AXIS {
        return Vec::new();
    }
    let Some((origin_x, origin_z, node_span)) = node_xz_bounds(key, base_cell_size) else {
        return Vec::new();
    };

    let axis = usize::from(candidate_grid_axis);
    let candidate_cell_span = node_span / f64::from(candidate_grid_axis);
    let mut candidates = Vec::with_capacity(axis * axis);
    for row in 0..candidate_grid_axis {
        for column in 0..candidate_grid_axis {
            let jitter_x = hash01(
                key.coord.x.wrapping_add(i32::from(column)),
                key.coord.z.wrapping_add(i32::from(row)),
                seed ^ u32::from(key.lod),
                0x7A1D_43B1,
            );
            let jitter_z = hash01(
                key.coord.x.wrapping_add(i32::from(column)),
                key.coord.z.wrapping_add(i32::from(row)),
                seed ^ u32::from(key.lod),
                0xB529_7A4D,
            );
            candidates.push([
                origin_x + (f64::from(column) + 0.25 + jitter_x * 0.5) * candidate_cell_span,
                origin_z + (f64::from(row) + 0.25 + jitter_z * 0.5) * candidate_cell_span,
            ]);
        }
    }

    candidates
}

/// Queries exact terrain triangles for candidate XZ points and returns accepted samples.
pub fn sample_terrain_placements_from_candidates(
    seed: u32,
    surface: &TerrainSurfaceIndex,
    candidates: &[[f64; 2]],
    config: TerrainPlacementSamplingConfig,
) -> TerrainPlacementSamplePacket {
    let key = surface.node_key();
    if !placement_config_is_valid(config) {
        return TerrainPlacementSamplePacket::empty(key, candidates.len());
    }

    let mut packet = TerrainPlacementSamplePacket::empty(key, candidates.len());
    for candidate in candidates {
        let Some(hit) = surface.highest_vertical_hit(TerrainVerticalQuery {
            x: candidate[0],
            z: candidate[1],
            min_y: config.min_y,
            max_y: config.max_y,
            min_normal_y: -1.0,
        }) else {
            packet.missed_surface_count += 1;
            continue;
        };

        if hit.position[1] < config.sea_level_meters {
            packet.rejected_below_water_count += 1;
            continue;
        }
        if f64::from(hit.shading_normal[1]) < config.min_normal_y {
            packet.rejected_slope_count += 1;
            continue;
        }

        packet.samples.push(TerrainPlacementSample {
            stable_id: stable_sample_id(seed, key, hit.triangle_index, *candidate),
            position: [
                hit.position[0] as f32,
                hit.position[1] as f32,
                hit.position[2] as f32,
            ],
            normal: hit.shading_normal,
            material_indices: hit.material_indices,
            material_weights: hit.material_weights,
        });
    }

    packet.accepted_count = packet.samples.len();
    packet
}

impl TerrainPlacementSamplePacket {
    /// Creates an empty packet with a known candidate count.
    fn empty(node_key: TerrainNodeKey, candidate_count: usize) -> Self {
        Self {
            node_key,
            candidate_count,
            accepted_count: 0,
            missed_surface_count: 0,
            rejected_below_water_count: 0,
            rejected_slope_count: 0,
            samples: Vec::new(),
        }
    }
}

fn placement_config_is_valid(config: TerrainPlacementSamplingConfig) -> bool {
    config.candidate_grid_axis <= MAX_CANDIDATE_GRID_AXIS
        && config.min_y.is_finite()
        && config.max_y.is_finite()
        && config.min_y <= config.max_y
        && config.sea_level_meters.is_finite()
        && config.min_normal_y.is_finite()
        && config.min_normal_y >= -1.0
        && config.min_normal_y <= 1.0
}

fn node_xz_bounds(key: TerrainNodeKey, base_cell_size: f64) -> Option<(f64, f64, f64)> {
    if !base_cell_size.is_finite() || base_cell_size <= 0.0 {
        return None;
    }

    let node_cell_size = terrain_node_cell_size(base_cell_size, key.lod);
    let node_span = node_cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    if !node_span.is_finite() || node_span <= 0.0 {
        return None;
    }

    Some((
        key.coord.x as f64 * node_span,
        key.coord.z as f64 * node_span,
        node_span,
    ))
}

fn stable_sample_id(
    seed: u32,
    key: TerrainNodeKey,
    triangle_index: u32,
    candidate: [f64; 2],
) -> u64 {
    [
        u64::from(seed),
        u64::from(key.lod),
        key.coord.x as u32 as u64,
        key.coord.y as u32 as u64,
        key.coord.z as u32 as u64,
        u64::from(triangle_index),
        candidate[0].to_bits(),
        candidate[1].to_bits(),
    ]
    .into_iter()
    .fold(STABLE_ID_OFFSET_BASIS, |hash, value| {
        (hash ^ value).wrapping_mul(STABLE_ID_PRIME)
    })
}
