//! Serializable browser runtime status shared by tests and the WASM facade.

use serde::{Deserialize, Serialize};

/// Debug status consumed by TypeScript and browser smoke tests.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RuntimeDebugStatus {
    pub initialized: bool,
    pub frame_count: u64,
    pub canvas_width: u32,
    pub canvas_height: u32,
    pub device_pixel_ratio: f64,
    pub surface_format: String,
    pub adapter_name: String,
    pub backend: String,
    pub pipeline_create_count: u32,
    pub buffer_create_count: u32,
    pub surface_configure_count: u32,
    pub last_error: Option<String>,
}

impl RuntimeDebugStatus {
    /// Creates a status before the GPU runtime is available.
    pub fn uninitialized(message: impl Into<String>) -> Self {
        Self {
            initialized: false,
            frame_count: 0,
            canvas_width: 0,
            canvas_height: 0,
            device_pixel_ratio: 1.0,
            surface_format: String::new(),
            adapter_name: String::new(),
            backend: String::new(),
            pipeline_create_count: 0,
            buffer_create_count: 0,
            surface_configure_count: 0,
            last_error: Some(message.into()),
        }
    }

    /// Serializes status for the narrow TypeScript wrapper.
    pub fn to_json(&self) -> String {
        serde_json::to_string(self).expect("runtime debug status should serialize")
    }
}
