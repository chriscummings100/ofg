mod config;
mod facade;
mod renderer;
mod resources;

pub const ENGINE_WEB_VERSION: u32 = 1;

pub use config::{
    RendererConfig, RendererConfigError, REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_VERTEX_FLOATS,
    TEXTURE_FORMAT_RGBA8_UNORM,
};
pub use facade::*;
pub use renderer::{
    MeshResource, RendererResourceCounts, RendererState, RendererStateError, TextureResource,
};
pub use resources::{ResourceHandle, ResourceStoreError};

#[cfg(test)]
mod tests;
