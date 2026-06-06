mod chunk;
mod constants;
mod density;
mod facade;
mod field;
mod material;
mod math;
mod mesh;
mod mesh_packet_store;
mod noise;
mod presets;
mod store;
mod stream;
mod worker_pool;

pub(crate) use chunk::*;
pub(crate) use constants::*;
pub(crate) use density::*;
#[allow(unused_imports)]
pub(crate) use facade::*;
pub(crate) use field::*;
pub(crate) use material::*;
pub(crate) use math::*;
pub(crate) use mesh_packet_store::*;
pub(crate) use noise::*;
pub(crate) use presets::*;
pub(crate) use store::*;
#[allow(unused_imports)]
pub(crate) use stream::*;
pub(crate) use worker_pool::*;

pub use chunk::{terrain_chunk_coord_containing_position, terrain_chunk_key, TerrainChunkCoord};
pub use constants::{DEFAULT_TERRAIN_PRESET, TERRAIN_CHUNK_CELLS_PER_AXIS};
pub use field::height_at;
pub use mesh::{build_chunk_mesh, MeshData};
pub use stream::{
    TerrainStreamConfig, TerrainStreamError, TerrainStreamJob, TerrainStreamScheduler,
    TerrainStreamStatus,
};

#[cfg(test)]
mod tests;
