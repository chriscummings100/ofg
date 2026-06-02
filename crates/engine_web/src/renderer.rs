use crate::config::{
    RendererConfig, RendererConfigError, TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
};
use crate::resources::{ResourceHandle, ResourceStore, ResourceStoreError};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MeshResource {
    vertex_float_count: u32,
    index_count: u32,
    floats_per_vertex: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextureResource {
    width: u32,
    height: u32,
    layers: u32,
    format: TextureFormat,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ObjectResource;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum TextureFormat {
    Rgba8Unorm,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RendererResourceCounts {
    pub meshes: usize,
    pub textures: usize,
    pub objects: usize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RendererStateError {
    NotConfigured,
    InvalidCanvasSize,
    InsufficientTextureArrayLayers,
    InvalidMesh,
    InvalidTexture,
    UnsupportedTextureFormat,
    StaleHandle,
}

pub struct RendererState {
    config: Option<RendererConfig>,
    meshes: ResourceStore<MeshResource>,
    textures: ResourceStore<TextureResource>,
    objects: ResourceStore<ObjectResource>,
    frame_index: u64,
    frame_draw_count: u32,
}

impl MeshResource {
    pub fn new(
        vertex_float_count: u32,
        index_count: u32,
        floats_per_vertex: u32,
    ) -> Result<Self, RendererStateError> {
        if floats_per_vertex != TERRAIN_VERTEX_FLOATS
            || vertex_float_count == 0
            || index_count == 0
            || vertex_float_count % floats_per_vertex != 0
            || index_count % 3 != 0
        {
            return Err(RendererStateError::InvalidMesh);
        }

        Ok(Self {
            vertex_float_count,
            index_count,
            floats_per_vertex,
        })
    }

    pub fn vertex_float_count(&self) -> u32 {
        self.vertex_float_count
    }

    pub fn index_count(&self) -> u32 {
        self.index_count
    }

    pub fn floats_per_vertex(&self) -> u32 {
        self.floats_per_vertex
    }
}

impl TextureResource {
    pub fn new(
        width: u32,
        height: u32,
        layers: u32,
        format_code: u32,
        max_layers: u32,
    ) -> Result<Self, RendererStateError> {
        if width == 0 || height == 0 || layers == 0 || layers > max_layers {
            return Err(RendererStateError::InvalidTexture);
        }

        let format = TextureFormat::from_code(format_code)?;

        Ok(Self {
            width,
            height,
            layers,
            format,
        })
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    pub fn layers(&self) -> u32 {
        self.layers
    }

    pub fn format_code(&self) -> u32 {
        self.format.code()
    }
}

impl TextureFormat {
    fn from_code(code: u32) -> Result<Self, RendererStateError> {
        match code {
            TEXTURE_FORMAT_RGBA8_UNORM => Ok(Self::Rgba8Unorm),
            _ => Err(RendererStateError::UnsupportedTextureFormat),
        }
    }

    fn code(self) -> u32 {
        match self {
            Self::Rgba8Unorm => TEXTURE_FORMAT_RGBA8_UNORM,
        }
    }
}

impl RendererState {
    pub fn new() -> Self {
        Self {
            config: None,
            meshes: ResourceStore::new(),
            textures: ResourceStore::new(),
            objects: ResourceStore::new(),
            frame_index: 0,
            frame_draw_count: 0,
        }
    }

    pub fn configure(
        &mut self,
        canvas_width: u32,
        canvas_height: u32,
        max_texture_array_layers: u32,
    ) -> Result<(), RendererStateError> {
        self.config = Some(RendererConfig::new(
            canvas_width,
            canvas_height,
            max_texture_array_layers,
        )?);
        Ok(())
    }

    pub fn is_configured(&self) -> bool {
        self.config.is_some()
    }

    pub fn config(&self) -> Option<RendererConfig> {
        self.config
    }

    pub fn resize(
        &mut self,
        canvas_width: u32,
        canvas_height: u32,
    ) -> Result<(), RendererStateError> {
        let config = self.config_mut()?;
        config.resize(canvas_width, canvas_height)?;
        Ok(())
    }

    pub fn register_mesh(
        &mut self,
        vertex_float_count: u32,
        index_count: u32,
        floats_per_vertex: u32,
    ) -> Result<ResourceHandle, RendererStateError> {
        self.require_config()?;
        Ok(self.meshes.insert(MeshResource::new(
            vertex_float_count,
            index_count,
            floats_per_vertex,
        )?))
    }

    pub fn unregister_mesh(
        &mut self,
        handle: ResourceHandle,
    ) -> Result<MeshResource, RendererStateError> {
        self.meshes.remove(handle).map_err(RendererStateError::from)
    }

    pub fn register_texture(
        &mut self,
        width: u32,
        height: u32,
        layers: u32,
        format_code: u32,
    ) -> Result<ResourceHandle, RendererStateError> {
        let max_layers = self.require_config()?.max_texture_array_layers();
        Ok(self.textures.insert(TextureResource::new(
            width,
            height,
            layers,
            format_code,
            max_layers,
        )?))
    }

    pub fn unregister_texture(
        &mut self,
        handle: ResourceHandle,
    ) -> Result<TextureResource, RendererStateError> {
        self.textures
            .remove(handle)
            .map_err(RendererStateError::from)
    }

    pub fn register_object(&mut self) -> Result<ResourceHandle, RendererStateError> {
        self.require_config()?;
        Ok(self.objects.insert(ObjectResource))
    }

    pub fn unregister_object(&mut self, handle: ResourceHandle) -> Result<(), RendererStateError> {
        self.objects
            .remove(handle)
            .map(|_| ())
            .map_err(RendererStateError::from)
    }

    pub fn begin_frame(
        &mut self,
        canvas_width: u32,
        canvas_height: u32,
    ) -> Result<(), RendererStateError> {
        self.resize(canvas_width, canvas_height)?;
        self.frame_index = self.frame_index.saturating_add(1);
        self.frame_draw_count = 0;
        Ok(())
    }

    pub fn note_draw(
        &mut self,
        mesh_handle: ResourceHandle,
        object_handle: ResourceHandle,
    ) -> Result<(), RendererStateError> {
        if !self.meshes.contains(mesh_handle) || !self.objects.contains(object_handle) {
            return Err(RendererStateError::StaleHandle);
        }

        self.frame_draw_count = self.frame_draw_count.saturating_add(1);
        Ok(())
    }

    pub fn resource_counts(&self) -> RendererResourceCounts {
        RendererResourceCounts {
            meshes: self.meshes.len(),
            textures: self.textures.len(),
            objects: self.objects.len(),
        }
    }

    pub fn frame_index(&self) -> u64 {
        self.frame_index
    }

    pub fn frame_draw_count(&self) -> u32 {
        self.frame_draw_count
    }

    fn require_config(&self) -> Result<RendererConfig, RendererStateError> {
        self.config.ok_or(RendererStateError::NotConfigured)
    }

    fn config_mut(&mut self) -> Result<&mut RendererConfig, RendererStateError> {
        self.config
            .as_mut()
            .ok_or(RendererStateError::NotConfigured)
    }
}

impl Default for RendererState {
    fn default() -> Self {
        Self::new()
    }
}

impl From<RendererConfigError> for RendererStateError {
    fn from(error: RendererConfigError) -> Self {
        match error {
            RendererConfigError::InvalidCanvasSize => Self::InvalidCanvasSize,
            RendererConfigError::InsufficientTextureArrayLayers => {
                Self::InsufficientTextureArrayLayers
            }
        }
    }
}

impl From<ResourceStoreError> for RendererStateError {
    fn from(_: ResourceStoreError) -> Self {
        Self::StaleHandle
    }
}
