use crate::{
    decode_model_texture, import_gltf_model_from_slice, ModelAlphaMode, ModelImageSource,
    ModelMagFilter, ModelMaterialWorkflow, ModelMinFilter, ModelTextureWrap,
};

const MATERIAL_TEXTURES_GLTF: &[u8] = br#"{
  "asset": { "version": "2.0" },
  "images": [
    { "name": "Base", "uri": "textures/base.png", "mimeType": "image/png" },
    { "name": "Packed", "uri": "data:image/png;base64,AQIDBA==" }
  ],
  "samplers": [
    { "magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33648 }
  ],
  "textures": [
    { "name": "BaseTexture", "source": 0, "sampler": 0 },
    { "name": "PackedTexture", "source": 1 }
  ],
  "materials": [
    {
      "name": "Paint",
      "pbrMetallicRoughness": {
        "baseColorFactor": [0.8, 0.7, 0.6, 0.5],
        "baseColorTexture": { "index": 0, "texCoord": 1 },
        "metallicFactor": 0.25,
        "roughnessFactor": 0.75,
        "metallicRoughnessTexture": { "index": 1 }
      },
      "normalTexture": { "index": 0, "texCoord": 1, "scale": 0.5 },
      "occlusionTexture": { "index": 1, "strength": 0.25 },
      "emissiveTexture": { "index": 0 },
      "emissiveFactor": [0.1, 0.2, 0.3],
      "alphaMode": "MASK",
      "alphaCutoff": 0.33,
      "doubleSided": true
    }
  ]
}"#;

const BUFFER_VIEW_IMAGE_GLTF: &[u8] = br#"{
  "asset": { "version": "2.0" },
  "buffers": [
    { "uri": "data:application/octet-stream;base64,AQIDBAUG", "byteLength": 6 }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 1, "byteLength": 4 }
  ],
  "images": [
    { "name": "Embedded", "bufferView": 0, "mimeType": "image/png" }
  ]
}"#;

const DATA_URI_TEXTURE_GLTF: &[u8] = br#"{
  "asset": { "version": "2.0" },
  "images": [
    {
      "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="
    }
  ],
  "textures": [
    { "source": 0 }
  ]
}"#;

const SPECULAR_GLOSSINESS_FIXTURE_GLB: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/material-specular-glossiness-13.glb");

const SPECULAR_GLOSSINESS_GLTF: &[u8] = br#"{
  "asset": { "version": "2.0" },
  "extensionsUsed": ["KHR_materials_pbrSpecularGlossiness"],
  "images": [
    { "uri": "diffuse.png", "mimeType": "image/png" },
    { "uri": "spec-gloss.png", "mimeType": "image/png" }
  ],
  "textures": [
    { "source": 0 },
    { "source": 1 }
  ],
  "materials": [
    {
      "name": "SpecGloss",
      "extensions": {
        "KHR_materials_pbrSpecularGlossiness": {
          "diffuseFactor": [0.2, 0.3, 0.4, 0.5],
          "diffuseTexture": { "index": 0, "texCoord": 1 },
          "specularFactor": [0.9, 0.8, 0.7],
          "glossinessFactor": 0.6,
          "specularGlossinessTexture": { "index": 1 }
        }
      }
    }
  ]
}"#;

#[test]
fn gltf_importer_preserves_images_textures_samplers_and_metallic_roughness_materials() {
    let model = import_gltf_model_from_slice(MATERIAL_TEXTURES_GLTF).unwrap();

    assert_eq!(model.image_count(), 2);
    assert_eq!(
        model.images[0].source,
        ModelImageSource::Uri("textures/base.png".to_string())
    );
    assert_eq!(model.images[0].mime_type.as_deref(), Some("image/png"));
    assert_eq!(model.images[1].mime_type.as_deref(), Some("image/png"));
    assert_eq!(
        model.images[1].source,
        ModelImageSource::DataUri(vec![1, 2, 3, 4])
    );

    assert_eq!(model.texture_count(), 2);
    assert_eq!(model.textures[0].source, 0);
    assert_eq!(model.textures[0].sampler, Some(0));
    assert_eq!(model.textures[1].source, 1);
    assert_eq!(model.textures[1].sampler, None);

    assert_eq!(model.samplers.len(), 1);
    assert_eq!(model.samplers[0].mag_filter, Some(ModelMagFilter::Linear));
    assert_eq!(
        model.samplers[0].min_filter,
        Some(ModelMinFilter::LinearMipmapLinear)
    );
    assert_eq!(model.samplers[0].wrap_s, ModelTextureWrap::ClampToEdge);
    assert_eq!(model.samplers[0].wrap_t, ModelTextureWrap::MirroredRepeat);

    let material = &model.materials[0];
    assert_eq!(material.name.as_deref(), Some("Paint"));
    assert_eq!(material.alpha_mode, ModelAlphaMode::Mask);
    assert_close(material.alpha_cutoff, 0.33);
    assert!(material.double_sided);
    assert_eq!(
        material.normal_texture,
        Some(crate::ModelNormalTextureInfo {
            texture: 0,
            texcoord: 1,
            scale: 0.5,
        })
    );
    assert_eq!(
        material.occlusion_texture,
        Some(crate::ModelOcclusionTextureInfo {
            texture: 1,
            texcoord: 0,
            strength: 0.25,
        })
    );
    assert_eq!(
        material.emissive_texture,
        Some(crate::ModelTextureInfo {
            texture: 0,
            texcoord: 0,
        })
    );
    assert_eq!(material.emissive_factor, [0.1, 0.2, 0.3]);

    match &material.workflow {
        ModelMaterialWorkflow::MetallicRoughness {
            base_color_factor,
            base_color_texture,
            metallic_factor,
            roughness_factor,
            metallic_roughness_texture,
        } => {
            assert_eq!(*base_color_factor, [0.8, 0.7, 0.6, 0.5]);
            assert_eq!(
                *base_color_texture,
                Some(crate::ModelTextureInfo {
                    texture: 0,
                    texcoord: 1,
                })
            );
            assert_close(*metallic_factor, 0.25);
            assert_close(*roughness_factor, 0.75);
            assert_eq!(
                *metallic_roughness_texture,
                Some(crate::ModelTextureInfo {
                    texture: 1,
                    texcoord: 0,
                })
            );
        }
        workflow => panic!("expected metallic-roughness workflow, got {workflow:?}"),
    }
}

