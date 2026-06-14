// Mesh-space LOD transition aprons derived from existing child and parent
// terrain mesh buffers.
//
// This module intentionally does not sample terrain density or rerun Dual
// Contouring. It builds small optional render meshes from the polygonized
// `MeshData` that already exists for fine and parent terrain nodes.

use crate::*;

const DEFAULT_MAX_VERTICAL_SEARCH_METERS: f64 = 256.0;
const DEFAULT_MIN_NORMAL_Y: f64 = -1.0;
const BOUNDS_EPSILON: f64 = 1.0e-7;
const VERTEX_DEDUP_SCALE: f64 = 100_000.0;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum TerrainTransitionFace {
    NegX,
    PosX,
    NegZ,
    PosZ,
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainTransitionMeshKey {
    pub fine_key: TerrainNodeKey,
    pub parent_key: TerrainNodeKey,
    pub face: TerrainTransitionFace,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainTransitionMeshConfig {
    pub max_vertical_search_meters: f64,
    pub min_normal_y: f64,
}

#[derive(Clone, Copy)]
pub struct TerrainTransitionMeshInput<'a> {
    pub fine_key: TerrainNodeKey,
    pub parent_key: TerrainNodeKey,
    pub face: TerrainTransitionFace,
    pub fine_node_cell_size: f64,
    pub parent_node_cell_size: f64,
    pub fine_mesh: &'a MeshData,
    pub parent_mesh: &'a MeshData,
    pub config: TerrainTransitionMeshConfig,
}

#[derive(Clone, Copy)]
struct NodeXzBounds {
    origin_x: f64,
    origin_z: f64,
    span: f64,
}

#[derive(Clone)]
struct BoundaryVertex {
    sort_position: f64,
    position: [f64; 3],
    values: [f32; FLOATS_PER_VERTEX],
}

impl Default for TerrainTransitionMeshConfig {
    /// Returns permissive defaults for parent-child transition mesh construction.
    fn default() -> Self {
        Self {
            max_vertical_search_meters: DEFAULT_MAX_VERTICAL_SEARCH_METERS,
            min_normal_y: DEFAULT_MIN_NORMAL_Y,
        }
    }
}

/// Builds a side-face transition mesh from a fine node surface to its parent surface.
pub fn build_parent_lod_transition_edge_mesh(
    input: TerrainTransitionMeshInput<'_>,
) -> Option<MeshData> {
    validate_transition_input(input)?;
    let fine_bounds = node_xz_bounds(input.fine_key, input.fine_node_cell_size)?;
    let parent_bounds = node_xz_bounds(input.parent_key, input.parent_node_cell_size)?;
    if !parent_bounds.contain_child(fine_bounds) {
        return None;
    }

    let fine_profile = collect_boundary_profile(
        input.fine_mesh,
        fine_bounds,
        input.fine_node_cell_size,
        input.face,
        fine_bounds,
        input.config,
    );
    let parent_profile = collect_boundary_profile(
        input.parent_mesh,
        parent_bounds,
        input.parent_node_cell_size,
        input.face,
        fine_bounds,
        input.config,
    );
    transition_mesh_from_profiles(&fine_profile, &parent_profile)
}

fn validate_transition_input(input: TerrainTransitionMeshInput<'_>) -> Option<()> {
    if terrain_node_parent(input.fine_key) != Some(input.parent_key) {
        return None;
    }
    if !input.fine_node_cell_size.is_finite()
        || !input.parent_node_cell_size.is_finite()
        || input.fine_node_cell_size <= 0.0
        || input.parent_node_cell_size <= 0.0
        || !input.config.max_vertical_search_meters.is_finite()
        || input.config.max_vertical_search_meters <= 0.0
        || !input.config.min_normal_y.is_finite()
        || input.config.min_normal_y < -1.0
        || input.config.min_normal_y > 1.0
    {
        return None;
    }

    Some(())
}

fn collect_boundary_profile(
    mesh: &MeshData,
    bounds: NodeXzBounds,
    node_cell_size: f64,
    face: TerrainTransitionFace,
    profile_bounds: NodeXzBounds,
    config: TerrainTransitionMeshConfig,
) -> Vec<BoundaryVertex> {
    let mut vertices = mesh
        .vertices
        .chunks_exact(FLOATS_PER_VERTEX)
        .filter_map(|vertex| boundary_vertex(vertex, bounds, node_cell_size, face, profile_bounds))
        .filter(|vertex| f64::from(vertex.values[7]) >= config.min_normal_y)
        .collect::<Vec<_>>();

    vertices.sort_by(|left, right| {
        left.sort_position
            .total_cmp(&right.sort_position)
            .then_with(|| left.position[1].total_cmp(&right.position[1]))
            .then_with(|| left.position[0].total_cmp(&right.position[0]))
            .then_with(|| left.position[2].total_cmp(&right.position[2]))
    });
    vertices.dedup_by(|left, right| boundary_vertex_key(left) == boundary_vertex_key(right));
    vertices
}

fn boundary_vertex(
    vertex: &[f32],
    bounds: NodeXzBounds,
    node_cell_size: f64,
    face: TerrainTransitionFace,
    profile_bounds: NodeXzBounds,
) -> Option<BoundaryVertex> {
    let position = [
        f64::from(*vertex.first()?),
        f64::from(*vertex.get(1)?),
        f64::from(*vertex.get(2)?),
    ];
    if !position.iter().all(|value| value.is_finite()) {
        return None;
    }

    if !face_band_contains(bounds, node_cell_size, face, position)
        || !profile_axis_contains(profile_bounds, node_cell_size, face, position)
    {
        return None;
    }

    let mut values = [0.0_f32; FLOATS_PER_VERTEX];
    values.copy_from_slice(vertex);
    Some(BoundaryVertex {
        sort_position: face_sort_position(face, position),
        position,
        values,
    })
}

fn transition_mesh_from_profiles(
    fine_profile: &[BoundaryVertex],
    parent_profile: &[BoundaryVertex],
) -> Option<MeshData> {
    if fine_profile.len() < 2 || parent_profile.len() < 2 {
        return None;
    }

    let mut vertices =
        Vec::with_capacity((fine_profile.len() + parent_profile.len()) * FLOATS_PER_VERTEX);
    for vertex in fine_profile.iter().chain(parent_profile.iter()) {
        vertices.extend_from_slice(&vertex.values);
    }

    let parent_offset = fine_profile.len() as u32;
    let mut indices = Vec::new();
    let mut fine_index = 0_usize;
    let mut parent_index = 0_usize;
    while fine_index + 1 < fine_profile.len() || parent_index + 1 < parent_profile.len() {
        if parent_index + 1 >= parent_profile.len()
            || (fine_index + 1 < fine_profile.len()
                && fine_profile[fine_index + 1].sort_position
                    < parent_profile[parent_index + 1].sort_position)
        {
            push_double_sided_triangle(
                &mut indices,
                fine_index as u32,
                (fine_index + 1) as u32,
                parent_offset + parent_index as u32,
            );
            fine_index += 1;
        } else {
            push_double_sided_triangle(
                &mut indices,
                fine_index as u32,
                parent_offset + parent_index as u32,
                parent_offset + parent_index as u32 + 1,
            );
            parent_index += 1;
        }
    }

    (!indices.is_empty()).then_some(MeshData { vertices, indices })
}

fn push_double_sided_triangle(indices: &mut Vec<u32>, a: u32, b: u32, c: u32) {
    if a == b || b == c || a == c {
        return;
    }
    indices.extend_from_slice(&[a, b, c, c, b, a]);
}

fn boundary_vertex_key(vertex: &BoundaryVertex) -> (i64, i64, i64) {
    (
        quantized_position(vertex.position[0]),
        quantized_position(vertex.position[1]),
        quantized_position(vertex.position[2]),
    )
}

fn quantized_position(value: f64) -> i64 {
    (value * VERTEX_DEDUP_SCALE).round() as i64
}

fn face_band_contains(
    bounds: NodeXzBounds,
    node_cell_size: f64,
    face: TerrainTransitionFace,
    position: [f64; 3],
) -> bool {
    let (min, max, value) = match face {
        TerrainTransitionFace::NegX => (
            bounds.origin_x,
            bounds.origin_x + node_cell_size,
            position[0],
        ),
        TerrainTransitionFace::PosX => {
            (bounds.end_x(), bounds.end_x() + node_cell_size, position[0])
        }
        TerrainTransitionFace::NegZ => (
            bounds.origin_z,
            bounds.origin_z + node_cell_size,
            position[2],
        ),
        TerrainTransitionFace::PosZ => {
            (bounds.end_z(), bounds.end_z() + node_cell_size, position[2])
        }
    };

    value >= min - BOUNDS_EPSILON && value < max + BOUNDS_EPSILON
}

fn profile_axis_contains(
    bounds: NodeXzBounds,
    axis_padding: f64,
    face: TerrainTransitionFace,
    position: [f64; 3],
) -> bool {
    let (min, max, value) = match face {
        TerrainTransitionFace::NegX | TerrainTransitionFace::PosX => {
            (bounds.origin_z, bounds.end_z(), position[2])
        }
        TerrainTransitionFace::NegZ | TerrainTransitionFace::PosZ => {
            (bounds.origin_x, bounds.end_x(), position[0])
        }
    };

    value >= min - axis_padding - BOUNDS_EPSILON && value <= max + axis_padding + BOUNDS_EPSILON
}

fn face_sort_position(face: TerrainTransitionFace, position: [f64; 3]) -> f64 {
    match face {
        TerrainTransitionFace::NegX | TerrainTransitionFace::PosX => position[2],
        TerrainTransitionFace::NegZ | TerrainTransitionFace::PosZ => position[0],
    }
}

impl NodeXzBounds {
    fn contain_child(self, child: NodeXzBounds) -> bool {
        child.origin_x >= self.origin_x - BOUNDS_EPSILON
            && child.origin_z >= self.origin_z - BOUNDS_EPSILON
            && child.end_x() <= self.end_x() + BOUNDS_EPSILON
            && child.end_z() <= self.end_z() + BOUNDS_EPSILON
    }

    fn end_x(self) -> f64 {
        self.origin_x + self.span
    }

    fn end_z(self) -> f64 {
        self.origin_z + self.span
    }
}

fn node_xz_bounds(key: TerrainNodeKey, node_cell_size: f64) -> Option<NodeXzBounds> {
    if !node_cell_size.is_finite() || node_cell_size <= 0.0 {
        return None;
    }

    let span = node_cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    if !span.is_finite() || span <= 0.0 {
        return None;
    }

    Some(NodeXzBounds {
        origin_x: key.coord.x as f64 * span,
        origin_z: key.coord.z as f64 * span,
        span,
    })
}
