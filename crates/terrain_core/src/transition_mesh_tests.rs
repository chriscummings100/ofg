// Tests for mesh-space LOD transition edge aprons built from existing terrain
// mesh buffers.

use crate::*;

const FINE_CELL_SIZE: f64 = 1.0;
const PARENT_CELL_SIZE: f64 = 2.0;
const POSITION_EPSILON: f64 = 0.000001;

#[test]
fn transition_mesh_bridges_child_band_to_parent_boundary() {
    let fine_key = node(0, 0, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &fine_axis,
        0.25,
        4.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &parent_axis,
        0.75,
        2.0,
        [1.0, 1.0, 1.0],
    );

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("flat child and parent planes should produce a transition mesh");

    let expected_vertex_count = fine_axis.len() + parent_axis.len();
    let expected_triangle_count = fine_axis.len() + parent_axis.len() - 2;
    assert_eq!(
        transition.vertices.len(),
        expected_vertex_count * FLOATS_PER_VERTEX
    );
    assert_eq!(transition.indices.len(), expected_triangle_count * 6);
    assert_indices_are_valid(&transition);
    assert_position(vertex_position(&transition, 0), [0.25, 4.0, 0.0]);
    assert_position(
        vertex_position(&transition, fine_axis.len()),
        [0.75, 2.0, 0.0],
    );
    assert_position(
        vertex_position(&transition, fine_axis.len() - 1),
        [0.25, 4.0, 32.0],
    );
    assert_position(
        vertex_position(&transition, expected_vertex_count - 1),
        [0.75, 2.0, 32.0],
    );
}

#[test]
fn transition_mesh_uses_parent_boundary_vertices_instead_of_plane_queries() {
    let fine_key = node(0, 1, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::PosX,
        &fine_axis,
        0.25,
        5.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::PosX,
        &parent_axis,
        0.75,
        3.0,
        [1.0, 1.0, 1.0],
    );

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::PosX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("transition mesh should reuse parent boundary vertices");

    assert_position(vertex_position(&transition, 0), [64.25, 5.0, 0.0]);
    assert_position(
        vertex_position(&transition, fine_axis.len()),
        [64.75, 3.0, 0.0],
    );
}

#[test]
fn transition_mesh_does_not_mutate_source_meshes() {
    let fine_key = node(0, 0, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegZ,
        &fine_axis,
        0.25,
        4.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegZ,
        &parent_axis,
        0.75,
        2.0,
        [1.0, 1.0, 1.0],
    );
    let original_fine = fine_mesh.clone();
    let original_parent = parent_mesh.clone();

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegZ,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("transition mesh should build from borrowed mesh data");

    assert!(!transition.indices.is_empty());
    assert_eq!(fine_mesh.vertices, original_fine.vertices);
    assert_eq!(fine_mesh.indices, original_fine.indices);
    assert_eq!(parent_mesh.vertices, original_parent.vertices);
    assert_eq!(parent_mesh.indices, original_parent.indices);
}

#[test]
fn transition_mesh_preserves_interpolated_source_vertex_colors() {
    let fine_key = node(0, 0, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &fine_axis,
        0.25,
        4.0,
        [0.2, 0.4, 0.6],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &parent_axis,
        0.75,
        2.0,
        [0.7, 0.8, 0.1],
    );

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("colored planes should produce a transition mesh");

    assert_color(vertex_color(&transition, 0), [0.2, 0.4, 0.6]);
    assert_color(vertex_color(&transition, fine_axis.len()), [0.7, 0.8, 0.1]);
}

#[test]
fn transition_mesh_zippers_double_resolution_child_profile_to_parent_profile() {
    let fine_key = node(0, 0, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &fine_axis,
        0.25,
        4.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &parent_axis,
        0.75,
        2.0,
        [1.0, 1.0, 1.0],
    );

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("double-resolution child profile should zipper to parent profile");

    assert_eq!(
        transition.vertices.len(),
        (fine_axis.len() + parent_axis.len()) * FLOATS_PER_VERTEX
    );
    assert_eq!(
        transition.indices.len(),
        (fine_axis.len() + parent_axis.len() - 2) * 6
    );
    assert_indices_are_valid(&transition);
}

#[test]
fn transition_mesh_rejects_invalid_inputs_and_missing_parent_hits() {
    let fine_key = node(0, 0, 0, 0);
    let parent_key = node(1, 0, 0, 0);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &fine_axis,
        0.25,
        4.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegX,
        &parent_axis,
        0.75,
        2.0,
        [1.0, 1.0, 1.0],
    );

    assert!(build_parent_lod_transition_edge_mesh(input(
        fine_key,
        node(1, 1, 0, 0),
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .is_none());
    assert!(build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &MeshData {
            vertices: Vec::new(),
            indices: Vec::new(),
        },
        TerrainTransitionMeshConfig::default(),
    ))
    .is_none());
    assert!(build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegX,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig {
            max_vertical_search_meters: f64::NAN,
            ..TerrainTransitionMeshConfig::default()
        },
    ))
    .is_none());
}

