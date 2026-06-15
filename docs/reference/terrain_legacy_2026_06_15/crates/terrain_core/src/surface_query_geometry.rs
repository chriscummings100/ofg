// Low-level triangle extraction and vertical intersection helpers for terrain
// surface queries.

use crate::*;

const PROJECTED_AREA_EPSILON: f64 = 1.0e-10;
const BARYCENTRIC_EPSILON: f64 = 1.0e-7;

/// One valid terrain mesh triangle prepared for vertical surface queries.
#[derive(Clone, Debug)]
pub(crate) struct TerrainSurfaceTriangle {
    pub(crate) triangle_index: u32,
    pub(crate) positions: [[f64; 3]; 3],
    pub(crate) colors: [[f32; 3]; 3],
    pub(crate) shading_normals: [[f32; 3]; 3],
    pub(crate) material_indices: [u8; 4],
    pub(crate) material_weights: [[f32; 4]; 3],
    pub(crate) geometric_normal: [f32; 3],
    pub(crate) x_min: f64,
    pub(crate) x_max: f64,
    pub(crate) z_min: f64,
    pub(crate) z_max: f64,
    projected_denominator: f64,
}

/// Barycentric weights and world height for a vertical triangle hit.
#[derive(Clone, Copy, Debug)]
pub(crate) struct BarycentricHit {
    pub(crate) weights: [f64; 3],
    pub(crate) y: f64,
}

/// Reads one indexed render triangle and prepares its query payload.
pub(crate) fn read_surface_triangle(
    mesh: &MeshData,
    triangle_index: u32,
    indices: &[u32],
) -> Option<TerrainSurfaceTriangle> {
    let positions = [
        read_position(mesh, indices[0])?,
        read_position(mesh, indices[1])?,
        read_position(mesh, indices[2])?,
    ];
    let projected_denominator = projected_barycentric_denominator(positions);
    if projected_denominator.abs() <= PROJECTED_AREA_EPSILON {
        return None;
    }

    let shading_normals = [
        read_normal(mesh, indices[0])?,
        read_normal(mesh, indices[1])?,
        read_normal(mesh, indices[2])?,
    ];
    let colors = [
        read_color(mesh, indices[0])?,
        read_color(mesh, indices[1])?,
        read_color(mesh, indices[2])?,
    ];
    let material_indices = read_material_indices(mesh, indices[0])?;
    let material_weights = [
        read_material_weights(mesh, indices[0])?,
        read_material_weights(mesh, indices[1])?,
        read_material_weights(mesh, indices[2])?,
    ];
    let mut geometric_normal = geometric_normal_for_positions(positions)?;
    let average_shading_normal = interpolate_normal(shading_normals, [1.0 / 3.0; 3]);
    if dot3(geometric_normal, average_shading_normal) < 0.0 {
        geometric_normal = [
            -geometric_normal[0],
            -geometric_normal[1],
            -geometric_normal[2],
        ];
    }

    Some(TerrainSurfaceTriangle {
        triangle_index,
        positions,
        colors,
        shading_normals,
        material_indices,
        material_weights,
        geometric_normal,
        x_min: positions
            .iter()
            .map(|position| position[0])
            .fold(f64::INFINITY, f64::min),
        x_max: positions
            .iter()
            .map(|position| position[0])
            .fold(f64::NEG_INFINITY, f64::max),
        z_min: positions
            .iter()
            .map(|position| position[2])
            .fold(f64::INFINITY, f64::min),
        z_max: positions
            .iter()
            .map(|position| position[2])
            .fold(f64::NEG_INFINITY, f64::max),
        projected_denominator,
    })
}

/// Returns a vertical hit for `x,z` if the projected point lies inside the triangle.
pub(crate) fn vertical_hit_on_triangle(
    triangle: &TerrainSurfaceTriangle,
    x: f64,
    z: f64,
) -> Option<BarycentricHit> {
    let weights =
        projected_barycentric_weights(triangle.positions, triangle.projected_denominator, x, z);
    if weights
        .iter()
        .any(|weight| *weight < -BARYCENTRIC_EPSILON || *weight > 1.0 + BARYCENTRIC_EPSILON)
    {
        return None;
    }

    let weights = normalize_barycentric_weights(weights);
    let y = triangle.positions[0][1] * weights[0]
        + triangle.positions[1][1] * weights[1]
        + triangle.positions[2][1] * weights[2];
    if !y.is_finite() {
        return None;
    }

    Some(BarycentricHit { weights, y })
}

