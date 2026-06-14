// Tests for Rust-owned placement sampling on exact polygonized terrain
// surfaces.

use crate::*;

const TEST_NODE_CELL_SIZE: f64 = 1.0;
const POSITION_EPSILON: f64 = 0.000001;
const WEIGHT_EPSILON: f32 = 0.000001;

#[test]
fn placement_sampler_returns_deterministic_samples_for_same_seed_key() {
    let key = node(0, 0, 0, 0);
    let surface = flat_surface(key, [0.0, 3.0, 0.0], [6.0, 3.0, 0.0], [0.0, 3.0, 6.0]);
    let candidates = [[1.0, 1.0], [2.0, 1.0]];
    let config = permissive_config();

    let first = sample_terrain_placements_from_candidates(0x0F6, &surface, &candidates, config);
    let second = sample_terrain_placements_from_candidates(0x0F6, &surface, &candidates, config);
    let reversed_candidates = [[2.0, 1.0], [1.0, 1.0]];
    let reversed =
        sample_terrain_placements_from_candidates(0x0F6, &surface, &reversed_candidates, config);

    assert_eq!(first, second);
    assert_eq!(first.candidate_count, 2);
    assert_eq!(first.accepted_count, 2);
    assert_eq!(first.samples.len(), 2);
    assert_ne!(first.samples[0].stable_id, first.samples[1].stable_id);
    assert_eq!(first.samples[0].stable_id, reversed.samples[1].stable_id);
    assert_eq!(first.samples[1].stable_id, reversed.samples[0].stable_id);
}

#[test]
fn placement_sampler_assigns_boundary_candidates_to_one_neighbor() {
    let left_key = node(0, 0, 0, 0);
    let right_key = node(0, 1, 0, 0);
    let left_surface = flat_surface(
        left_key,
        [31.0, 2.0, 0.0],
        [32.0, 2.0, 0.0],
        [31.0, 2.0, 4.0],
    );
    let right_surface = flat_surface(
        right_key,
        [32.0, 2.0, 0.0],
        [36.0, 2.0, 0.0],
        [32.0, 2.0, 4.0],
    );
    let candidates = [[32.0, 1.0]];
    let config = permissive_config();

    let left = sample_terrain_placements_from_candidates(0x0F6, &left_surface, &candidates, config);
    let right =
        sample_terrain_placements_from_candidates(0x0F6, &right_surface, &candidates, config);

    assert_eq!(left.accepted_count, 0);
    assert_eq!(left.missed_surface_count, 1);
    assert_eq!(right.accepted_count, 1);
    assert_eq!(right.samples[0].position, [32.0, 2.0, 1.0]);
}

#[test]
fn placement_sampler_rejects_steep_surfaces() {
    let key = node(0, 0, 0, 0);
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 2.0, 0.0], [1.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 2.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 2.0, 4.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );
    let surface = TerrainSurfaceIndex::from_mesh(key, TEST_NODE_CELL_SIZE, &mesh)
        .expect("steep but nondegenerate triangle should be queryable");
    let candidates = [[1.0, 1.0]];

    let packet = sample_terrain_placements_from_candidates(
        0x0F6,
        &surface,
        &candidates,
        TerrainPlacementSamplingConfig {
            min_normal_y: 0.5,
            ..permissive_config()
        },
    );

    assert_eq!(packet.accepted_count, 0);
    assert_eq!(packet.rejected_slope_count, 1);
    assert_eq!(packet.missed_surface_count, 0);
}

