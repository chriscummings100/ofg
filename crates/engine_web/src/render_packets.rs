use crate::render_math::{look_at_mat4, multiply_mat4, perspective_mat4, RenderVec3};
use crate::render_uniforms::{inverse_mat4, FRAME_PACKET_FLOATS};

pub const ENGINE_RENDER_SNAPSHOT_FLOATS: usize = 19;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RenderPacketError {
    InvalidEngineSnapshot,
    InvalidAspect,
    InvalidCamera,
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

    let eye = RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]);
    let target = RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]);
    let projection = perspective_mat4(snapshot[8], aspect, snapshot[9], snapshot[10])
        .ok_or(RenderPacketError::InvalidCamera)?;
    let view = look_at_mat4(eye, target, RenderVec3::UP).ok_or(RenderPacketError::InvalidCamera)?;
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
