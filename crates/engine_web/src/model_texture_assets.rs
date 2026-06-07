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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model_assets::ModelAsset;
    use crate::model_materials::{ModelImage, ModelImageSource, ModelTexture};
    use image::codecs::png::PngEncoder;
    use image::{ColorType, ImageEncoder};

    #[test]
    fn decodes_embedded_png_texture_pixels() {
        let model = model_with_texture(ModelImageSource::DataUri(encoded_png_rgba_1x1()), 0);

        let image = decode_model_texture(&model, 0).expect("embedded PNG should decode");

        assert_eq!(image.width, 1);
        assert_eq!(image.height, 1);
        assert_eq!(image.data, vec![10, 20, 30, 255]);
    }

    #[test]
    fn rejects_missing_texture_and_missing_image_references() {
        let empty = empty_model_asset();
        assert_eq!(
            decode_model_texture(&empty, 3),
            Err(ModelTextureAssetError::MissingTexture { texture_index: 3 })
        );

        let missing_image = model_with_texture(ModelImageSource::DataUri(vec![]), 4);
        assert_eq!(
            decode_model_texture(&missing_image, 0),
            Err(ModelTextureAssetError::MissingImage {
                texture_index: 0,
                image_index: 4,
            })
        );
    }

    #[test]
    fn rejects_external_and_invalid_embedded_images() {
        let external = model_with_texture(ModelImageSource::Uri("textures/albedo.png".into()), 0);
        assert_eq!(
            decode_model_texture(&external, 0),
            Err(ModelTextureAssetError::ExternalImageUnsupported {
                image_index: 0,
                uri: "textures/albedo.png".into(),
            })
        );

        let invalid = model_with_texture(
            ModelImageSource::BufferView {
                buffer_view_index: 0,
                data: vec![1, 2, 3],
            },
            0,
        );
        let error = decode_model_texture(&invalid, 0).expect_err("invalid image should fail");
        match error {
            ModelTextureAssetError::DecodeImage {
                image_index,
                message,
            } => {
                assert_eq!(image_index, 0);
                assert!(!message.is_empty());
            }
            other => panic!("expected decode error, got {other:?}"),
        }
    }

    #[test]
    fn formats_texture_asset_errors_for_browser_diagnostics() {
        assert_eq!(
            ModelTextureAssetError::MissingTexture { texture_index: 7 }.to_string(),
            "glTF texture 7 does not exist"
        );
        assert_eq!(
            ModelTextureAssetError::MissingImage {
                texture_index: 1,
                image_index: 2,
            }
            .to_string(),
            "glTF texture 1 references missing image 2"
        );
        assert_eq!(
            ModelTextureAssetError::ExternalImageUnsupported {
                image_index: 3,
                uri: "a.png".into(),
            }
            .to_string(),
            "glTF image 3 uses external URI 'a.png', but runtime model textures must be embedded for now"
        );
        assert_eq!(
            ModelTextureAssetError::DecodeImage {
                image_index: 4,
                message: "bad png".into(),
            }
            .to_string(),
            "glTF image 4 could not be decoded into RGBA pixels: bad png"
        );
    }

    fn model_with_texture(source: ModelImageSource, texture_source: usize) -> ModelAsset {
        let mut model = empty_model_asset();
        model.images.push(ModelImage {
            name: Some("test image".into()),
            mime_type: Some("image/png".into()),
            source,
        });
        model.textures.push(ModelTexture {
            name: Some("test texture".into()),
            source: texture_source,
            sampler: None,
        });
        model
    }

    fn empty_model_asset() -> ModelAsset {
        ModelAsset {
            nodes: vec![],
            primitives: vec![],
            images: vec![],
            textures: vec![],
            samplers: vec![],
            materials: vec![],
            animations: vec![],
            skins: vec![],
        }
    }

    fn encoded_png_rgba_1x1() -> Vec<u8> {
        let mut encoded = Vec::new();
        PngEncoder::new(&mut encoded)
            .write_image(&[10, 20, 30, 255], 1, 1, ColorType::Rgba8.into())
            .expect("test PNG should encode");
        encoded
    }
}
