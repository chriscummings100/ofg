mod engine;
mod facade;
mod math;
mod player;
mod render_packet;
mod scene;
mod scene_access;
mod scene_components;
mod scene_resources;

pub const ENGINE_CORE_VERSION: u32 = 1;

pub use engine::{
    Engine, EngineDebugSnapshot, EngineError, EngineUpdateInput, EngineUpdateSummary,
};
pub use facade::*;
pub use math::{Quat, Vec3};
pub use player::{EyeTransform, PlayerConfig, PlayerMode, PlayerMovementIntent, PlayerRig};
pub use render_packet::{
    RenderCameraPacket, RenderLightPacket, RenderMeshItemPacket, RenderSnapshot,
    RENDER_MESH_ITEM_WORLD_MATRIX_FLOAT_COUNT, RENDER_SNAPSHOT_FLOAT_COUNT,
};
pub use scene::{Entity, EntityId, LocalTransform, Scene, SceneError, WorldTransform};
pub use scene_access::{EntityMut, EntityRef};
pub use scene_components::{
    CameraComponent, Components, MeshRendererComponent, PlayerComponent, TerrainComponent,
};
pub use scene_resources::{
    MaterialId, MaterialResource, MeshId, MeshResource, ResourceId, SceneResources,
    DEBUG_PLAYER_MARKER_MATERIAL_LABEL, DEBUG_PLAYER_MARKER_MESH_LABEL,
};

#[cfg(test)]
mod tests;
