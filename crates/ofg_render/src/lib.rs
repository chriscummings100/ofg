//! Shared bootstrap renderer for browser WebGPU and native smoke tests.

pub mod bootstrap_scene;
pub mod renderer;

pub use bootstrap_scene::{clear_color, clear_color_rgba8, BootstrapVertex, BOOTSTRAP_VERTICES};
pub use renderer::{BootstrapRenderer, RendererCounters};