/// Interpolates three packed vertex colors.
pub(crate) fn interpolate_color(colors: [[f32; 3]; 3], weights: [f64; 3]) -> [f32; 3] {
    [
        (f64::from(colors[0][0]) * weights[0]
            + f64::from(colors[1][0]) * weights[1]
            + f64::from(colors[2][0]) * weights[2]) as f32,
        (f64::from(colors[0][1]) * weights[0]
            + f64::from(colors[1][1]) * weights[1]
            + f64::from(colors[2][1]) * weights[2]) as f32,
        (f64::from(colors[0][2]) * weights[0]
            + f64::from(colors[1][2]) * weights[1]
            + f64::from(colors[2][2]) * weights[2]) as f32,
    ]
}

/// Interpolates and normalizes three packed vertex normals.
pub(crate) fn interpolate_normal(normals: [[f32; 3]; 3], weights: [f64; 3]) -> [f32; 3] {
    let normal = [
        f64::from(normals[0][0]) * weights[0]
            + f64::from(normals[1][0]) * weights[1]
            + f64::from(normals[2][0]) * weights[2],
        f64::from(normals[0][1]) * weights[0]
            + f64::from(normals[1][1]) * weights[1]
            + f64::from(normals[2][1]) * weights[2],
        f64::from(normals[0][2]) * weights[0]
            + f64::from(normals[1][2]) * weights[1]
            + f64::from(normals[2][2]) * weights[2],
    ];

    normalize3(normal)
}

/// Interpolates and normalizes material weights for a triangle hit.
pub(crate) fn interpolate_material_weights(
    weights: [[f32; 4]; 3],
    barycentric: [f64; 3],
) -> [f32; 4] {
    normalize_weights([
        (f64::from(weights[0][0]) * barycentric[0]
            + f64::from(weights[1][0]) * barycentric[1]
            + f64::from(weights[2][0]) * barycentric[2]) as f32,
        (f64::from(weights[0][1]) * barycentric[0]
            + f64::from(weights[1][1]) * barycentric[1]
            + f64::from(weights[2][1]) * barycentric[2]) as f32,
        (f64::from(weights[0][2]) * barycentric[0]
            + f64::from(weights[1][2]) * barycentric[1]
            + f64::from(weights[2][2]) * barycentric[2]) as f32,
        (f64::from(weights[0][3]) * barycentric[0]
            + f64::from(weights[1][3]) * barycentric[1]
            + f64::from(weights[2][3]) * barycentric[2]) as f32,
    ])
}

/// Returns the inclusive bin range touched by one projected triangle interval.
pub(crate) fn bin_range_for_interval(
    min: f64,
    max: f64,
    origin: f64,
    span: f64,
    bins_per_axis: u16,
) -> Option<(u16, u16)> {
    let end = origin + span;
    if !min.is_finite() || !max.is_finite() || max < origin || min >= end {
        return None;
    }

    let min_bin = coordinate_to_bin(min.max(origin), origin, span, bins_per_axis);
    let max_bin = coordinate_to_bin(max.min(end), origin, span, bins_per_axis);
    Some((min_bin.min(max_bin), min_bin.max(max_bin)))
}

/// Maps one world coordinate into the owning bin along one axis.
pub(crate) fn coordinate_to_bin(value: f64, origin: f64, span: f64, bins_per_axis: u16) -> u16 {
    let normalized = ((value - origin) / span).clamp(0.0, 1.0);
    let scaled = normalized * f64::from(bins_per_axis);
    let index = scaled.floor() as i32;

    index.clamp(0, i32::from(bins_per_axis) - 1) as u16
}

/// Returns the flat bin offset for an XZ bin coordinate.
pub(crate) fn bin_index(x: u16, z: u16, bins_per_axis: u16) -> usize {
    usize::from(x) + usize::from(z) * usize::from(bins_per_axis)
}

/// Returns whether the query XZ is within the triangle projection bounds.
pub(crate) fn triangle_xz_bounds_contain(
    triangle: &TerrainSurfaceTriangle,
    x: f64,
    z: f64,
) -> bool {
    x >= triangle.x_min - BARYCENTRIC_EPSILON
        && x <= triangle.x_max + BARYCENTRIC_EPSILON
        && z >= triangle.z_min - BARYCENTRIC_EPSILON
        && z <= triangle.z_max + BARYCENTRIC_EPSILON
}

fn read_position(mesh: &MeshData, index: u32) -> Option<[f64; 3]> {
    let offset = usize::try_from(index)
        .ok()?
        .checked_mul(FLOATS_PER_VERTEX)?;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;
    let position = [
        f64::from(vertex[0]),
        f64::from(vertex[1]),
        f64::from(vertex[2]),
    ];

    position
        .iter()
        .all(|value| value.is_finite())
        .then_some(position)
}

