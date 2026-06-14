// Tests for exact vertical surface queries over polygonized terrain meshes.

use crate::*;

const TEST_NODE_CELL_SIZE: f64 = 1.0;
const HEIGHT_EPSILON: f64 = 0.000001;
const WEIGHT_EPSILON: f32 = 0.000001;

#[test]
fn surface_index_returns_exact_height_on_sloped_triangle() {
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 1.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 5.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 3.0, 4.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );
    let surface = TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
        .expect("sloped triangle should produce a query index");

    let hit = surface
        .highest_vertical_hit(query(1.0, 1.0))
        .expect("interior query should hit the triangle");

    assert_eq!(hit.node_key, node(0, 0, 0, 0));
    assert_eq!(hit.triangle_index, 0);
    assert!((hit.position[1] - 2.5).abs() <= HEIGHT_EPSILON);
}

#[test]
fn surface_index_hits_triangle_vertex_xz_without_duplicate_or_missed_hit() {
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 2.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 2.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 2.0, 4.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
            vertex([4.0, 2.0, 4.0], [0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]),
        ],
        &[0, 1, 2, 0, 3, 1],
    );
    let surface = TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
        .expect("shared-vertex triangles should produce a query index");

    let hits = surface.vertical_hits(query(0.0, 0.0));

    assert_eq!(hits.len(), 1);
    assert!((hits[0].position[1] - 2.0).abs() <= HEIGHT_EPSILON);
}

#[test]
fn surface_index_sorts_multiple_vertical_hits_from_high_to_low() {
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 2.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 2.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([0.0, 2.0, 4.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([0.0, 8.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([4.0, 8.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 8.0, 4.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
        ],
        &[0, 1, 2, 3, 4, 5],
    );
    let surface = TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
        .expect("stacked triangles should produce a query index");

    let hits = surface.vertical_hits(query(1.0, 1.0));

    assert_eq!(hits.len(), 2);
    assert!((hits[0].position[1] - 8.0).abs() <= HEIGHT_EPSILON);
    assert!((hits[1].position[1] - 2.0).abs() <= HEIGHT_EPSILON);
}

#[test]
fn surface_index_rejects_degenerate_vertical_projections() {
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([1.0, 1.0, 0.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([2.0, 2.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );

    assert_eq!(
        TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
            .map(|surface| surface.triangle_count()),
        None
    );
}

#[test]
fn surface_index_interpolates_normals_and_material_weights() {
    let mesh = mesh_from_vertices(
        &[
            vertex([0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([4.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([0.0, 0.0, 4.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );
    let surface = TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
        .expect("triangle should produce a query index");

    let hit = surface
        .highest_vertical_hit(query(1.0, 1.0))
        .expect("interior query should hit the triangle");

    assert_eq!(hit.material_indices, [0, 1, 2, 3]);
    assert!((hit.material_weights[0] - 0.5).abs() <= WEIGHT_EPSILON);
    assert!((hit.material_weights[1] - 0.25).abs() <= WEIGHT_EPSILON);
    assert!((hit.material_weights[2] - 0.25).abs() <= WEIGHT_EPSILON);
    assert_eq!(hit.material_weights[3], 0.0);
    assert!(hit.shading_normal[0] > 0.4);
    assert!(hit.shading_normal[1] > 0.8);
    assert!(hit.shading_normal[2] > 0.4);
}

#[test]
fn surface_index_uses_half_open_node_bounds_for_edge_ownership() {
    let mesh = mesh_from_vertices(
        &[
            vertex([31.0, 1.0, 1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0]),
            vertex([32.0, 1.0, 1.0], [0.0, 1.0, 0.0], [0.0, 1.0, 0.0, 0.0]),
            vertex([31.0, 1.0, 2.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0, 0.0]),
        ],
        &[0, 1, 2],
    );
    let surface = TerrainSurfaceIndex::from_mesh(node(0, 0, 0, 0), TEST_NODE_CELL_SIZE, &mesh)
        .expect("edge triangle should produce a query index");

    assert!(surface.highest_vertical_hit(query(31.25, 1.25)).is_some());
    assert!(surface.highest_vertical_hit(query(32.0, 1.0)).is_none());
}

#[test]
fn generated_node_surface_hits_lie_on_indexed_triangles() {
    let _lock = test_lock();
    let key = node(0, 0, 0, 0);
    let mesh = build_node_mesh(0x0F6, 1, key, TEST_NODE_CELL_SIZE);
    let surface = TerrainSurfaceIndex::from_mesh(key, TEST_NODE_CELL_SIZE, &mesh)
        .expect("generated terrain mesh should produce a query index");

    let (x, z, expected_y) = first_non_degenerate_triangle_centroid_xzy(&mesh)
        .expect("terrain mesh should have triangles");
    let hit = surface
        .highest_vertical_hit(TerrainVerticalQuery {
            x,
            z,
            min_y: expected_y - 0.01,
            max_y: expected_y + 0.01,
            min_normal_y: -1.0,
        })
        .expect("centroid query should hit its generated triangle");

    assert!((hit.position[0] - x).abs() <= HEIGHT_EPSILON);
    assert!((hit.position[1] - expected_y).abs() <= 0.01);
    assert!((hit.position[2] - z).abs() <= HEIGHT_EPSILON);
    assert_eq!(surface.bins_per_axis(), 32);
    assert!(surface.bin_reference_count() >= surface.triangle_count());
    assert!(surface.max_bin_occupancy() > 0);
}

#[test]
fn build_node_mesh_and_surface_for_variant_indexes_the_returned_mesh() {
    let _lock = test_lock();
    let key = node(0, 0, 0, 0);
    let output =
        build_node_mesh_and_surface_for_variant(0x0F6, terrain_variant_for_preset(1), key, 1.0);

    assert!(!output.mesh.indices.is_empty());
    let surface = output
        .surface
        .expect("renderable generated mesh should include a surface index");
    assert_eq!(surface.triangle_count(), output.mesh.indices.len() / 3);
    assert!(surface.bin_reference_count() >= surface.triangle_count());
}

fn first_non_degenerate_triangle_centroid_xzy(mesh: &MeshData) -> Option<(f64, f64, f64)> {
    for indices in mesh.indices.chunks_exact(3) {
        let positions = [
            mesh_position(mesh, indices[0])?,
            mesh_position(mesh, indices[1])?,
            mesh_position(mesh, indices[2])?,
        ];
        let projected_area = ((positions[1][2] - positions[2][2])
            * (positions[0][0] - positions[2][0])
            + (positions[2][0] - positions[1][0]) * (positions[0][2] - positions[2][2]))
            .abs();
        if projected_area <= 0.000001 {
            continue;
        }

        return Some((
            (positions[0][0] + positions[1][0] + positions[2][0]) / 3.0,
            (positions[0][2] + positions[1][2] + positions[2][2]) / 3.0,
            (positions[0][1] + positions[1][1] + positions[2][1]) / 3.0,
        ));
    }

    None
}

fn mesh_position(mesh: &MeshData, index: u32) -> Option<[f64; 3]> {
    let offset = index as usize * FLOATS_PER_VERTEX;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;

    Some([
        f64::from(vertex[0]),
        f64::from(vertex[1]),
        f64::from(vertex[2]),
    ])
}

fn query(x: f64, z: f64) -> TerrainVerticalQuery {
    TerrainVerticalQuery {
        x,
        z,
        min_y: -100.0,
        max_y: 100.0,
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
