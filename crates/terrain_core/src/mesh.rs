//! Grass-only heightfield mesh generation for terrain nodes.

use crate::heightfield::height_at_for_variant;
use crate::node::{
    terrain_node_cell_size, terrain_node_size, TerrainChunkCoord, TerrainNodeKey,
    TERRAIN_CHUNK_CELLS_PER_AXIS,
};
use crate::variant::{terrain_variant_for_preset, TerrainVariantDescriptor};

pub const TERRAIN_VERTEX_FLOATS: usize = 19;
const POINT_IN_TRIANGLE_EPSILON: f64 = 0.0001;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct MeshData {
    pub vertices: Vec<f32>,
    pub indices: Vec<u32>,
}

impl MeshData {
    /// Samples the generated triangle surface at a world X/Z point.
    pub fn height_at(&self, x: f32, z: f32) -> Option<f32> {
        mesh_height_at(self, x, z)
    }
}

/// Builds an LOD0 compatibility mesh.
pub fn build_chunk_mesh(
    seed: u32,
    preset: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> MeshData {
    build_node_mesh(seed, preset, TerrainNodeKey { lod: 0, coord }, cell_size)
}

/// Builds a mesh for a terrain node and preset code.
pub fn build_node_mesh(
    seed: u32,
    preset: u32,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> MeshData {
    build_node_mesh_for_variant(
        seed,
        terrain_variant_for_preset(preset),
        key,
        base_cell_size,
    )
}

/// Builds a grass-only heightfield mesh for a terrain node.
pub fn build_node_mesh_for_variant(
    seed: u32,
    variant: TerrainVariantDescriptor,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> MeshData {
    if variant.validate().is_err() || !base_cell_size.is_finite() || base_cell_size <= 0.0 {
        return MeshData::default();
    }

    let node_size = terrain_node_size(base_cell_size, key.lod);
    let node_min_y = f64::from(key.coord.y) * node_size;
    let node_max_y = node_min_y + node_size;
    let origin_x = f64::from(key.coord.x) * node_size;
    let origin_z = f64::from(key.coord.z) * node_size;
    let cell_size = terrain_node_cell_size(base_cell_size, key.lod);
    let sample_count = TERRAIN_CHUNK_CELLS_PER_AXIS + 1;

    let mut heights = Vec::with_capacity((sample_count * sample_count) as usize);
    let mut intersects_node = false;
    for z in 0..sample_count {
        for x in 0..sample_count {
            let world_x = origin_x + f64::from(x) * cell_size;
            let world_z = origin_z + f64::from(z) * cell_size;
            let height = height_at_for_variant(seed, variant, world_x, world_z).unwrap_or(0.0);
            intersects_node |= height >= node_min_y && height <= node_max_y;
            heights.push(height);
        }
    }

    if !intersects_node {
        return MeshData::default();
    }

    let mut vertices =
        Vec::with_capacity((sample_count * sample_count) as usize * TERRAIN_VERTEX_FLOATS);
    for z in 0..sample_count {
        for x in 0..sample_count {
            let index = (z * sample_count + x) as usize;
            let world_x = origin_x + f64::from(x) * cell_size;
            let world_z = origin_z + f64::from(z) * cell_size;
            push_grass_vertex(
                &mut vertices,
                world_x,
                heights[index],
                world_z,
                x,
                z,
                sample_count,
                &heights,
                cell_size,
            );
        }
    }

    let mut indices = Vec::with_capacity(
        (TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS * 6) as usize,
    );
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            let a = z * sample_count + x;
            let b = a + 1;
            let c = a + sample_count;
            let d = c + 1;
            indices.extend_from_slice(&[a, b, c, c, b, d]);
        }
    }

    MeshData { vertices, indices }
}

/// Interpolates terrain height from generated mesh triangles at world X/Z.
pub fn mesh_height_at(mesh: &MeshData, x: f32, z: f32) -> Option<f32> {
    if !x.is_finite()
        || !z.is_finite()
        || mesh.vertices.len() % TERRAIN_VERTEX_FLOATS != 0
        || mesh.indices.len() % 3 != 0
    {
        return None;
    }

    let mut best_height: Option<f64> = None;
    for triangle in mesh.indices.chunks_exact(3) {
        let (Some(a), Some(b), Some(c)) = (
            vertex_position(mesh, triangle[0]),
            vertex_position(mesh, triangle[1]),
            vertex_position(mesh, triangle[2]),
        ) else {
            continue;
        };
        let Some(height) = triangle_height_at_xz(a, b, c, f64::from(x), f64::from(z)) else {
            continue;
        };
        best_height = Some(best_height.map_or(height, |previous| previous.max(height)));
    }

    best_height.map(|height| height as f32)
}

fn push_grass_vertex(
    vertices: &mut Vec<f32>,
    world_x: f64,
    world_y: f64,
    world_z: f64,
    x: u32,
    z: u32,
    sample_count: u32,
    heights: &[f64],
    cell_size: f64,
) {
    let normal = normal_at_sample(x, z, sample_count, heights, cell_size);
    vertices.extend_from_slice(&[
        world_x as f32,
        world_y as f32,
        world_z as f32,
        0.18,
        0.62,
        0.22,
        normal[0],
        normal[1],
        normal[2],
        world_x as f32 * 0.05,
        world_z as f32 * 0.05,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
    ]);
}

fn normal_at_sample(
    x: u32,
    z: u32,
    sample_count: u32,
    heights: &[f64],
    cell_size: f64,
) -> [f32; 3] {
    let left = height_sample(x.saturating_sub(1), z, sample_count, heights);
    let right = height_sample((x + 1).min(sample_count - 1), z, sample_count, heights);
    let down = height_sample(x, z.saturating_sub(1), sample_count, heights);
    let up = height_sample(x, (z + 1).min(sample_count - 1), sample_count, heights);
    let dx = (left - right) / (2.0 * cell_size);
    let dz = (down - up) / (2.0 * cell_size);
    let length = (dx * dx + dz * dz + 1.0).sqrt();
    [
        (dx / length) as f32,
        (1.0 / length) as f32,
        (dz / length) as f32,
    ]
}

fn height_sample(x: u32, z: u32, sample_count: u32, heights: &[f64]) -> f64 {
    heights[(z * sample_count + x) as usize]
}

fn vertex_position(mesh: &MeshData, index: u32) -> Option<[f64; 3]> {
    let start = index as usize * TERRAIN_VERTEX_FLOATS;
    let end = start.checked_add(3)?;
    let values = mesh.vertices.get(start..end)?;
    let x = f64::from(values[0]);
    let y = f64::from(values[1]);
    let z = f64::from(values[2]);
    if x.is_finite() && y.is_finite() && z.is_finite() {
        Some([x, y, z])
    } else {
        None
    }
}

fn triangle_height_at_xz(a: [f64; 3], b: [f64; 3], c: [f64; 3], x: f64, z: f64) -> Option<f64> {
    let denominator = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
    if denominator.abs() <= f64::EPSILON {
        return None;
    }

    let wa = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / denominator;
    let wb = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / denominator;
    let wc = 1.0 - wa - wb;
    if wa < -POINT_IN_TRIANGLE_EPSILON
        || wb < -POINT_IN_TRIANGLE_EPSILON
        || wc < -POINT_IN_TRIANGLE_EPSILON
    {
        return None;
    }

    Some(wa * a[1] + wb * b[1] + wc * c[1])
}