fn read_normal(mesh: &MeshData, index: u32) -> Option<[f32; 3]> {
    let offset = usize::try_from(index)
        .ok()?
        .checked_mul(FLOATS_PER_VERTEX)?;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;
    let normal = [vertex[6], vertex[7], vertex[8]];

    normal
        .iter()
        .all(|value| value.is_finite())
        .then_some(normal)
}

fn read_color(mesh: &MeshData, index: u32) -> Option<[f32; 3]> {
    let offset = usize::try_from(index)
        .ok()?
        .checked_mul(FLOATS_PER_VERTEX)?;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;
    let color = [vertex[3], vertex[4], vertex[5]];

    color.iter().all(|value| value.is_finite()).then_some(color)
}

fn read_material_indices(mesh: &MeshData, index: u32) -> Option<[u8; 4]> {
    let offset = usize::try_from(index)
        .ok()?
        .checked_mul(FLOATS_PER_VERTEX)?;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;
    let mut indices = [0_u8; 4];

    for slot in 0..4 {
        let value = vertex[MATERIAL_INDICES_VERTEX_OFFSET + slot];
        if !value.is_finite() {
            return None;
        }
        indices[slot] = value.round().clamp(0.0, f32::from(u8::MAX)) as u8;
    }

    Some(indices)
}

fn read_material_weights(mesh: &MeshData, index: u32) -> Option<[f32; 4]> {
    let offset = usize::try_from(index)
        .ok()?
        .checked_mul(FLOATS_PER_VERTEX)?;
    let vertex = mesh.vertices.get(offset..offset + FLOATS_PER_VERTEX)?;
    let mut weights = [0.0_f32; 4];

    for slot in 0..4 {
        let value = vertex[MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
        if !value.is_finite() {
            return None;
        }
        weights[slot] = value.max(0.0);
    }

    Some(normalize_weights(weights))
}

fn projected_barycentric_denominator(positions: [[f64; 3]; 3]) -> f64 {
    (positions[1][2] - positions[2][2]) * (positions[0][0] - positions[2][0])
        + (positions[2][0] - positions[1][0]) * (positions[0][2] - positions[2][2])
}

fn projected_barycentric_weights(
    positions: [[f64; 3]; 3],
    denominator: f64,
    x: f64,
    z: f64,
) -> [f64; 3] {
    let a = ((positions[1][2] - positions[2][2]) * (x - positions[2][0])
        + (positions[2][0] - positions[1][0]) * (z - positions[2][2]))
        / denominator;
    let b = ((positions[2][2] - positions[0][2]) * (x - positions[2][0])
        + (positions[0][0] - positions[2][0]) * (z - positions[2][2]))
        / denominator;
    let c = 1.0 - a - b;

    [a, b, c]
}

fn normalize_barycentric_weights(weights: [f64; 3]) -> [f64; 3] {
    let clamped = [
        weights[0].clamp(0.0, 1.0),
        weights[1].clamp(0.0, 1.0),
        weights[2].clamp(0.0, 1.0),
    ];
    let total = clamped[0] + clamped[1] + clamped[2];
    if total <= f64::EPSILON {
        return [1.0, 0.0, 0.0];
    }

    [clamped[0] / total, clamped[1] / total, clamped[2] / total]
}

fn normalize_weights(mut weights: [f32; 4]) -> [f32; 4] {
    let total: f32 = weights.iter().sum();
    if total <= f32::EPSILON || !total.is_finite() {
        return [1.0, 0.0, 0.0, 0.0];
    }

    for weight in &mut weights {
        *weight /= total;
    }
    weights
}

fn geometric_normal_for_positions(positions: [[f64; 3]; 3]) -> Option<[f32; 3]> {
    let a = [
        positions[1][0] - positions[0][0],
        positions[1][1] - positions[0][1],
        positions[1][2] - positions[0][2],
    ];
    let b = [
        positions[2][0] - positions[0][0],
        positions[2][1] - positions[0][1],
        positions[2][2] - positions[0][2],
    ];

    let normal = [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ];
    let normalized = normalize3(normal);
    if normalized == [0.0, 0.0, 0.0] {
        return None;
    }

    Some(normalized)
}

fn normalize3(value: [f64; 3]) -> [f32; 3] {
    let length = (value[0] * value[0] + value[1] * value[1] + value[2] * value[2]).sqrt();
    if length <= f64::EPSILON || !length.is_finite() {
        return [0.0, 0.0, 0.0];
    }

    [
        (value[0] / length) as f32,
        (value[1] / length) as f32,
        (value[2] / length) as f32,
    ]
}

fn dot3(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
