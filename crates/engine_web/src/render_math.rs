// Shared render-space math helpers for camera packets, culling, and shadows.
// Matrices are stored column-major to match WGSL and wgpu uniform packing.

pub const MATRIX_FLOATS: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderVec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Aabb {
    pub min: RenderVec3,
    pub max: RenderVec3,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Plane {
    pub normal: RenderVec3,
    pub distance: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Frustum {
    pub planes: [Plane; 6],
}

impl RenderVec3 {
    pub const ZERO: Self = Self::new(0.0, 0.0, 0.0);
    pub const UP: Self = Self::new(0.0, 1.0, 0.0);

    /// Creates a render-space vector from three `f32` components.
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    /// Returns the component-wise sum of two vectors.
    pub fn add(self, other: Self) -> Self {
        Self::new(self.x + other.x, self.y + other.y, self.z + other.z)
    }

    /// Returns the component-wise difference between two vectors.
    pub fn sub(self, other: Self) -> Self {
        Self::new(self.x - other.x, self.y - other.y, self.z - other.z)
    }

    /// Returns this vector scaled by a scalar amount.
    pub fn scale(self, amount: f32) -> Self {
        Self::new(self.x * amount, self.y * amount, self.z * amount)
    }

    /// Returns the dot product between two vectors.
    pub fn dot(self, other: Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }

    /// Returns the right-handed cross product between two vectors.
    pub fn cross(self, other: Self) -> Self {
        Self::new(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x,
        )
    }

    /// Returns the Euclidean length of this vector.
    pub fn length(self) -> f32 {
        self.dot(self).sqrt()
    }

    /// Returns a unit-length vector, or `None` for zero or invalid vectors.
    pub fn normalize(self) -> Option<Self> {
        let length = self.length();
        if !length.is_finite() || length <= f32::EPSILON {
            return None;
        }

        Some(self.scale(1.0 / length))
    }

    /// Returns true when every vector component is finite.
    pub fn is_finite(self) -> bool {
        self.x.is_finite() && self.y.is_finite() && self.z.is_finite()
    }
}

impl Aabb {
    /// Returns the eight world-space or local-space corners of this box.
    pub fn corners(self) -> [RenderVec3; 8] {
        [
            RenderVec3::new(self.min.x, self.min.y, self.min.z),
            RenderVec3::new(self.max.x, self.min.y, self.min.z),
            RenderVec3::new(self.min.x, self.max.y, self.min.z),
            RenderVec3::new(self.max.x, self.max.y, self.min.z),
            RenderVec3::new(self.min.x, self.min.y, self.max.z),
            RenderVec3::new(self.max.x, self.min.y, self.max.z),
            RenderVec3::new(self.min.x, self.max.y, self.max.z),
            RenderVec3::new(self.max.x, self.max.y, self.max.z),
        ]
    }
}

/// Builds an AABB from interleaved vertex positions.
pub fn aabb_from_vertex_positions(
    vertices: &[f32],
    floats_per_vertex: u32,
    position_offset: usize,
) -> Option<Aabb> {
    let stride = floats_per_vertex as usize;
    if stride == 0
        || vertices.is_empty()
        || vertices.len() % stride != 0
        || position_offset + 2 >= stride
    {
        return None;
    }

    let mut min = RenderVec3::new(f32::INFINITY, f32::INFINITY, f32::INFINITY);
    let mut max = RenderVec3::new(f32::NEG_INFINITY, f32::NEG_INFINITY, f32::NEG_INFINITY);
    for vertex in vertices.chunks_exact(stride) {
        let position = RenderVec3::new(
            vertex[position_offset],
            vertex[position_offset + 1],
            vertex[position_offset + 2],
        );
        if !position.is_finite() {
            return None;
        }

        min.x = min.x.min(position.x);
        min.y = min.y.min(position.y);
        min.z = min.z.min(position.z);
        max.x = max.x.max(position.x);
        max.y = max.y.max(position.y);
        max.z = max.z.max(position.z);
    }

    Some(Aabb { min, max })
}

/// Transforms an AABB by an affine matrix and returns a conservative AABB.
pub fn transform_aabb(aabb: Aabb, world_matrix: &[f32; MATRIX_FLOATS]) -> Aabb {
    let mut min = RenderVec3::new(f32::INFINITY, f32::INFINITY, f32::INFINITY);
    let mut max = RenderVec3::new(f32::NEG_INFINITY, f32::NEG_INFINITY, f32::NEG_INFINITY);

    for corner in aabb.corners() {
        let transformed = transform_point(world_matrix, corner);
        min.x = min.x.min(transformed.x);
        min.y = min.y.min(transformed.y);
        min.z = min.z.min(transformed.z);
        max.x = max.x.max(transformed.x);
        max.y = max.y.max(transformed.y);
        max.z = max.z.max(transformed.z);
    }

    Aabb { min, max }
}

/// Extracts WebGPU clip-space frustum planes from a column-major view-projection matrix.
pub fn frustum_from_view_projection(matrix: &[f32; MATRIX_FLOATS]) -> Option<Frustum> {
    let row0 = matrix_row(matrix, 0);
    let row1 = matrix_row(matrix, 1);
    let row2 = matrix_row(matrix, 2);
    let row3 = matrix_row(matrix, 3);

    Some(Frustum {
        planes: [
            plane_from_coefficients(add_row(row3, row0))?,
            plane_from_coefficients(sub_row(row3, row0))?,
            plane_from_coefficients(add_row(row3, row1))?,
            plane_from_coefficients(sub_row(row3, row1))?,
            plane_from_coefficients(row2)?,
            plane_from_coefficients(sub_row(row3, row2))?,
        ],
    })
}

/// Returns true when an AABB is at least partially inside the frustum.
pub fn frustum_intersects_aabb(frustum: Frustum, aabb: Aabb) -> bool {
    for plane in frustum.planes {
        let positive = RenderVec3::new(
            if plane.normal.x >= 0.0 {
                aabb.max.x
            } else {
                aabb.min.x
            },
            if plane.normal.y >= 0.0 {
                aabb.max.y
            } else {
                aabb.min.y
            },
            if plane.normal.z >= 0.0 {
                aabb.max.z
            } else {
                aabb.min.z
            },
        );
        if plane.distance_to(positive) < 0.0 {
            return false;
        }
    }

    true
}

/// Builds a right-handed perspective projection matrix for WebGPU depth range.
pub fn perspective_mat4(
    fov_y_radians: f32,
    aspect: f32,
    near: f32,
    far: f32,
) -> Option<[f32; MATRIX_FLOATS]> {
    if !fov_y_radians.is_finite()
        || !aspect.is_finite()
        || !near.is_finite()
        || !far.is_finite()
        || fov_y_radians <= 0.0
        || aspect <= 0.0
        || near <= 0.0
        || far <= near
    {
        return None;
    }

    let f = 1.0 / (fov_y_radians / 2.0).tan();
    let range_inv = 1.0 / (near - far);
    Some([
        f / aspect,
        0.0,
        0.0,
        0.0,
        0.0,
        f,
        0.0,
        0.0,
        0.0,
        0.0,
        far * range_inv,
        -1.0,
        0.0,
        0.0,
        far * near * range_inv,
        0.0,
    ])
}

/// Builds a right-handed look-at view matrix.
pub fn look_at_mat4(
    eye: RenderVec3,
    target: RenderVec3,
    up: RenderVec3,
) -> Option<[f32; MATRIX_FLOATS]> {
    let z_axis = eye.sub(target).normalize()?;
    let x_axis = up.cross(z_axis).normalize()?;
    let y_axis = z_axis.cross(x_axis);

    Some([
        x_axis.x,
        y_axis.x,
        z_axis.x,
        0.0,
        x_axis.y,
        y_axis.y,
        z_axis.y,
        0.0,
        x_axis.z,
        y_axis.z,
        z_axis.z,
        0.0,
        -x_axis.dot(eye),
        -y_axis.dot(eye),
        -z_axis.dot(eye),
        1.0,
    ])
}

/// Builds a right-handed orthographic projection matrix for WebGPU depth range.
pub fn orthographic_mat4(
    left: f32,
    right: f32,
    bottom: f32,
    top: f32,
    near: f32,
    far: f32,
) -> Option<[f32; MATRIX_FLOATS]> {
    if !left.is_finite()
        || !right.is_finite()
        || !bottom.is_finite()
        || !top.is_finite()
        || !near.is_finite()
        || !far.is_finite()
        || right <= left
        || top <= bottom
        || far <= near
    {
        return None;
    }

    Some([
        2.0 / (right - left),
        0.0,
        0.0,
        0.0,
        0.0,
        2.0 / (top - bottom),
        0.0,
        0.0,
        0.0,
        0.0,
        1.0 / (near - far),
        0.0,
        -(right + left) / (right - left),
        -(top + bottom) / (top - bottom),
        near / (near - far),
        1.0,
    ])
}

/// Multiplies two column-major 4x4 matrices.
pub fn multiply_mat4(a: &[f32; MATRIX_FLOATS], b: &[f32; MATRIX_FLOATS]) -> [f32; MATRIX_FLOATS] {
    let mut out = [0.0; MATRIX_FLOATS];
    for column in 0..4 {
        for row in 0..4 {
            out[column * 4 + row] = a[row] * b[column * 4]
                + a[4 + row] * b[column * 4 + 1]
                + a[8 + row] * b[column * 4 + 2]
                + a[12 + row] * b[column * 4 + 3];
        }
    }
    out
}

/// Transforms a point by a column-major 4x4 matrix, dividing by `w` when needed.
pub fn transform_point(matrix: &[f32; MATRIX_FLOATS], point: RenderVec3) -> RenderVec3 {
    let x = matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12];
    let y = matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13];
    let z = matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14];
    let w = matrix[3] * point.x + matrix[7] * point.y + matrix[11] * point.z + matrix[15];

    if w.is_finite() && w.abs() > f32::EPSILON {
        return RenderVec3::new(x / w, y / w, z / w);
    }

    RenderVec3::new(x, y, z)
}

impl Plane {
    /// Returns signed distance from this plane to a point.
    pub fn distance_to(self, point: RenderVec3) -> f32 {
        self.normal.dot(point) + self.distance
    }
}

fn matrix_row(matrix: &[f32; MATRIX_FLOATS], row: usize) -> [f32; 4] {
    [
        matrix[row],
        matrix[4 + row],
        matrix[8 + row],
        matrix[12 + row],
    ]
}

fn add_row(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]]
}

fn sub_row(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]]
}

fn plane_from_coefficients(coefficients: [f32; 4]) -> Option<Plane> {
    let normal = RenderVec3::new(coefficients[0], coefficients[1], coefficients[2]);
    let length = normal.length();
    if !length.is_finite() || length <= f32::EPSILON || !coefficients[3].is_finite() {
        return None;
    }

    Some(Plane {
        normal: normal.scale(1.0 / length),
        distance: coefficients[3] / length,
    })
}
