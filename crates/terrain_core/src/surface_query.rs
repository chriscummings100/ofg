// CPU-side vertical surface queries for generated terrain meshes.
//
// This module builds a compact XZ bin index over the same triangle buffers that
// terrain rendering consumes, so placement code can query the polygonized Dual
// Contouring surface instead of the analytic density height approximation.

use crate::surface_query_geometry::{
    bin_index, bin_range_for_interval, coordinate_to_bin, interpolate_color,
    interpolate_material_weights, interpolate_normal, read_surface_triangle,
    triangle_xz_bounds_contain, vertical_hit_on_triangle, TerrainSurfaceTriangle,
};
use crate::*;

const SURFACE_QUERY_BINS_PER_AXIS: u16 = 32;
const HIT_DEDUP_Y_EPSILON: f64 = 1.0e-6;
const BOUNDARY_INCLUSIVE_EPSILON: f64 = 1.0e-7;

/// Spatial index for vertical queries against one generated terrain node mesh.
#[derive(Clone, Debug)]
pub struct TerrainSurfaceIndex {
    key: TerrainNodeKey,
    node_origin_x: f64,
    node_origin_z: f64,
    node_span: f64,
    bins_per_axis: u16,
    bin_offsets: Vec<u32>,
    bin_triangle_indices: Vec<u32>,
    triangles: Vec<TerrainSurfaceTriangle>,
}

/// Input parameters for one vertical terrain surface query at fixed world XZ.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainVerticalQuery {
    pub x: f64,
    pub z: f64,
    pub min_y: f64,
    pub max_y: f64,
    pub min_normal_y: f64,
}

/// One vertical query hit against a polygonized terrain triangle.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainSurfaceHit {
    pub node_key: TerrainNodeKey,
    pub triangle_index: u32,
    pub position: [f64; 3],
    pub color: [f32; 3],
    pub geometric_normal: [f32; 3],
    pub shading_normal: [f32; 3],
    pub material_indices: [u8; 4],
    pub material_weights: [f32; 4],
}

impl TerrainSurfaceIndex {
    /// Builds an XZ bin index over all valid triangles in one generated terrain node mesh.
    pub fn from_mesh(key: TerrainNodeKey, node_cell_size: f64, mesh: &MeshData) -> Option<Self> {
        if !node_cell_size.is_finite()
            || node_cell_size <= 0.0
            || mesh.vertices.len() % FLOATS_PER_VERTEX != 0
            || mesh.indices.len() % 3 != 0
            || mesh.indices.is_empty()
        {
            return None;
        }

        let node_span = node_cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
        if !node_span.is_finite() || node_span <= 0.0 {
            return None;
        }

        let node_origin_x = key.coord.x as f64 * node_span;
        let node_origin_z = key.coord.z as f64 * node_span;
        let triangles = mesh
            .indices
            .chunks_exact(3)
            .enumerate()
            .filter_map(|(triangle_index, indices)| {
                read_surface_triangle(mesh, triangle_index as u32, indices)
            })
            .collect::<Vec<_>>();

        if triangles.is_empty() {
            return None;
        }

        let bins_per_axis = SURFACE_QUERY_BINS_PER_AXIS;
        let bin_count = usize::from(bins_per_axis) * usize::from(bins_per_axis);
        let mut bins = vec![Vec::<u32>::new(); bin_count];

        for (triangle_storage_index, triangle) in triangles.iter().enumerate() {
            let Some((min_x, max_x)) = bin_range_for_interval(
                triangle.x_min,
                triangle.x_max,
                node_origin_x,
                node_span,
                bins_per_axis,
            ) else {
                continue;
            };
            let Some((min_z, max_z)) = bin_range_for_interval(
                triangle.z_min,
                triangle.z_max,
                node_origin_z,
                node_span,
                bins_per_axis,
            ) else {
                continue;
            };

            for z in min_z..=max_z {
                for x in min_x..=max_x {
                    bins[bin_index(x, z, bins_per_axis)].push(triangle_storage_index as u32);
                }
            }
        }

        let mut bin_offsets = Vec::with_capacity(bin_count + 1);
        let mut bin_triangle_indices = Vec::new();
        bin_offsets.push(0);
        for bin in bins {
            bin_triangle_indices.extend(bin);
            bin_offsets.push(bin_triangle_indices.len() as u32);
        }

        Some(Self {
            key,
            node_origin_x,
            node_origin_z,
            node_span,
            bins_per_axis,
            bin_offsets,
            bin_triangle_indices,
            triangles,
        })
    }

    /// Returns all vertical ray hits at the query XZ, sorted from highest to lowest Y.
    pub fn vertical_hits(&self, query: TerrainVerticalQuery) -> Vec<TerrainSurfaceHit> {
        self.vertical_hits_with_ownership(query, false)
    }

    /// Returns the highest vertical hit at the query XZ after all query filters are applied.
    pub fn highest_vertical_hit(&self, query: TerrainVerticalQuery) -> Option<TerrainSurfaceHit> {
        self.vertical_hits(query).into_iter().next()
    }

    /// Returns vertical hits while accepting exact max-edge node boundaries.
    ///
    /// Placement should normally use `vertical_hits`, which keeps half-open node
    /// ownership. Transition mesh generation needs this boundary-inclusive query
    /// to connect child and parent mesh edges at the same XZ line.
    pub fn vertical_hits_including_boundary(
        &self,
        query: TerrainVerticalQuery,
    ) -> Vec<TerrainSurfaceHit> {
        self.vertical_hits_with_ownership(query, true)
    }

