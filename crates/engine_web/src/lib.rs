mod config;
mod facade;
mod game_state;
mod materials;
mod render_packets;
mod render_uniforms;
mod renderer;
mod resources;
#[cfg(target_arch = "wasm32")]
mod wgpu_renderer;

pub const ENGINE_WEB_VERSION: u32 = 1;

pub use config::{
    RendererConfig, RendererConfigError, REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_VERTEX_FLOATS,
    TEXTURE_FORMAT_RGBA8_UNORM,
};
pub use facade::*;
pub use game_state::{
    player_mode_code, player_mode_from_code, BrowserGameInput, BrowserGameState,
    BrowserGameStateError,
};
pub use materials::{
    build_material_packet, MaterialPacketError, DEFAULT_MATERIAL_PACKET, TERRAIN_MATERIAL_ID,
    TERRAIN_MATERIAL_PACKET,
};
pub use render_packets::{
    build_frame_packet_from_engine_snapshot, build_player_marker_world_matrix, RenderPacketError,
    ENGINE_RENDER_SNAPSHOT_FLOATS,
};
pub use render_uniforms::{
    build_frame_uniform_values, build_object_uniform_values, RenderUniformError,
    FRAME_PACKET_FLOATS, FRAME_UNIFORM_FLOATS, MATERIAL_PACKET_FLOATS, OBJECT_UNIFORM_FLOATS,
    WORLD_MATRIX_FLOATS,
};
pub use renderer::{
    MeshResource, RendererResourceCounts, RendererState, RendererStateError, TextureResource,
};
pub use resources::{ResourceHandle, ResourceStoreError};
#[cfg(target_arch = "wasm32")]
pub use wgpu_renderer::*;

#[cfg(test)]
mod tests;
