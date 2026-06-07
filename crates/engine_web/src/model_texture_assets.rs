// Decodes imported glTF image records into RGBA pixels for renderer upload.
// GLTF parsing stays in Rust, and this module does not allocate WebGPU handles.

use std::fmt;

use crate::model_assets::ModelAsset;
use crate::model_materials::{ModelImageSource, ModelTexture};

#[derive(Clone, Debug, PartialEq)]
pub struct ModelRgbaImage {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelTextureAssetError {
    MissingTexture {
        texture_index: usize,
    },
    MissingImage {
        texture_index: usize,
        image_index: usize,
    },
    ExternalImageUnsupported {
        image_index: usize,
        uri: String,
    },
    DecodeImage {
        image_index: usize,
        message: String,
    },
}

impl fmt::Display for ModelTextureAssetError {
    /// Formats texture decode errors for browser diagnostics and tests.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingTexture { texture_index } => {
                write!(formatter, "glTF texture {texture_index} does not exist")
            }
            Self::MissingImage {
                texture_index,
                image_index,
            } => write!(
                formatter,
                "glTF texture {texture_index} references missing image {image_index}"
            ),
            Self::ExternalImageUnsupported { image_index, uri } => write!(
                formatter,
                "glTF image {image_index} uses external URI '{uri}', but runtime model textures must be embedded for now"
            ),
            Self::DecodeImage {
                image_index,
                message,
            } => write!(
                formatter,
                "glTF image {image_index} could not be decoded into RGBA pixels: {message}"
            ),
        }
    }
}

impl std::error::Error for ModelTextureAssetError {}

/// Decodes one imported glTF texture's image into 8-bit RGBA pixels.
pub fn decode_model_texture(
    model: &ModelAsset,
    texture_index: usize,
) -> Result<ModelRgbaImage, ModelTextureAssetError> {
    let texture = model
        .textures
        .get(texture_index)
        .ok_or(ModelTextureAssetError::MissingTexture { texture_index })?;
    decode_texture_image(model, texture_index, texture)
}

fn decode_texture_image(
    model: &ModelAsset,
    texture_index: usize,
    texture: &ModelTexture,
) -> Result<ModelRgbaImage, ModelTextureAssetError> {
    let image = model
        .images
        .get(texture.source)
        .ok_or(ModelTextureAssetError::MissingImage {
            texture_index,
            image_index: texture.source,
        })?;
    let encoded = match &image.source {
        ModelImageSource::BufferView { data, .. } | ModelImageSource::DataUri(data) => data,
        ModelImageSource::Uri(uri) => {
            return Err(ModelTextureAssetError::ExternalImageUnsupported {
                image_index: texture.source,
                uri: uri.clone(),
            });
        }
    };

    let decoded =
        image::load_from_memory(encoded).map_err(|error| ModelTextureAssetError::DecodeImage {
            image_index: texture.source,
            message: error.to_string(),
        })?;
    let rgba = decoded.to_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(ModelRgbaImage {
        width,
        height,
        data: rgba.into_raw(),
    })
}
