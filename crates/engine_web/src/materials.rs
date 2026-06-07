use crate::render_uniforms::MATERIAL_PACKET_FLOATS;

pub const MATERIAL_WORKFLOW_SIMPLE: f32 = 0.0;
pub const MATERIAL_WORKFLOW_TERRAIN: f32 = 1.0;
pub const MATERIAL_WORKFLOW_METALLIC_ROUGHNESS: f32 = 2.0;
pub const MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS: f32 = 3.0;
pub const DEFAULT_MATERIAL_PACKET: [f32; MATERIAL_PACKET_FLOATS] = [
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    0.0,
    0.0,
    MATERIAL_WORKFLOW_METALLIC_ROUGHNESS,
    1.0,
];
pub const TERRAIN_MATERIAL_ID: &str = "material:terrain.seed";
pub const TERRAIN_MATERIAL_PACKET: [f32; MATERIAL_PACKET_FLOATS] = [
    1.0,
    1.0,
    1.0,
    1.0,
    0.55,
    0.58,
    0.52,
    0.04,
    MATERIAL_WORKFLOW_TERRAIN,
    0.08,
];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MaterialPacketError {
    InvalidValue,
    InvalidTextureScale,
}

impl std::fmt::Display for MaterialPacketError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidValue => "invalid Rust WebGPU material value",
            Self::InvalidTextureScale => "invalid Rust WebGPU material texture scale",
        };
        formatter.write_str(message)
    }
}

pub fn build_material_packet(
    albedo: [f32; 4],
    specular: [f32; 3],
    specular_factor: f32,
    flags: f32,
    texture_scale: f32,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], MaterialPacketError> {
    let mut packet = [0.0; MATERIAL_PACKET_FLOATS];
    packet[0..4].copy_from_slice(&albedo);
    packet[4..7].copy_from_slice(&specular);
    packet[7] = specular_factor;
    packet[8] = flags;
    packet[9] = texture_scale;

    if packet.iter().any(|value| !value.is_finite()) {
        return Err(MaterialPacketError::InvalidValue);
    }
    if texture_scale <= 0.0 {
        return Err(MaterialPacketError::InvalidTextureScale);
    }

    Ok(packet)
}

pub fn build_metallic_roughness_material_packet(
    base_color: [f32; 4],
    metallic_factor: f32,
    roughness_factor: f32,
    texture_scale: f32,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], MaterialPacketError> {
    build_material_packet(
        base_color,
        [metallic_factor, roughness_factor, 0.0],
        0.0,
        MATERIAL_WORKFLOW_METALLIC_ROUGHNESS,
        texture_scale,
    )
}

pub fn build_specular_glossiness_material_packet(
    diffuse: [f32; 4],
    specular: [f32; 3],
    glossiness_factor: f32,
    texture_scale: f32,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], MaterialPacketError> {
    build_material_packet(
        diffuse,
        specular,
        glossiness_factor,
        MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS,
        texture_scale,
    )
}
