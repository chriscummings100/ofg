use crate::config::SHADOW_CASCADE_COUNT;
use crate::shadows::ShadowCascadeSet;

pub const FRAME_UNIFORM_FLOATS: usize = 56;
pub const OBJECT_UNIFORM_FLOATS: usize = 44;
pub const FRAME_PACKET_FLOATS: usize = 55;
pub const WORLD_MATRIX_FLOATS: usize = 16;
pub const MATERIAL_PACKET_FLOATS: usize = 10;
pub const SHADOW_UNIFORM_FLOATS: usize = 76;
pub const FRAME_UNIFORM_SKY_CLOUD_COVERAGE_OFFSET: usize = 49;
pub const SHADOW_DEBUG_MODE_OFFSET: usize = 72;
pub const SHADOW_STRENGTH_OFFSET: usize = 75;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RenderUniformError {
    InvalidFramePacket,
    InvalidObjectPacket,
    InvalidShadowPacket,
    SingularWorldMatrix,
}

impl std::fmt::Display for RenderUniformError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidFramePacket => "invalid Rust WebGPU frame packet",
            Self::InvalidObjectPacket => "invalid Rust WebGPU object packet",
            Self::InvalidShadowPacket => "invalid Rust WebGPU shadow packet",
            Self::SingularWorldMatrix => "singular Rust WebGPU world matrix",
        };
        formatter.write_str(message)
    }
}

pub fn build_frame_uniform_values(
    frame_packet: &[f32],
) -> Result<[f32; FRAME_UNIFORM_FLOATS], RenderUniformError> {
    if frame_packet.len() != FRAME_PACKET_FLOATS {
        return Err(RenderUniformError::InvalidFramePacket);
    }

    let mut values = [0.0; FRAME_UNIFORM_FLOATS];
    values[0..16].copy_from_slice(&frame_packet[0..16]);
    values[16..32].copy_from_slice(&frame_packet[16..32]);
    values[32..35].copy_from_slice(&frame_packet[32..35]);
    values[35] = 1.0;
    values[36..39].copy_from_slice(&frame_packet[35..38]);
    values[39] = frame_packet[41];
    values[40..43].copy_from_slice(&frame_packet[38..41]);
    values[43] = frame_packet[42];
    values[44..56].copy_from_slice(&frame_packet[43..55]);

    Ok(values)
}

pub fn build_object_uniform_values(
    world_matrix: &[f32],
    material_packet: &[f32],
) -> Result<[f32; OBJECT_UNIFORM_FLOATS], RenderUniformError> {
    if world_matrix.len() != WORLD_MATRIX_FLOATS || material_packet.len() != MATERIAL_PACKET_FLOATS
    {
        return Err(RenderUniformError::InvalidObjectPacket);
    }

    let normal_matrix =
        transpose_mat4(inverse_mat4(world_matrix).ok_or(RenderUniformError::SingularWorldMatrix)?);
    let mut values = [0.0; OBJECT_UNIFORM_FLOATS];
    values[0..16].copy_from_slice(world_matrix);
    values[16..32].copy_from_slice(&normal_matrix);
    values[32..36].copy_from_slice(&material_packet[0..4]);
    values[36..39].copy_from_slice(&material_packet[4..7]);
    values[39] = material_packet[7];
    values[40] = material_packet[8];
    values[41] = material_packet[9];

    Ok(values)
}

