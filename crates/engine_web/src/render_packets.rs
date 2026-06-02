use crate::render_uniforms::{inverse_mat4, FRAME_PACKET_FLOATS, WORLD_MATRIX_FLOATS};

pub const ENGINE_RENDER_SNAPSHOT_FLOATS: usize = 24;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RenderPacketError {
    InvalidEngineSnapshot,
    InvalidAspect,
    InvalidCamera,
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Vec3 {
    x: f32,
    y: f32,
    z: f32,
}

impl std::fmt::Display for RenderPacketError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidEngineSnapshot => "invalid Rust engine render snapshot",
            Self::InvalidAspect => "invalid Rust WebGPU frame aspect ratio",
            Self::InvalidCamera => "invalid Rust engine camera render packet",
        };
        formatter.write_str(message)
    }
}

pub fn build_frame_packet_from_engine_snapshot(
    snapshot: &[f32],
    aspect: f32,
) -> Result<[f32; FRAME_PACKET_FLOATS], RenderPacketError> {
    if snapshot.len() != ENGINE_RENDER_SNAPSHOT_FLOATS {
        return Err(RenderPacketError::InvalidEngineSnapshot);
    }
    if !aspect.is_finite() || aspect <= 0.0 {
        return Err(RenderPacketError::InvalidAspect);
    }

    let eye = Vec3::new(snapshot[0], snapshot[1], snapshot[2]);
    let target = Vec3::new(snapshot[3], snapshot[4], snapshot[5]);
    let projection = perspective_mat4(snapshot[8], aspect, snapshot[9], snapshot[10])
        .ok_or(RenderPacketError::InvalidCamera)?;
    let view = look_at_mat4(eye, target).ok_or(RenderPacketError::InvalidCamera)?;
    let view_projection = multiply_mat4(&projection, &view);
    let inverse_view_projection =
        inverse_mat4(&view_projection).ok_or(RenderPacketError::InvalidCamera)?;

    let mut frame = [0.0; FRAME_PACKET_FLOATS];
    frame[0..16].copy_from_slice(&view_projection);
    frame[16..32].copy_from_slice(&inverse_view_projection);
    frame[32..35].copy_from_slice(&snapshot[0..3]);
    frame[35..38].copy_from_slice(&snapshot[11..14]);
    frame[38..41].copy_from_slice(&snapshot[14..17]);
    frame[41] = snapshot[17];
    frame[42] = snapshot[18];

    Ok(frame)
}

pub fn build_player_marker_world_matrix(
    snapshot: &[f32],
) -> Result<Option<[f32; WORLD_MATRIX_FLOATS]>, RenderPacketError> {
    if snapshot.len() != ENGINE_RENDER_SNAPSHOT_FLOATS {
        return Err(RenderPacketError::InvalidEngineSnapshot);
    }

    if snapshot[19] < 0.5 {
        return Ok(None);
    }

    let mut matrix = identity_mat4();
    matrix[12] = snapshot[20];
    matrix[13] = snapshot[21];
    matrix[14] = snapshot[22];
    Ok(Some(matrix))
}

fn identity_mat4() -> [f32; WORLD_MATRIX_FLOATS] {
    [
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
    ]
}

fn perspective_mat4(
    fov_y_radians: f32,
    aspect: f32,
    near: f32,
    far: f32,
) -> Option<[f32; WORLD_MATRIX_FLOATS]> {
    if !fov_y_radians.is_finite()
        || !near.is_finite()
        || !far.is_finite()
        || fov_y_radians <= 0.0
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

fn look_at_mat4(eye: Vec3, target: Vec3) -> Option<[f32; WORLD_MATRIX_FLOATS]> {
    let z_axis = eye.sub(target).normalize()?;
    let x_axis = Vec3::UP.cross(z_axis).normalize()?;
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

fn multiply_mat4(a: &[f32; WORLD_MATRIX_FLOATS], b: &[f32; WORLD_MATRIX_FLOATS]) -> [f32; 16] {
    let mut out = [0.0; WORLD_MATRIX_FLOATS];
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

impl Vec3 {
    const UP: Self = Self::new(0.0, 1.0, 0.0);

    const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    fn sub(self, other: Self) -> Self {
        Self::new(self.x - other.x, self.y - other.y, self.z - other.z)
    }

    fn dot(self, other: Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }

    fn cross(self, other: Self) -> Self {
        Self::new(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x,
        )
    }

    fn normalize(self) -> Option<Self> {
        let length = self.dot(self).sqrt();
        if !length.is_finite() || length <= f32::EPSILON {
            return None;
        }

        Some(Self::new(self.x / length, self.y / length, self.z / length))
    }
}
