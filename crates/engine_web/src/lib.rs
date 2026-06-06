mod config;
mod facade;
mod game_state;
mod materials;
mod model_animation;
mod model_asset_loader;
mod model_assets;
mod model_locomotion;
mod model_render_assets;
mod model_skinning;
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
    RendererConfig, RendererConfigError, MODEL_VERTEX_FLOATS, REQUIRED_TEXTURE_ARRAY_LAYERS,
    TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
};
pub use facade::*;
pub use game_state::{
    player_mode_code, player_mode_from_code, BrowserGameInput, BrowserGameState,
    BrowserGameStateError, BrowserSceneMeshItem,
};
pub use materials::{
    build_material_packet, MaterialPacketError, DEFAULT_MATERIAL_PACKET, TERRAIN_MATERIAL_ID,
    TERRAIN_MATERIAL_PACKET,
};
pub use model_animation::{
    blend_node_transforms, ModelAnimationChannel, ModelAnimationClip, ModelAnimationInterpolation,
    ModelAnimationOutputs, ModelAnimationTarget,
};
#[cfg(target_arch = "wasm32")]
pub use model_asset_loader::load_model_asset_bytes;
pub use model_assets::{
    import_gltf_model_from_slice, model_primitive_vertex_floats, ModelAsset, ModelAssetError,
    ModelMaterial, ModelNode, ModelNodeTransform, ModelPrimitive, ModelSkin, ModelVertex,
    PLAYER_QUATERNIUS_UAL2_MATERIAL_LABEL, PLAYER_QUATERNIUS_UAL2_MESH_LABEL,
    PLAYER_QUATERNIUS_UAL2_MODEL_ID, PLAYER_QUATERNIUS_UAL2_MODEL_URL,
    SAMPLE_ANIMATED_BOX_MATERIAL_LABEL, SAMPLE_ANIMATED_BOX_MESH_LABEL,
    SAMPLE_ANIMATED_BOX_MODEL_ID, SAMPLE_ANIMATED_BOX_MODEL_URL,
    SAMPLE_RIGGED_SIMPLE_MATERIAL_LABEL, SAMPLE_RIGGED_SIMPLE_MESH_LABEL,
    SAMPLE_RIGGED_SIMPLE_MODEL_ID, SAMPLE_RIGGED_SIMPLE_MODEL_URL,
    SAMPLE_STATIC_BOX_MATERIAL_LABEL, SAMPLE_STATIC_BOX_MESH_LABEL, SAMPLE_STATIC_BOX_MODEL_ID,
    SAMPLE_STATIC_BOX_MODEL_URL,
};
pub use model_locomotion::{
    horizontal_movement_is_active, LocomotionAnimationController, PlayerCharacterAnimationSnapshot,
    PlayerCharacterModel, PlayerCharacterModelError, QUATERNIUS_IDLE_CLIP_NAME,
    QUATERNIUS_WALK_CLIP_NAME,
};
pub use model_render_assets::{
    first_primitive_node_index, skinned_model_render_assets, ModelRenderAssetError,
    ModelRenderAssets,
};
pub use model_skinning::{model_node_world_matrices, skin_joint_matrices, skin_primitive_vertices};
pub use render_packets::{
    build_frame_packet_from_engine_snapshot, RenderPacketError, ENGINE_RENDER_SNAPSHOT_FLOATS,
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