/// Packs the shadow uniform buffer consumed by future CSM WGSL paths.
pub fn build_shadow_uniform_values(
    cascades: &ShadowCascadeSet,
    enabled: bool,
    constant_bias: f32,
    normal_bias: f32,
    texel_size: f32,
) -> Result<[f32; SHADOW_UNIFORM_FLOATS], RenderUniformError> {
    if !constant_bias.is_finite()
        || !normal_bias.is_finite()
        || !texel_size.is_finite()
        || constant_bias < 0.0
        || normal_bias < 0.0
        || texel_size < 0.0
        || (enabled && texel_size <= 0.0)
    {
        return Err(RenderUniformError::InvalidShadowPacket);
    }

    let mut values = [0.0; SHADOW_UNIFORM_FLOATS];
    let mut previous_split = 0.0;
    for index in 0..SHADOW_CASCADE_COUNT {
        let cascade = cascades.cascades[index];
        let split = cascades.split_depths[index];
        if !cascade.near_depth.is_finite()
            || !cascade.far_depth.is_finite()
            || !split.is_finite()
            || cascade.near_depth < 0.0
            || cascade.far_depth <= cascade.near_depth
            || split <= previous_split
            || (split - cascade.far_depth).abs() > 0.001
            || !matrix_values_are_finite(&cascade.light_view_projection)
        {
            return Err(RenderUniformError::InvalidShadowPacket);
        }

        let matrix_offset = index * WORLD_MATRIX_FLOATS;
        values[matrix_offset..matrix_offset + WORLD_MATRIX_FLOATS]
            .copy_from_slice(&cascade.light_view_projection);
        values[64 + index] = split;
        previous_split = split;
    }

    values[68] = if enabled { 1.0 } else { 0.0 };
    values[69] = constant_bias;
    values[70] = normal_bias;
    values[71] = texel_size;

    Ok(values)
}

fn transpose_mat4(matrix: [f32; WORLD_MATRIX_FLOATS]) -> [f32; WORLD_MATRIX_FLOATS] {
    [
        matrix[0], matrix[4], matrix[8], matrix[12], matrix[1], matrix[5], matrix[9], matrix[13],
        matrix[2], matrix[6], matrix[10], matrix[14], matrix[3], matrix[7], matrix[11], matrix[15],
    ]
}

pub(crate) fn inverse_mat4(matrix: &[f32]) -> Option<[f32; WORLD_MATRIX_FLOATS]> {
    let a00 = matrix[0];
    let a01 = matrix[1];
    let a02 = matrix[2];
    let a03 = matrix[3];
    let a10 = matrix[4];
    let a11 = matrix[5];
    let a12 = matrix[6];
    let a13 = matrix[7];
    let a20 = matrix[8];
    let a21 = matrix[9];
    let a22 = matrix[10];
    let a23 = matrix[11];
    let a30 = matrix[12];
    let a31 = matrix[13];
    let a32 = matrix[14];
    let a33 = matrix[15];

    let b00 = a00 * a11 - a01 * a10;
    let b01 = a00 * a12 - a02 * a10;
    let b02 = a00 * a13 - a03 * a10;
    let b03 = a01 * a12 - a02 * a11;
    let b04 = a01 * a13 - a03 * a11;
    let b05 = a02 * a13 - a03 * a12;
    let b06 = a20 * a31 - a21 * a30;
    let b07 = a20 * a32 - a22 * a30;
    let b08 = a20 * a33 - a23 * a30;
    let b09 = a21 * a32 - a22 * a31;
    let b10 = a21 * a33 - a23 * a31;
    let b11 = a22 * a33 - a23 * a32;

    let determinant = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if determinant.abs() <= f32::EPSILON {
        return None;
    }

    let determinant = 1.0 / determinant;
    Some([
        (a11 * b11 - a12 * b10 + a13 * b09) * determinant,
        (a02 * b10 - a01 * b11 - a03 * b09) * determinant,
        (a31 * b05 - a32 * b04 + a33 * b03) * determinant,
        (a22 * b04 - a21 * b05 - a23 * b03) * determinant,
        (a12 * b08 - a10 * b11 - a13 * b07) * determinant,
        (a00 * b11 - a02 * b08 + a03 * b07) * determinant,
        (a32 * b02 - a30 * b05 - a33 * b01) * determinant,
        (a20 * b05 - a22 * b02 + a23 * b01) * determinant,
        (a10 * b10 - a11 * b08 + a13 * b06) * determinant,
        (a01 * b08 - a00 * b10 - a03 * b06) * determinant,
        (a30 * b04 - a31 * b02 + a33 * b00) * determinant,
        (a21 * b02 - a20 * b04 - a23 * b00) * determinant,
        (a11 * b07 - a10 * b09 - a12 * b06) * determinant,
        (a00 * b09 - a01 * b07 + a02 * b06) * determinant,
        (a31 * b01 - a30 * b03 - a32 * b00) * determinant,
        (a20 * b03 - a21 * b01 + a22 * b00) * determinant,
    ])
}

fn matrix_values_are_finite(matrix: &[f32; WORLD_MATRIX_FLOATS]) -> bool {
    matrix.iter().all(|value| value.is_finite())
}
