mod engine;
mod facade;
mod math;
mod player;
mod render_packet;
mod world;

pub const ENGINE_CORE_VERSION: u32 = 1;

pub use engine::{
    Engine, EngineDebugSnapshot, EngineError, EngineUpdateInput, EngineUpdateSummary,
};
pub use facade::*;
pub use math::{Quat, Vec3};
pub use player::{EyeTransform, PlayerConfig, PlayerMode, PlayerMovementIntent, PlayerRig};
pub use render_packet::{
    RenderCameraPacket, RenderDebugMarkerPacket, RenderLightPacket, RenderSnapshot,
    RENDER_SNAPSHOT_FLOAT_COUNT,
};
pub use world::{EntityId, LocalTransform, World, WorldError, WorldTransform};

#[cfg(test)]
mod tests;