    /// Returns the highest boundary-inclusive vertical hit at the query XZ.
    pub fn highest_vertical_hit_including_boundary(
        &self,
        query: TerrainVerticalQuery,
    ) -> Option<TerrainSurfaceHit> {
        self.vertical_hits_including_boundary(query)
            .into_iter()
            .next()
    }

    /// Returns the number of valid triangles stored in this index.
    pub fn triangle_count(&self) -> usize {
        self.triangles.len()
    }

    /// Returns the total number of triangle references stored across all bins.
    pub fn bin_reference_count(&self) -> usize {
        self.bin_triangle_indices.len()
    }

    /// Returns the largest number of triangle references stored in one bin.
    pub fn max_bin_occupancy(&self) -> usize {
        self.bin_offsets
            .windows(2)
            .map(|range| (range[1] - range[0]) as usize)
            .max()
            .unwrap_or(0)
    }

    /// Returns the fixed XZ bin count per axis.
    pub fn bins_per_axis(&self) -> u16 {
        self.bins_per_axis
    }

    /// Returns the terrain node key this index was built from.
    pub fn node_key(&self) -> TerrainNodeKey {
        self.key
    }

    fn hit_for_triangle(
        &self,
        triangle_storage_index: usize,
        query: TerrainVerticalQuery,
    ) -> Option<TerrainSurfaceHit> {
        let triangle = self.triangles.get(triangle_storage_index)?;
        if !triangle_xz_bounds_contain(triangle, query.x, query.z) {
            return None;
        }
        let hit = vertical_hit_on_triangle(triangle, query.x, query.z)?;
        if hit.y < query.min_y || hit.y > query.max_y {
            return None;
        }

        let shading_normal = interpolate_normal(triangle.shading_normals, hit.weights);
        if f64::from(shading_normal[1]) < query.min_normal_y {
            return None;
        }

        Some(TerrainSurfaceHit {
            node_key: self.key,
            triangle_index: triangle.triangle_index,
            position: [query.x, hit.y, query.z],
            color: interpolate_color(triangle.colors, hit.weights),
            geometric_normal: triangle.geometric_normal,
            shading_normal,
            material_indices: triangle.material_indices,
            material_weights: interpolate_material_weights(triangle.material_weights, hit.weights),
        })
    }

    fn vertical_hits_with_ownership(
        &self,
        query: TerrainVerticalQuery,
        include_boundary: bool,
    ) -> Vec<TerrainSurfaceHit> {
        let owns_xz = if include_boundary {
            self.includes_boundary_xz(query.x, query.z)
        } else {
            self.owns_xz(query.x, query.z)
        };
        if !query_is_valid(query) || !owns_xz {
            return Vec::new();
        }

        let bin = bin_index(
            self.bin_for_x(query.x),
            self.bin_for_z(query.z),
            self.bins_per_axis,
        );
        let start = self.bin_offsets[bin] as usize;
        let end = self.bin_offsets[bin + 1] as usize;
        let mut hits = self.bin_triangle_indices[start..end]
            .iter()
            .filter_map(|triangle_storage_index| {
                self.hit_for_triangle(*triangle_storage_index as usize, query)
            })
            .collect::<Vec<_>>();

        sort_and_deduplicate_hits(&mut hits);
        hits
    }

    fn owns_xz(&self, x: f64, z: f64) -> bool {
        x.is_finite()
            && z.is_finite()
            && x >= self.node_origin_x
            && z >= self.node_origin_z
            && x < self.node_origin_x + self.node_span
            && z < self.node_origin_z + self.node_span
    }

    fn includes_boundary_xz(&self, x: f64, z: f64) -> bool {
        x.is_finite()
            && z.is_finite()
            && x >= self.node_origin_x - BOUNDARY_INCLUSIVE_EPSILON
            && z >= self.node_origin_z - BOUNDARY_INCLUSIVE_EPSILON
            && x <= self.node_origin_x + self.node_span + BOUNDARY_INCLUSIVE_EPSILON
            && z <= self.node_origin_z + self.node_span + BOUNDARY_INCLUSIVE_EPSILON
    }

    fn bin_for_x(&self, x: f64) -> u16 {
        coordinate_to_bin(x, self.node_origin_x, self.node_span, self.bins_per_axis)
    }

    fn bin_for_z(&self, z: f64) -> u16 {
        coordinate_to_bin(z, self.node_origin_z, self.node_span, self.bins_per_axis)
    }
}

fn query_is_valid(query: TerrainVerticalQuery) -> bool {
    query.x.is_finite()
        && query.z.is_finite()
        && query.min_y.is_finite()
        && query.max_y.is_finite()
        && query.min_normal_y.is_finite()
        && query.min_y <= query.max_y
}

fn sort_and_deduplicate_hits(hits: &mut Vec<TerrainSurfaceHit>) {
    hits.sort_by(|left, right| {
        right.position[1]
            .total_cmp(&left.position[1])
            .then_with(|| right.shading_normal[1].total_cmp(&left.shading_normal[1]))
            .then_with(|| left.triangle_index.cmp(&right.triangle_index))
    });

    let mut deduplicated = Vec::with_capacity(hits.len());
    for hit in hits.drain(..) {
        if deduplicated.iter().any(|existing: &TerrainSurfaceHit| {
            (existing.position[1] - hit.position[1]).abs() <= HIT_DEDUP_Y_EPSILON
        }) {
            continue;
        }
        deduplicated.push(hit);
    }

    *hits = deduplicated;
}