#[test]
fn placement_sampler_treats_vertical_projection_as_missed_surface() {
    let key = node(0, 0, 0, 0);
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([1.0, 1.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([2.0, 2.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );

    assert!(TerrainSurfaceIndex::from_mesh(key, TEST_NODE_CELL_SIZE, &mesh).is_none());
}

#[test]
fn placement_sampler_rejects_samples_below_water() {
    let key = node(0, 0, 0, 0);
    let surface = flat_surface(key, [0.0, -2.0, 0.0], [4.0, -2.0, 0.0], [0.0, -2.0, 4.0]);
    let candidates = [[1.0, 1.0]];

    let packet = sample_terrain_placements_from_candidates(
        0x0F6,
        &surface,
        &candidates,
        TerrainPlacementSamplingConfig {
            sea_level_meters: 0.0,
            ..permissive_config()
        },
    );

    assert_eq!(packet.accepted_count, 0);
    assert_eq!(packet.rejected_below_water_count, 1);
}

#[test]
fn placement_sampler_preserves_candidate_count_for_invalid_config() {
    let key = node(0, 0, 0, 0);
    let surface = flat_surface(key, [0.0, 3.0, 0.0], [4.0, 3.0, 0.0], [0.0, 3.0, 4.0]);
    let candidates = [[1.0, 1.0], [2.0, 1.0]];

    let packet = sample_terrain_placements_from_candidates(
        0x0F6,
        &surface,
        &candidates,
        TerrainPlacementSamplingConfig {
            min_y: 10.0,
            max_y: -10.0,
            ..permissive_config()
        },
    );

    assert_eq!(packet.candidate_count, 2);
    assert_eq!(packet.accepted_count, 0);
    assert!(packet.samples.is_empty());
}

#[test]
fn placement_sampler_keeps_exact_hit_payload_from_surface_query() {
    let key = node(0, 0, 0, 0);
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 1.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 5.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 3.0, 4.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );
    let surface = TerrainSurfaceIndex::from_mesh(key, TEST_NODE_CELL_SIZE, &mesh)
        .expect("sloped triangle should be queryable");
    let candidates = [[1.0, 1.0]];
    let hit = surface
        .highest_vertical_hit(TerrainVerticalQuery {
            x: 1.0,
            z: 1.0,
            min_y: -100.0,
            max_y: 100.0,
            min_normal_y: -1.0,
        })
        .expect("candidate should hit surface");

    let packet = sample_terrain_placements_from_candidates(
        0x0F6,
        &surface,
        &candidates,
        permissive_config(),
    );

    assert_eq!(packet.accepted_count, 1);
    let sample = packet.samples[0];
    assert!((f64::from(sample.position[0]) - hit.position[0]).abs() <= POSITION_EPSILON);
    assert!((f64::from(sample.position[1]) - hit.position[1]).abs() <= POSITION_EPSILON);
    assert!((f64::from(sample.position[2]) - hit.position[2]).abs() <= POSITION_EPSILON);
    assert_eq!(sample.normal, hit.shading_normal);
    assert_eq!(sample.material_indices, hit.material_indices);
    assert!((sample.material_weights[0] - hit.material_weights[0]).abs() <= WEIGHT_EPSILON);
    assert!((sample.material_weights[1] - hit.material_weights[1]).abs() <= WEIGHT_EPSILON);
    assert!((sample.material_weights[2] - hit.material_weights[2]).abs() <= WEIGHT_EPSILON);
    assert_eq!(sample.material_weights[3], hit.material_weights[3]);
    assert_ne!(sample.stable_id, 0);
}

#[test]
fn placement_builder_samples_generated_node_deterministically() {
    let _lock = test_lock();
    let key = node(0, 0, 0, 0);
    let config = TerrainPlacementSamplingConfig {
        candidate_grid_axis: 4,
        sea_level_meters: -1_000.0,
        min_normal_y: -1.0,
        ..TerrainPlacementSamplingConfig::default()
    };

    let first = build_node_surface_placement_samples_for_variant_with_config(
        0x0F6,
        terrain_variant_for_preset(1),
        key,
        TEST_NODE_CELL_SIZE,
        config,
    );
    let second = build_node_surface_placement_samples_for_variant_with_config(
        0x0F6,
        terrain_variant_for_preset(1),
        key,
        TEST_NODE_CELL_SIZE,
        config,
    );

    assert_eq!(first, second);
    assert_eq!(first.candidate_count, 16);
    assert!(first.accepted_count > 0);
}

#[test]
fn placement_builder_default_config_samples_generated_node() {
    let _lock = test_lock();
    let key = node(0, 0, 0, 0);

    let packet = build_node_surface_placement_samples_for_variant(
        0x0F6,
        terrain_variant_for_preset(1),
        key,
        TEST_NODE_CELL_SIZE,
    );

    assert_eq!(packet.node_key, key);
    assert_eq!(packet.candidate_count, 64);
    assert_eq!(
        packet.accepted_count
            + packet.missed_surface_count
            + packet.rejected_below_water_count
            + packet.rejected_slope_count,
        packet.candidate_count
    );
}

#[test]
fn placement_builder_returns_empty_packet_when_mesh_has_no_surface() {
    let key = node(0, 0, 0, 0);
    let mut descriptor = terrain_variant_for_preset(1);
    descriptor.version = TERRAIN_VARIANT_DESCRIPTOR_VERSION + 1;

    let packet = build_node_surface_placement_samples_for_variant_with_config(
        0x0F6,
        descriptor,
        key,
        TEST_NODE_CELL_SIZE,
        TerrainPlacementSamplingConfig {
            candidate_grid_axis: 2,
            ..permissive_config()
        },
    );

    assert_eq!(packet.node_key, key);
    assert_eq!(packet.candidate_count, 4);
    assert_eq!(packet.accepted_count, 0);
    assert!(packet.samples.is_empty());
}

#[test]
fn placement_candidates_reject_invalid_grid_and_node_bounds() {
    let key = node(0, 1, 0, -2);

    assert!(terrain_placement_candidates_for_node(0x0F6, key, TEST_NODE_CELL_SIZE, 0).is_empty());
    assert!(terrain_placement_candidates_for_node(0x0F6, key, TEST_NODE_CELL_SIZE, 129).is_empty());
    assert!(terrain_placement_candidates_for_node(0x0F6, key, 0.0, 1).is_empty());
    assert!(terrain_placement_candidates_for_node(0x0F6, key, f64::NAN, 1).is_empty());
    assert!(
        terrain_placement_candidates_for_node(0x0F6, node(u8::MAX, 0, 0, 0), f64::MAX, 1)
            .is_empty()
    );
}

fn flat_surface(key: TerrainNodeKey, a: [f32; 3], b: [f32; 3], c: [f32; 3]) -> TerrainSurfaceIndex {
    let mesh = mesh_from_vertices(
        &[
            vertex(a, [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex(b, [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex(c, [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );

    TerrainSurfaceIndex::from_mesh(key, TEST_NODE_CELL_SIZE, &mesh)
        .expect("test triangle should produce a surface index")
}

fn permissive_config() -> TerrainPlacementSamplingConfig {
    TerrainPlacementSamplingConfig {
        candidate_grid_axis: 1,
        min_y: -100.0,
        max_y: 100.0,
        sea_level_meters: -100.0,
        min_normal_y: -1.0,
    }
}

fn mesh_from_vertices(vertices: &[[f32; FLOATS_PER_VERTEX]], indices: &[u32]) -> MeshData {
    MeshData {
        vertices: vertices
            .iter()
            .flat_map(|vertex| vertex.iter().copied())
            .collect(),
        indices: indices.to_vec(),
    }
}

fn vertex(position: [f32; 3], normal: [f32; 3], weights: [f32; 4]) -> [f32; FLOATS_PER_VERTEX] {
    [
        position[0],
        position[1],
        position[2],
        1.0,
        1.0,
        1.0,
        normal[0],
        normal[1],
        normal[2],
        0.0,
        0.0,
        0.0,
        1.0,
        2.0,
        3.0,
        weights[0],
        weights[1],
        weights[2],
        weights[3],
    ]
}

fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
    TerrainNodeKey {
        lod,
        coord: TerrainChunkCoord { x, y, z },
    }
}
