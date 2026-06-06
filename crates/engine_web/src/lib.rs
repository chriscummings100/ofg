mod config;
mod facade;
mod game_state;
mod materials;
mod render_packets;
mod render_uniforms;
mod renderer;
mod resources;
mod terrain_stream;
mod terrain_textures;
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
pub use terrain_stream::{
    BrowserTerrainMeshUpdate, BrowserTerrainStream, BrowserTerrainStreamStatus,
    BrowserTerrainStreamUpdate, TerrainJobStats,
};
pub use terrain_textures::{
    terrain_texture_array_requests, terrain_texture_array_requests_from_manifest_json,
    RgbaTextureArrayAsset, TerrainTextureArrayRequest, TerrainTextureArrays, TerrainTextureError,
    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID, TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
};
#[cfg(target_arch = "wasm32")]
pub use wgpu_renderer::*;

#[cfg(test)]
mod tests;
