// Renderer-neutral glTF material, texture, sampler, and image import records.
// This module keeps material model semantics near the GLTF importer while
// avoiding WebGPU handles or TypeScript/browser ownership.

use crate::model_assets::ModelAssetError;

#[derive(Clone, Debug, PartialEq)]
pub struct ModelImage {
    pub name: Option<String>,
    pub mime_type: Option<String>,
    pub source: ModelImageSource,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelImageSource {
    Uri(String),
    DataUri(Vec<u8>),
    BufferView {
        buffer_view_index: usize,
        data: Vec<u8>,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelTexture {
    pub name: Option<String>,
    pub source: usize,
    pub sampler: Option<usize>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelSampler {
    pub name: Option<String>,
    pub mag_filter: Option<ModelMagFilter>,
    pub min_filter: Option<ModelMinFilter>,
    pub wrap_s: ModelTextureWrap,
    pub wrap_t: ModelTextureWrap,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelMagFilter {
    Nearest,
    Linear,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelMinFilter {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelTextureWrap {
    ClampToEdge,
    MirroredRepeat,
    Repeat,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelTextureInfo {
    pub texture: usize,
    pub texcoord: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelNormalTextureInfo {
    pub texture: usize,
    pub texcoord: u32,
    pub scale: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelOcclusionTextureInfo {
    pub texture: usize,
    pub texcoord: u32,
    pub strength: f32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelAlphaMode {
    Opaque,
    Mask,
    Blend,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelMaterialWorkflow {
    MetallicRoughness {
        base_color_factor: [f32; 4],
        base_color_texture: Option<ModelTextureInfo>,
        metallic_factor: f32,
        roughness_factor: f32,
        metallic_roughness_texture: Option<ModelTextureInfo>,
    },
    SpecularGlossiness {
        diffuse_factor: [f32; 4],
        diffuse_texture: Option<ModelTextureInfo>,
        specular_factor: [f32; 3],
        glossiness_factor: f32,
        specular_glossiness_texture: Option<ModelTextureInfo>,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelMaterial {
    pub name: Option<String>,
    pub workflow: ModelMaterialWorkflow,
    pub base_color_factor: [f32; 4],
    pub metallic_factor: f32,
    pub roughness_factor: f32,
    pub normal_texture: Option<ModelNormalTextureInfo>,
    pub occlusion_texture: Option<ModelOcclusionTextureInfo>,
    pub emissive_texture: Option<ModelTextureInfo>,
    pub emissive_factor: [f32; 3],
    pub alpha_mode: ModelAlphaMode,
    pub alpha_cutoff: f32,
    pub double_sided: bool,
}

/// Converts glTF image records into renderer-neutral encoded image sources.
pub fn import_model_images(
    document: &gltf::Document,
    buffers: &[Vec<u8>],
) -> Result<Vec<ModelImage>, ModelAssetError> {
    document
        .images()
        .map(|image| {
            let image_index = image.index();
            let name = image.name().map(str::to_owned);
            match image.source() {
                gltf::image::Source::View { view, mime_type } => {
                    let buffer_index = view.buffer().index();
                    let offset = view.offset();
                    let expected_end = offset + view.length();
                    let buffer = buffers.get(buffer_index).ok_or_else(|| {
                        ModelAssetError::InvalidImageBufferView {
                            image_index,
                            buffer_view_index: view.index(),
                            buffer_index,
                            actual: 0,
                            expected_end,
                        }
                    })?;
                    if expected_end > buffer.len() {
                        return Err(ModelAssetError::InvalidImageBufferView {
                            image_index,
                            buffer_view_index: view.index(),
                            buffer_index,
                            actual: buffer.len(),
                            expected_end,
                        });
                    }

                    Ok(ModelImage {
                        name,
                        mime_type: Some(mime_type.to_string()),
                        source: ModelImageSource::BufferView {
                            buffer_view_index: view.index(),
                            data: buffer[offset..expected_end].to_vec(),
                        },
                    })
                }
                gltf::image::Source::Uri { uri, mime_type } if uri.starts_with("data:") => {
                    let data = decode_image_data_uri(uri).map_err(|error| match error {
                        ImageDataUriError::Unsupported => {
                            ModelAssetError::UnsupportedImageDataUri {
                                image_index,
                                uri: uri.to_string(),
                            }
                        }
                        ImageDataUriError::Decode(message) => ModelAssetError::ImageDataUriDecode {
                            image_index,
                            message,
                        },
                    })?;

                    Ok(ModelImage {
                        name,
                        mime_type: mime_type
                            .map(str::to_owned)
                            .or_else(|| data_uri_mime_type(uri)),
                        source: ModelImageSource::DataUri(data),
                    })
                }
                gltf::image::Source::Uri { uri, mime_type } => Ok(ModelImage {
                    name,
                    mime_type: mime_type.map(str::to_owned),
                    source: ModelImageSource::Uri(uri.to_string()),
                }),
            }
        })
        .collect()
}

/// Converts glTF texture records into image/sampler index references.
pub fn import_model_textures(document: &gltf::Document) -> Vec<ModelTexture> {
    document
        .textures()
        .map(|texture| ModelTexture {
            name: texture.name().map(str::to_owned),
            source: texture.source().index(),
            sampler: texture.sampler().index(),
        })
        .collect()
}

/// Converts glTF sampler settings into engine-owned enums.
pub fn import_model_samplers(document: &gltf::Document) -> Vec<ModelSampler> {
    document
        .samplers()
        .map(|sampler| ModelSampler {
            name: sampler.name().map(str::to_owned),
            mag_filter: sampler.mag_filter().map(model_mag_filter),
            min_filter: sampler.min_filter().map(model_min_filter),
            wrap_s: model_texture_wrap(sampler.wrap_s()),
            wrap_t: model_texture_wrap(sampler.wrap_t()),
        })
        .collect()
}

/// Converts glTF material workflows into renderer-neutral material records.
pub fn import_model_materials(document: &gltf::Document) -> Vec<ModelMaterial> {
    document
        .materials()
        .map(|material| {
            let pbr = material.pbr_metallic_roughness();
            let base_color_factor = pbr.base_color_factor();
            let metallic_factor = pbr.metallic_factor();
            let roughness_factor = pbr.roughness_factor();
            let workflow = if let Some(specular_glossiness) = material.pbr_specular_glossiness() {
                ModelMaterialWorkflow::SpecularGlossiness {
                    diffuse_factor: specular_glossiness.diffuse_factor(),
                    diffuse_texture: specular_glossiness
                        .diffuse_texture()
                        .map(model_texture_info),
                    specular_factor: specular_glossiness.specular_factor(),
                    glossiness_factor: specular_glossiness.glossiness_factor(),
                    specular_glossiness_texture: specular_glossiness
                        .specular_glossiness_texture()
                        .map(model_texture_info),
                }
            } else {
                ModelMaterialWorkflow::MetallicRoughness {
                    base_color_factor,
                    base_color_texture: pbr.base_color_texture().map(model_texture_info),
                    metallic_factor,
                    roughness_factor,
                    metallic_roughness_texture: pbr
                        .metallic_roughness_texture()
                        .map(model_texture_info),
                }
            };

            ModelMaterial {
                name: material.name().map(str::to_owned),
                workflow,
                base_color_factor,
                metallic_factor,
                roughness_factor,
                normal_texture: material.normal_texture().map(model_normal_texture_info),
                occlusion_texture: material
                    .occlusion_texture()
                    .map(model_occlusion_texture_info),
                emissive_texture: material.emissive_texture().map(model_texture_info),
                emissive_factor: material.emissive_factor(),
                alpha_mode: model_alpha_mode(material.alpha_mode()),
                alpha_cutoff: material.alpha_cutoff().unwrap_or(0.5),
                double_sided: material.double_sided(),
            }
        })
        .collect()
}

enum ImageDataUriError {
    Unsupported,
    Decode(String),
}

fn decode_image_data_uri(uri: &str) -> Result<Vec<u8>, ImageDataUriError> {
    let Some((metadata, payload)) = uri.split_once(',') else {
        return Err(ImageDataUriError::Unsupported);
    };
    if !metadata.starts_with("data:") || !metadata.contains(";base64") {
        return Err(ImageDataUriError::Unsupported);
    }

    base64::decode(payload).map_err(|error| ImageDataUriError::Decode(error.to_string()))
}

fn data_uri_mime_type(uri: &str) -> Option<String> {
    let (metadata, _) = uri.split_once(',')?;
    let mime_type = metadata
        .strip_prefix("data:")?
        .split(';')
        .next()
        .unwrap_or_default();
    if mime_type.is_empty() {
        None
    } else {
        Some(mime_type.to_string())
    }
}

fn model_mag_filter(filter: gltf::texture::MagFilter) -> ModelMagFilter {
    match filter {
        gltf::texture::MagFilter::Nearest => ModelMagFilter::Nearest,
        gltf::texture::MagFilter::Linear => ModelMagFilter::Linear,
    }
}

fn model_min_filter(filter: gltf::texture::MinFilter) -> ModelMinFilter {
    match filter {
        gltf::texture::MinFilter::Nearest => ModelMinFilter::Nearest,
        gltf::texture::MinFilter::Linear => ModelMinFilter::Linear,
        gltf::texture::MinFilter::NearestMipmapNearest => ModelMinFilter::NearestMipmapNearest,
        gltf::texture::MinFilter::LinearMipmapNearest => ModelMinFilter::LinearMipmapNearest,
        gltf::texture::MinFilter::NearestMipmapLinear => ModelMinFilter::NearestMipmapLinear,
        gltf::texture::MinFilter::LinearMipmapLinear => ModelMinFilter::LinearMipmapLinear,
    }
}

fn model_texture_wrap(wrap: gltf::texture::WrappingMode) -> ModelTextureWrap {
    match wrap {
        gltf::texture::WrappingMode::ClampToEdge => ModelTextureWrap::ClampToEdge,
        gltf::texture::WrappingMode::MirroredRepeat => ModelTextureWrap::MirroredRepeat,
        gltf::texture::WrappingMode::Repeat => ModelTextureWrap::Repeat,
    }
}

fn model_texture_info(info: gltf::texture::Info<'_>) -> ModelTextureInfo {
    ModelTextureInfo {
        texture: info.texture().index(),
        texcoord: info.tex_coord(),
    }
}

fn model_normal_texture_info(info: gltf::material::NormalTexture<'_>) -> ModelNormalTextureInfo {
    ModelNormalTextureInfo {
        texture: info.texture().index(),
        texcoord: info.tex_coord(),
        scale: info.scale(),
    }
}

fn model_occlusion_texture_info(
    info: gltf::material::OcclusionTexture<'_>,
) -> ModelOcclusionTextureInfo {
    ModelOcclusionTextureInfo {
        texture: info.texture().index(),
        texcoord: info.tex_coord(),
        strength: info.strength(),
    }
}

fn model_alpha_mode(alpha_mode: gltf::material::AlphaMode) -> ModelAlphaMode {
    match alpha_mode {
        gltf::material::AlphaMode::Opaque => ModelAlphaMode::Opaque,
        gltf::material::AlphaMode::Mask => ModelAlphaMode::Mask,
        gltf::material::AlphaMode::Blend => ModelAlphaMode::Blend,
    }
}
