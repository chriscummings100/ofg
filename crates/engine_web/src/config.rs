pub const REQUIRED_TEXTURE_ARRAY_LAYERS: u32 = 16;
pub const MODEL_VERTEX_FLOATS: u32 = 12;
pub const TERRAIN_VERTEX_FLOATS: u32 = 19;
pub const TEXTURE_FORMAT_RGBA8_UNORM: u32 = 1;
pub const SHADOW_CASCADE_COUNT: usize = 4;
pub const SHADOW_MAP_SIZE: u32 = 1024;
pub const SHADOW_MAX_DISTANCE: f32 = 220.0;
pub const SHADOW_SPLIT_LAMBDA: f32 = 0.65;
pub const SHADOW_CASTER_MARGIN: f32 = 80.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RendererConfig {
    canvas_width: u32,
    canvas_height: u32,
    max_texture_array_layers: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RendererConfigError {
    InvalidCanvasSize,
    InsufficientTextureArrayLayers,
}

impl RendererConfig {
    pub fn new(
        canvas_width: u32,
        canvas_height: u32,
        max_texture_array_layers: u32,
    ) -> Result<Self, RendererConfigError> {
        if canvas_width == 0 || canvas_height == 0 {
            return Err(RendererConfigError::InvalidCanvasSize);
        }

        if max_texture_array_layers < REQUIRED_TEXTURE_ARRAY_LAYERS {
            return Err(RendererConfigError::InsufficientTextureArrayLayers);
        }

        Ok(Self {
            canvas_width,
            canvas_height,
            max_texture_array_layers,
        })
    }

    pub fn canvas_width(&self) -> u32 {
        self.canvas_width
    }

    pub fn canvas_height(&self) -> u32 {
        self.canvas_height
    }

    pub fn max_texture_array_layers(&self) -> u32 {
        self.max_texture_array_layers
    }

    pub fn resize(
        &mut self,
        canvas_width: u32,
        canvas_height: u32,
    ) -> Result<(), RendererConfigError> {
        if canvas_width == 0 || canvas_height == 0 {
            return Err(RendererConfigError::InvalidCanvasSize);
        }

        self.canvas_width = canvas_width;
        self.canvas_height = canvas_height;
        Ok(())
    }
}