#[test]
fn gltf_importer_preserves_buffer_view_image_bytes() {
    let model = import_gltf_model_from_slice(BUFFER_VIEW_IMAGE_GLTF).unwrap();

    assert_eq!(model.image_count(), 1);
    assert_eq!(model.images[0].name.as_deref(), Some("Embedded"));
    assert_eq!(model.images[0].mime_type.as_deref(), Some("image/png"));
    assert_eq!(
        model.images[0].source,
        ModelImageSource::BufferView {
            buffer_view_index: 0,
            data: vec![2, 3, 4, 5],
        }
    );
}

#[test]
fn model_texture_decoder_converts_imported_png_to_rgba_pixels() {
    let model = import_gltf_model_from_slice(DATA_URI_TEXTURE_GLTF).unwrap();

    let image = decode_model_texture(&model, 0).unwrap();

    assert_eq!(image.width, 1);
    assert_eq!(image.height, 1);
    assert_eq!(image.data.len(), 4);
}

#[test]
fn gltf_importer_preserves_specular_glossiness_material_extension() {
    let model = import_gltf_model_from_slice(SPECULAR_GLOSSINESS_GLTF).unwrap();

    let material = &model.materials[0];
    assert_eq!(material.name.as_deref(), Some("SpecGloss"));
    match &material.workflow {
        ModelMaterialWorkflow::SpecularGlossiness {
            diffuse_factor,
            diffuse_texture,
            specular_factor,
            glossiness_factor,
            specular_glossiness_texture,
        } => {
            assert_eq!(*diffuse_factor, [0.2, 0.3, 0.4, 0.5]);
            assert_eq!(
                *diffuse_texture,
                Some(crate::ModelTextureInfo {
                    texture: 0,
                    texcoord: 1,
                })
            );
            assert_eq!(*specular_factor, [0.9, 0.8, 0.7]);
            assert_close(*glossiness_factor, 0.6);
            assert_eq!(
                *specular_glossiness_texture,
                Some(crate::ModelTextureInfo {
                    texture: 1,
                    texcoord: 0,
                })
            );
        }
        workflow => panic!("expected specular-glossiness workflow, got {workflow:?}"),
    }
}

#[test]
fn specular_glossiness_fixture_embeds_renderable_extension_textures() {
    let model = import_gltf_model_from_slice(SPECULAR_GLOSSINESS_FIXTURE_GLB).unwrap();

    assert_eq!(model.primitive_count(), 1);
    assert_eq!(model.image_count(), 3);
    assert_eq!(model.texture_count(), 3);
    let material = &model.materials[0];
    match &material.workflow {
        ModelMaterialWorkflow::SpecularGlossiness {
            diffuse_factor,
            diffuse_texture,
            specular_factor,
            glossiness_factor,
            specular_glossiness_texture,
        } => {
            assert_eq!(*diffuse_factor, [0.2, 0.2, 0.2, 0.8]);
            assert_eq!(diffuse_texture.unwrap().texture, 1);
            assert_eq!(*specular_factor, [0.4, 0.4, 0.4]);
            assert_close(*glossiness_factor, 0.3);
            assert_eq!(specular_glossiness_texture.unwrap().texture, 2);
        }
        workflow => panic!("expected specular-glossiness workflow, got {workflow:?}"),
    }

    let diffuse = decode_model_texture(&model, 1).unwrap();
    let specular_glossiness = decode_model_texture(&model, 2).unwrap();
    assert!(diffuse.width > 0);
    assert!(diffuse.height > 0);
    assert!(specular_glossiness.width > 0);
    assert!(specular_glossiness.height > 0);
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.0001,
        "expected {actual} to be close to {expected}"
    );
}