#[test]
fn transition_mesh_orders_negative_coordinate_faces_deterministically() {
    let fine_key = node(0, -2, 0, -2);
    let parent_key = node(1, -1, 0, -1);
    let fine_axis = axis_values(1);
    let parent_axis = axis_values(2);
    let fine_mesh = boundary_profile_mesh(
        fine_key,
        FINE_CELL_SIZE,
        TerrainTransitionFace::NegZ,
        &fine_axis,
        0.25,
        6.0,
        [1.0, 1.0, 1.0],
    );
    let parent_mesh = boundary_profile_mesh(
        parent_key,
        PARENT_CELL_SIZE,
        TerrainTransitionFace::NegZ,
        &parent_axis,
        0.75,
        4.0,
        [1.0, 1.0, 1.0],
    );

    let transition = build_parent_lod_transition_edge_mesh(input(
        fine_key,
        parent_key,
        TerrainTransitionFace::NegZ,
        &fine_mesh,
        &parent_mesh,
        TerrainTransitionMeshConfig::default(),
    ))
    .expect("negative coordinate parent boundary should produce a transition mesh");

    assert_position(vertex_position(&transition, 0), [-64.0, 6.0, -63.75]);
    assert_position(
        vertex_position(&transition, fine_axis.len()),
        [-64.0, 4.0, -63.25],
    );
    assert_position(
        vertex_position(&transition, fine_axis.len() - 1),
        [-32.0, 6.0, -63.75],
    );
    assert_position(
        vertex_position(&transition, fine_axis.len() + parent_axis.len() - 1),
        [-32.0, 4.0, -63.25],
    );
}

fn input<'a>(
    fine_key: TerrainNodeKey,
    parent_key: TerrainNodeKey,
    face: TerrainTransitionFace,
    fine_mesh: &'a MeshData,
    parent_mesh: &'a MeshData,
    config: TerrainTransitionMeshConfig,
) -> TerrainTransitionMeshInput<'a> {
    TerrainTransitionMeshInput {
        fine_key,
        parent_key,
        face,
        fine_node_cell_size: FINE_CELL_SIZE,
        parent_node_cell_size: PARENT_CELL_SIZE,
        fine_mesh,
        parent_mesh,
        config,
    }
}

fn axis_values(step_cells: usize) -> Vec<f64> {
    (0..=TERRAIN_CHUNK_CELLS_PER_AXIS)
        .step_by(step_cells)
        .map(|offset| offset as f64)
        .collect()
}

fn boundary_profile_mesh(
    key: TerrainNodeKey,
    node_cell_size: f64,
    face: TerrainTransitionFace,
    axis_offsets: &[f64],
    depth: f64,
    y: f32,
    color: [f32; 3],
) -> MeshData {
    let span = node_cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let origin_x = key.coord.x as f64 * span;
    let origin_z = key.coord.z as f64 * span;
    let end_x = origin_x + span;
    let end_z = origin_z + span;
    let vertices = axis_offsets
        .iter()
        .map(|axis_offset| {
            let position = match face {
                TerrainTransitionFace::NegX => [
                    (origin_x + depth) as f32,
                    y,
                    (origin_z + axis_offset) as f32,
                ],
                TerrainTransitionFace::PosX => {
                    [(end_x + depth) as f32, y, (origin_z + axis_offset) as f32]
                }
                TerrainTransitionFace::NegZ => [
                    (origin_x + axis_offset) as f32,
                    y,
                    (origin_z + depth) as f32,
                ],
                TerrainTransitionFace::PosZ => {
                    [(origin_x + axis_offset) as f32, y, (end_z + depth) as f32]
                }
            };
            vertex(position, color)
        })
        .collect::<Vec<_>>();

    MeshData {
        vertices: vertices
            .iter()
            .flat_map(|vertex| vertex.iter().copied())
            .collect(),
        indices: Vec::new(),
    }
}

fn vertex(position: [f32; 3], color: [f32; 3]) -> [f32; FLOATS_PER_VERTEX] {
    [
        position[0],
        position[1],
        position[2],
        color[0],
        color[1],
        color[2],
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        1.0,
        2.0,
        3.0,
        4.0,
        1.0,
        0.0,
        0.0,
        0.0,
    ]
}

fn vertex_position(mesh: &MeshData, vertex_index: usize) -> [f64; 3] {
    let offset = vertex_index * FLOATS_PER_VERTEX;
    [
        f64::from(mesh.vertices[offset]),
        f64::from(mesh.vertices[offset + 1]),
        f64::from(mesh.vertices[offset + 2]),
    ]
}

fn vertex_color(mesh: &MeshData, vertex_index: usize) -> [f32; 3] {
    let offset = vertex_index * FLOATS_PER_VERTEX;
    [
        mesh.vertices[offset + 3],
        mesh.vertices[offset + 4],
        mesh.vertices[offset + 5],
    ]
}

fn assert_position(actual: [f64; 3], expected: [f64; 3]) {
    assert!(
        actual
            .iter()
            .zip(expected)
            .all(|(actual, expected)| (*actual - expected).abs() <= POSITION_EPSILON),
        "expected position {expected:?}, got {actual:?}"
    );
}

fn assert_color(actual: [f32; 3], expected: [f32; 3]) {
    assert!(
        actual
            .iter()
            .zip(expected)
            .all(|(actual, expected)| (*actual - expected).abs() <= POSITION_EPSILON as f32),
        "expected color {expected:?}, got {actual:?}"
    );
}

fn assert_indices_are_valid(mesh: &MeshData) {
    assert_eq!(mesh.vertices.len() % FLOATS_PER_VERTEX, 0);
    let vertex_count = mesh.vertices.len() / FLOATS_PER_VERTEX;
    assert!(mesh
        .indices
        .iter()
        .all(|index| (*index as usize) < vertex_count));
}

fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
    TerrainNodeKey {
        lod,
        coord: TerrainChunkCoord { x, y, z },
    }
}
