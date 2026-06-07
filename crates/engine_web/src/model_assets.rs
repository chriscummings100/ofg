// Rust-owned glTF/GLB model asset import for browser-fetched model bytes.
// This module converts a narrow, tested subset of glTF into engine-friendly
// mesh, material, and node data without introducing TypeScript model semantics.

use std::fmt;

use crate::config::MODEL_VERTEX_FLOATS;
use crate::model_animation::{import_animations, ModelAnimationClip};
use crate::model_materials::{
    import_model_images, import_model_materials, import_model_samplers, import_model_textures,
    ModelImage, ModelMaterial, ModelSampler, ModelTexture,
};

pub const SAMPLE_ANIMATED_BOX_MODEL_ID: &str = "model.test-fixtures.animated-box";
pub const SAMPLE_ANIMATED_BOX_MODEL_URL: &str = "/assets/models/test-fixtures/box-animated.glb";
pub const SAMPLE_ANIMATED_BOX_MESH_LABEL: &str = "model.test-fixtures.animated-box.primitive0.mesh";
pub const SAMPLE_ANIMATED_BOX_MATERIAL_LABEL: &str =
    "model.test-fixtures.animated-box.primitive0.material";
pub const SAMPLE_RIGGED_SIMPLE_MODEL_ID: &str = "model.test-fixtures.rigged-simple";
pub const SAMPLE_RIGGED_SIMPLE_MODEL_URL: &str = "/assets/models/test-fixtures/rigged-simple.glb";
pub const SAMPLE_RIGGED_SIMPLE_MESH_LABEL: &str =
    "model.test-fixtures.rigged-simple.primitive0.mesh";
pub const SAMPLE_RIGGED_SIMPLE_MATERIAL_LABEL: &str =
    "model.test-fixtures.rigged-simple.primitive0.material";
pub const SAMPLE_STATIC_BOX_MODEL_ID: &str = "model.test-fixtures.static-box";
pub const SAMPLE_STATIC_BOX_MODEL_URL: &str = "/assets/models/test-fixtures/static-box.glb";
pub const SAMPLE_STATIC_BOX_MESH_LABEL: &str = "model.test-fixtures.static-box.primitive0.mesh";
pub const SAMPLE_STATIC_BOX_MATERIAL_LABEL: &str =
    "model.test-fixtures.static-box.primitive0.material";
pub const SAMPLE_SPECULAR_GLOSSINESS_MODEL_ID: &str = "model.test-fixtures.specular-glossiness";
pub const SAMPLE_SPECULAR_GLOSSINESS_MODEL_URL: &str =
    "/assets/models/test-fixtures/material-specular-glossiness-13.glb";
pub const SAMPLE_SPECULAR_GLOSSINESS_MESH_LABEL: &str =
    "model.test-fixtures.specular-glossiness.primitive0.mesh";
pub const SAMPLE_SPECULAR_GLOSSINESS_MATERIAL_LABEL: &str =
    "model.test-fixtures.specular-glossiness.primitive0.material";
pub const PLAYER_QUATERNIUS_UAL2_MODEL_ID: &str = "model.player.quaternius-ual2";
pub const PLAYER_QUATERNIUS_UAL2_MODEL_URL: &str =
    "/assets/models/player/quaternius-ual2-standard.glb";
pub const PLAYER_QUATERNIUS_UAL2_MESH_LABEL: &str = "model.player.quaternius-ual2.primitive0.mesh";
pub const PLAYER_QUATERNIUS_UAL2_MATERIAL_LABEL: &str =
    "model.player.quaternius-ual2.primitive0.material";
pub const PLAYER_QUATERNIUS_UAL1_MODEL_ID: &str = "model.player.quaternius-ual1";
pub const PLAYER_QUATERNIUS_UAL1_MODEL_URL: &str =
    "/assets/models/player/quaternius-ual1-standard.glb";
pub const PLAYER_SUPERHERO_MALE_MODEL_ID: &str = "model.player.superhero-male";
pub const PLAYER_SUPERHERO_MALE_MODEL_URL: &str =
    "/assets/models/player/quaternius-superhero-male.glb";
pub const PLAYER_SUPERHERO_MALE_MESH_LABEL: &str = "model.player.superhero-male.body.mesh";
pub const PLAYER_SUPERHERO_MALE_MATERIAL_LABEL: &str = "model.player.superhero-male.body.material";
pub const PLAYER_SUPERHERO_FEMALE_MODEL_ID: &str = "model.player.superhero-female";
pub const PLAYER_SUPERHERO_FEMALE_MODEL_URL: &str =
    "/assets/models/player/quaternius-superhero-female.glb";
pub const PLAYER_SUPERHERO_FEMALE_MESH_LABEL: &str = "model.player.superhero-female.body.mesh";
pub const PLAYER_SUPERHERO_FEMALE_MATERIAL_LABEL: &str =
    "model.player.superhero-female.body.material";

const IDENTITY_MATRIX: [f32; 16] = [
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
];

#[derive(Clone, Debug, PartialEq)]
pub struct ModelAsset {
    pub nodes: Vec<ModelNode>,
    pub primitives: Vec<ModelPrimitive>,
    pub images: Vec<ModelImage>,
    pub textures: Vec<ModelTexture>,
    pub samplers: Vec<ModelSampler>,
    pub materials: Vec<ModelMaterial>,
    pub animations: Vec<ModelAnimationClip>,
    pub skins: Vec<ModelSkin>,
}

impl ModelAsset {
    /// Returns the number of imported draw primitives.
    pub fn primitive_count(&self) -> usize {
        self.primitives.len()
    }

    /// Returns the total number of imported vertices across all primitives.
    pub fn vertex_count(&self) -> usize {
        self.primitives
            .iter()
            .map(|primitive| primitive.vertices.len())
            .sum()
    }

    /// Returns the total number of imported indices across all primitives.
    pub fn index_count(&self) -> usize {
        self.primitives
            .iter()
            .map(|primitive| primitive.indices.len())
            .sum()
    }

    /// Returns the number of imported animation clips.
    pub fn animation_count(&self) -> usize {
        self.animations.len()
    }

    /// Returns the number of imported skin bindings.
    pub fn skin_count(&self) -> usize {
        self.skins.len()
    }

    /// Returns the number of imported image records.
    pub fn image_count(&self) -> usize {
        self.images.len()
    }

    /// Returns the number of imported texture records.
    pub fn texture_count(&self) -> usize {
        self.textures.len()
    }

    /// Returns the imported material count.
    pub fn material_count(&self) -> usize {
        self.materials.len()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelNode {
    pub name: Option<String>,
    pub parent: Option<usize>,
    pub children: Vec<usize>,
    pub mesh: Option<usize>,
    pub skin: Option<usize>,
    pub local_transform: ModelNodeTransform,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelNodeTransform {
    pub translation: [f32; 3],
    pub rotation: [f32; 4],
    pub scale: [f32; 3],
}

impl Default for ModelNodeTransform {
    fn default() -> Self {
        Self {
            translation: [0.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0, 1.0],
            scale: [1.0, 1.0, 1.0],
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelPrimitive {
    pub mesh_index: usize,
    pub mesh_name: Option<String>,
    pub material: Option<usize>,
    pub vertices: Vec<ModelVertex>,
    pub indices: Vec<u32>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub texcoord0: [f32; 2],
    pub color0: [f32; 4],
    pub joints0: [u16; 4],
    pub weights0: [f32; 4],
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelSkin {
    pub name: Option<String>,
    pub joints: Vec<usize>,
    pub inverse_bind_matrices: Vec<[f32; 16]>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelAssetError {
    GltfParse(String),
    MissingBinaryBuffer {
        buffer_index: usize,
    },
    InvalidBufferLength {
        buffer_index: usize,
        actual: usize,
        expected: usize,
    },
    UnsupportedDataUri {
        buffer_index: usize,
        uri: String,
    },
    DataUriDecode {
        buffer_index: usize,
        message: String,
    },
    UnsupportedImageDataUri {
        image_index: usize,
        uri: String,
    },
    ImageDataUriDecode {
        image_index: usize,
        message: String,
    },
    InvalidImageBufferView {
        image_index: usize,
        buffer_view_index: usize,
        buffer_index: usize,
        actual: usize,
        expected_end: usize,
    },
    UnsupportedExternalBuffer {
        buffer_index: usize,
        uri: String,
    },
    UnsupportedPrimitiveMode {
        mesh_index: usize,
        mode: String,
    },
    MissingPositions {
        mesh_index: usize,
    },
    InvalidAttributeLength {
        mesh_index: usize,
        attribute: &'static str,
        actual: usize,
        expected: usize,
    },
    InvalidTriangleIndexCount {
        mesh_index: usize,
        index_count: usize,
    },
    InvalidFloatData {
        mesh_index: usize,
        attribute: &'static str,
    },
    MissingAnimationInput {
        animation_index: usize,
        channel_index: usize,
    },
    MissingAnimationOutput {
        animation_index: usize,
        channel_index: usize,
    },
    UnsupportedAnimationInterpolation {
        animation_index: usize,
        channel_index: usize,
        interpolation: String,
    },
    UnsupportedAnimationTarget {
        animation_index: usize,
        channel_index: usize,
        target: String,
    },
    InvalidAnimationKeyframes {
        animation_index: usize,
        channel_index: usize,
        input_count: usize,
        output_count: usize,
    },
    InvalidAnimationData {
        animation_index: usize,
        channel_index: usize,
        attribute: &'static str,
    },
    InvalidAnimationTargetNode {
        node_index: usize,
    },
    InvalidAnimationBlendTransformCount {
        from_count: usize,
        to_count: usize,
    },
    InvalidAnimationTime,
    InvalidSkinIndex {
        skin_index: usize,
    },
    InvalidSkinJoint {
        skin_index: usize,
        joint_index: usize,
        node_index: usize,
    },
    InvalidSkinInverseBindCount {
        skin_index: usize,
        joint_count: usize,
        inverse_bind_count: usize,
    },
    InvalidSkinData {
        skin_index: usize,
        attribute: &'static str,
    },
    InvalidSkinVertexJoint {
        mesh_index: usize,
        vertex_index: usize,
        joint_index: usize,
    },
    InvalidSkinNodeTransformCount {
        node_count: usize,
        transform_count: usize,
    },
}

impl fmt::Display for ModelAssetError {
    /// Formats model import errors for tests, debug snapshots, and browser logs.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::GltfParse(message) => write!(formatter, "Failed to parse glTF asset: {message}"),
            Self::MissingBinaryBuffer { buffer_index } => write!(
                formatter,
                "glTF buffer {buffer_index} references a GLB binary chunk, but no binary chunk exists"
            ),
            Self::InvalidBufferLength {
                buffer_index,
                actual,
                expected,
            } => write!(
                formatter,
                "glTF buffer {buffer_index} has {actual} bytes; expected at least {expected}"
            ),
            Self::UnsupportedDataUri { buffer_index, uri } => write!(
                formatter,
                "glTF buffer {buffer_index} uses unsupported data URI '{uri}'"
            ),
            Self::DataUriDecode {
                buffer_index,
                message,
            } => write!(
                formatter,
                "glTF buffer {buffer_index} data URI could not be decoded: {message}"
            ),
            Self::UnsupportedImageDataUri { image_index, uri } => write!(
                formatter,
                "glTF image {image_index} uses unsupported data URI '{uri}'"
            ),
            Self::ImageDataUriDecode {
                image_index,
                message,
            } => write!(
                formatter,
                "glTF image {image_index} data URI could not be decoded: {message}"
            ),
            Self::InvalidImageBufferView {
                image_index,
                buffer_view_index,
                buffer_index,
                actual,
                expected_end,
            } => write!(
                formatter,
                "glTF image {image_index} bufferView {buffer_view_index} reads through byte {expected_end} of buffer {buffer_index}, but the buffer has {actual} bytes"
            ),
            Self::UnsupportedExternalBuffer { buffer_index, uri } => write!(
                formatter,
                "glTF buffer {buffer_index} uses external URI '{uri}', but only GLB or data URI buffers are supported"
            ),
            Self::UnsupportedPrimitiveMode { mesh_index, mode } => write!(
                formatter,
                "glTF mesh {mesh_index} uses primitive mode {mode}; only triangles are supported"
            ),
            Self::MissingPositions { mesh_index } => {
                write!(formatter, "glTF mesh {mesh_index} is missing POSITION data")
            }
            Self::InvalidAttributeLength {
                mesh_index,
                attribute,
                actual,
                expected,
            } => write!(
                formatter,
                "glTF mesh {mesh_index} attribute {attribute} has {actual} values; expected {expected}"
            ),
            Self::InvalidTriangleIndexCount {
                mesh_index,
                index_count,
            } => write!(
                formatter,
                "glTF mesh {mesh_index} has {index_count} triangle indices; expected a multiple of 3"
            ),
            Self::InvalidFloatData {
                mesh_index,
                attribute,
            } => write!(
                formatter,
                "glTF mesh {mesh_index} attribute {attribute} contains non-finite data"
            ),
            Self::MissingAnimationInput {
                animation_index,
                channel_index,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} is missing input keyframe times"
            ),
            Self::MissingAnimationOutput {
                animation_index,
                channel_index,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} is missing output values"
            ),
            Self::UnsupportedAnimationInterpolation {
                animation_index,
                channel_index,
                interpolation,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} uses unsupported interpolation {interpolation}"
            ),
            Self::UnsupportedAnimationTarget {
                animation_index,
                channel_index,
                target,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} targets unsupported property {target}"
            ),
            Self::InvalidAnimationKeyframes {
                animation_index,
                channel_index,
                input_count,
                output_count,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} has {input_count} input times and {output_count} output values"
            ),
            Self::InvalidAnimationData {
                animation_index,
                channel_index,
                attribute,
            } => write!(
                formatter,
                "glTF animation {animation_index} channel {channel_index} contains invalid {attribute} data"
            ),
            Self::InvalidAnimationTargetNode { node_index } => write!(
                formatter,
                "glTF animation targets missing model node {node_index}"
            ),
            Self::InvalidAnimationBlendTransformCount {
                from_count,
                to_count,
            } => write!(
                formatter,
                "glTF animation blend received {from_count} source transforms and {to_count} target transforms"
            ),
            Self::InvalidAnimationTime => {
                write!(formatter, "glTF animation sampling time was not finite")
            }
            Self::InvalidSkinIndex { skin_index } => {
                write!(formatter, "glTF skin {skin_index} does not exist")
            }
            Self::InvalidSkinJoint {
                skin_index,
                joint_index,
                node_index,
            } => write!(
                formatter,
                "glTF skin {skin_index} joint {joint_index} references missing node {node_index}"
            ),
            Self::InvalidSkinInverseBindCount {
                skin_index,
                joint_count,
                inverse_bind_count,
            } => write!(
                formatter,
                "glTF skin {skin_index} has {joint_count} joints and {inverse_bind_count} inverse bind matrices"
            ),
            Self::InvalidSkinData {
                skin_index,
                attribute,
            } => write!(
                formatter,
                "glTF skin {skin_index} contains invalid {attribute} data"
            ),
            Self::InvalidSkinVertexJoint {
                mesh_index,
                vertex_index,
                joint_index,
            } => write!(
                formatter,
                "glTF mesh {mesh_index} vertex {vertex_index} references missing skin joint {joint_index}"
            ),
            Self::InvalidSkinNodeTransformCount {
                node_count,
                transform_count,
            } => write!(
                formatter,
                "glTF skinning received {transform_count} node transforms for {node_count} model nodes"
            ),
        }
    }
}

impl std::error::Error for ModelAssetError {}

/// Imports a glTF or GLB byte slice into engine-owned model asset data.
pub fn import_gltf_model_from_slice(bytes: &[u8]) -> Result<ModelAsset, ModelAssetError> {
    let parsed = gltf::Gltf::from_slice(bytes)
        .map_err(|error| ModelAssetError::GltfParse(error.to_string()))?;
    let buffers = import_buffers(&parsed.document, parsed.blob.as_deref())?;
    let document = parsed.document;

    let nodes = import_nodes(&document);
    let samplers = import_model_samplers(&document);
    let images = import_model_images(&document, &buffers)?;
    let textures = import_model_textures(&document);
    let materials = import_model_materials(&document);
    let primitives = import_primitives(&document, &buffers)?;
    let animations = import_animations(&document, &buffers)?;
    let skins = import_skins(&document, &buffers)?;

    Ok(ModelAsset {
        nodes,
        primitives,
        images,
        textures,
        samplers,
        materials,
        animations,
        skins,
    })
}

/// Packs one imported primitive into the static model renderer vertex layout.
pub fn model_primitive_vertex_floats(primitive: &ModelPrimitive) -> Vec<f32> {
    let mut values = Vec::with_capacity(primitive.vertices.len() * MODEL_VERTEX_FLOATS as usize);
    for vertex in &primitive.vertices {
        values.extend_from_slice(&vertex.position);
        values.extend_from_slice(&vertex.normal);
        values.extend_from_slice(&vertex.texcoord0);
        values.extend_from_slice(&vertex.color0);
    }

    values
}

/// Resolves GLB binary chunks and base64 data URI buffers into owned byte arrays.
fn import_buffers(
    document: &gltf::Document,
    binary_blob: Option<&[u8]>,
) -> Result<Vec<Vec<u8>>, ModelAssetError> {
    document
        .buffers()
        .map(|buffer| {
            let mut data = match buffer.source() {
                gltf::buffer::Source::Bin => binary_blob
                    .ok_or(ModelAssetError::MissingBinaryBuffer {
                        buffer_index: buffer.index(),
                    })?
                    .to_vec(),
                gltf::buffer::Source::Uri(uri) if uri.starts_with("data:") => {
                    decode_data_uri_buffer(buffer.index(), uri)?
                }
                gltf::buffer::Source::Uri(uri) => {
                    return Err(ModelAssetError::UnsupportedExternalBuffer {
                        buffer_index: buffer.index(),
                        uri: uri.to_string(),
                    });
                }
            };

            let expected = buffer.length();
            if data.len() < expected {
                return Err(ModelAssetError::InvalidBufferLength {
                    buffer_index: buffer.index(),
                    actual: data.len(),
                    expected,
                });
            }
            data.truncate(expected);

            Ok(data)
        })
        .collect()
}

/// Decodes the base64 payload from an embedded glTF data URI buffer.
fn decode_data_uri_buffer(buffer_index: usize, uri: &str) -> Result<Vec<u8>, ModelAssetError> {
    decode_data_uri_payload(uri).map_err(|error| match error {
        DataUriPayloadError::Unsupported => ModelAssetError::UnsupportedDataUri {
            buffer_index,
            uri: uri.to_string(),
        },
        DataUriPayloadError::Decode(message) => ModelAssetError::DataUriDecode {
            buffer_index,
            message,
        },
    })
}

#[derive(Debug)]
enum DataUriPayloadError {
    Unsupported,
    Decode(String),
}

/// Decodes a base64 data URI payload into encoded asset bytes.
fn decode_data_uri_payload(uri: &str) -> Result<Vec<u8>, DataUriPayloadError> {
    let Some((metadata, payload)) = uri.split_once(',') else {
        return Err(DataUriPayloadError::Unsupported);
    };
    if !metadata.starts_with("data:") || !metadata.contains(";base64") {
        return Err(DataUriPayloadError::Unsupported);
    }

    base64::decode(payload).map_err(|error| DataUriPayloadError::Decode(error.to_string()))
}

/// Converts glTF nodes into parent-aware local transform records.
fn import_nodes(document: &gltf::Document) -> Vec<ModelNode> {
    let mut nodes: Vec<ModelNode> = document
        .nodes()
        .map(|node| {
            let (translation, rotation, scale) = node.transform().decomposed();

            ModelNode {
                name: node.name().map(str::to_owned),
                parent: None,
                children: node.children().map(|child| child.index()).collect(),
                mesh: node.mesh().map(|mesh| mesh.index()),
                skin: node.skin().map(|skin| skin.index()),
                local_transform: ModelNodeTransform {
                    translation,
                    rotation,
                    scale,
                },
            }
        })
        .collect();

    for parent in document.nodes() {
        for child in parent.children() {
            if let Some(node) = nodes.get_mut(child.index()) {
                node.parent = Some(parent.index());
            }
        }
    }

    nodes
}

/// Converts glTF skins into joint lists and inverse bind matrices.
fn import_skins(
    document: &gltf::Document,
    buffers: &[Vec<u8>],
) -> Result<Vec<ModelSkin>, ModelAssetError> {
    let node_count = document.nodes().len();
    let mut skins = Vec::new();

    for skin in document.skins() {
        let skin_index = skin.index();
        let joints: Vec<usize> = skin.joints().map(|node| node.index()).collect();
        for (joint_index, node_index) in joints.iter().copied().enumerate() {
            if node_index >= node_count {
                return Err(ModelAssetError::InvalidSkinJoint {
                    skin_index,
                    joint_index,
                    node_index,
                });
            }
        }

        let reader = skin.reader(|buffer| {
            buffers
                .get(buffer.index())
                .map(|buffer_data| buffer_data.as_slice())
        });
        let inverse_bind_matrices: Vec<[f32; 16]> = reader
            .read_inverse_bind_matrices()
            .map(|matrices| matrices.map(flatten_gltf_matrix).collect())
            .unwrap_or_else(|| vec![IDENTITY_MATRIX; joints.len()]);
        if inverse_bind_matrices.len() != joints.len() {
            return Err(ModelAssetError::InvalidSkinInverseBindCount {
                skin_index,
                joint_count: joints.len(),
                inverse_bind_count: inverse_bind_matrices.len(),
            });
        }
        if inverse_bind_matrices
            .iter()
            .flat_map(|matrix| matrix.iter())
            .any(|value| !value.is_finite())
        {
            return Err(ModelAssetError::InvalidSkinData {
                skin_index,
                attribute: "inverse bind matrix",
            });
        }

        skins.push(ModelSkin {
            name: skin.name().map(str::to_owned),
            joints,
            inverse_bind_matrices,
        });
    }

    Ok(skins)
}

/// Flattens a glTF matrix into OFG's column-major 16-float matrix convention.
fn flatten_gltf_matrix(matrix: [[f32; 4]; 4]) -> [f32; 16] {
    [
        matrix[0][0],
        matrix[0][1],
        matrix[0][2],
        matrix[0][3],
        matrix[1][0],
        matrix[1][1],
        matrix[1][2],
        matrix[1][3],
        matrix[2][0],
        matrix[2][1],
        matrix[2][2],
        matrix[2][3],
        matrix[3][0],
        matrix[3][1],
        matrix[3][2],
        matrix[3][3],
    ]
}

/// Converts supported glTF mesh primitives into indexed vertex buffers.
fn import_primitives(
    document: &gltf::Document,
    buffers: &[Vec<u8>],
) -> Result<Vec<ModelPrimitive>, ModelAssetError> {
    let mut imported = Vec::new();

    for mesh in document.meshes() {
        for primitive in mesh.primitives() {
            if primitive.mode() != gltf::mesh::Mode::Triangles {
                return Err(ModelAssetError::UnsupportedPrimitiveMode {
                    mesh_index: mesh.index(),
                    mode: format!("{:?}", primitive.mode()),
                });
            }

            imported.push(import_primitive(mesh.clone(), primitive, buffers)?);
        }
    }

    Ok(imported)
}

/// Reads one glTF primitive and validates required vertex/index shapes.
fn import_primitive(
    mesh: gltf::Mesh<'_>,
    primitive: gltf::Primitive<'_>,
    buffers: &[Vec<u8>],
) -> Result<ModelPrimitive, ModelAssetError> {
    let reader = primitive.reader(|buffer| {
        buffers
            .get(buffer.index())
            .map(|buffer_data| buffer_data.as_slice())
    });

    let positions: Vec<[f32; 3]> = reader
        .read_positions()
        .ok_or(ModelAssetError::MissingPositions {
            mesh_index: mesh.index(),
        })?
        .collect();
    ensure_finite_vec3(mesh.index(), "POSITION", &positions)?;

    let normals: Vec<[f32; 3]> = reader
        .read_normals()
        .map(Iterator::collect)
        .unwrap_or_else(|| vec![[0.0, 1.0, 0.0]; positions.len()]);
    ensure_attribute_len(mesh.index(), "NORMAL", normals.len(), positions.len())?;
    ensure_finite_vec3(mesh.index(), "NORMAL", &normals)?;

    let texcoords: Vec<[f32; 2]> = reader
        .read_tex_coords(0)
        .map(|values| values.into_f32().collect())
        .unwrap_or_else(|| vec![[0.0, 0.0]; positions.len()]);
    ensure_attribute_len(mesh.index(), "TEXCOORD_0", texcoords.len(), positions.len())?;
    ensure_finite_vec2(mesh.index(), "TEXCOORD_0", &texcoords)?;

    let colors: Vec<[f32; 4]> = reader
        .read_colors(0)
        .map(|values| values.into_rgba_f32().collect())
        .unwrap_or_else(|| vec![[1.0, 1.0, 1.0, 1.0]; positions.len()]);
    ensure_attribute_len(mesh.index(), "COLOR_0", colors.len(), positions.len())?;
    ensure_finite_vec4(mesh.index(), "COLOR_0", &colors)?;

    let joints: Vec<[u16; 4]> = reader
        .read_joints(0)
        .map(|values| values.into_u16().collect())
        .unwrap_or_else(|| vec![[0, 0, 0, 0]; positions.len()]);
    ensure_attribute_len(mesh.index(), "JOINTS_0", joints.len(), positions.len())?;

    let weights: Vec<[f32; 4]> = reader
        .read_weights(0)
        .map(|values| values.into_f32().collect())
        .unwrap_or_else(|| vec![[0.0, 0.0, 0.0, 0.0]; positions.len()]);
    ensure_attribute_len(mesh.index(), "WEIGHTS_0", weights.len(), positions.len())?;
    ensure_finite_vec4(mesh.index(), "WEIGHTS_0", &weights)?;

    let indices: Vec<u32> = reader
        .read_indices()
        .map(|values| values.into_u32().collect())
        .unwrap_or_else(|| (0..positions.len() as u32).collect());
    if indices.len() % 3 != 0 {
        return Err(ModelAssetError::InvalidTriangleIndexCount {
            mesh_index: mesh.index(),
            index_count: indices.len(),
        });
    }

    let vertices = positions
        .into_iter()
        .zip(normals)
        .zip(texcoords)
        .zip(colors)
        .zip(joints)
        .zip(weights)
        .map(
            |(((((position, normal), texcoord0), color0), joints0), weights0)| ModelVertex {
                position,
                normal,
                texcoord0,
                color0,
                joints0,
                weights0,
            },
        )
        .collect();

    Ok(ModelPrimitive {
        mesh_index: mesh.index(),
        mesh_name: mesh.name().map(str::to_owned),
        material: primitive.material().index(),
        vertices,
        indices,
    })
}

/// Ensures an optional vertex attribute has one value per position.
fn ensure_attribute_len(
    mesh_index: usize,
    attribute: &'static str,
    actual: usize,
    expected: usize,
) -> Result<(), ModelAssetError> {
    if actual != expected {
        return Err(ModelAssetError::InvalidAttributeLength {
            mesh_index,
            attribute,
            actual,
            expected,
        });
    }

    Ok(())
}

/// Ensures 2D float attributes contain finite values.
fn ensure_finite_vec2(
    mesh_index: usize,
    attribute: &'static str,
    values: &[[f32; 2]],
) -> Result<(), ModelAssetError> {
    if values
        .iter()
        .any(|value| !value[0].is_finite() || !value[1].is_finite())
    {
        return Err(ModelAssetError::InvalidFloatData {
            mesh_index,
            attribute,
        });
    }

    Ok(())
}

/// Ensures 3D float attributes contain finite values.
fn ensure_finite_vec3(
    mesh_index: usize,
    attribute: &'static str,
    values: &[[f32; 3]],
) -> Result<(), ModelAssetError> {
    if values
        .iter()
        .any(|value| !value[0].is_finite() || !value[1].is_finite() || !value[2].is_finite())
    {
        return Err(ModelAssetError::InvalidFloatData {
            mesh_index,
            attribute,
        });
    }

    Ok(())
}

/// Ensures 4D float attributes contain finite values.
fn ensure_finite_vec4(
    mesh_index: usize,
    attribute: &'static str,
    values: &[[f32; 4]],
) -> Result<(), ModelAssetError> {
    if values.iter().any(|value| {
        !value[0].is_finite()
            || !value[1].is_finite()
            || !value[2].is_finite()
            || !value[3].is_finite()
    }) {
        return Err(ModelAssetError::InvalidFloatData {
            mesh_index,
            attribute,
        });
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn model_asset_count_helpers_sum_imported_parts() {
        let asset = ModelAsset {
            nodes: vec![ModelNode {
                name: Some("root".to_string()),
                parent: None,
                children: vec![1],
                mesh: Some(0),
                skin: Some(0),
                local_transform: ModelNodeTransform::default(),
            }],
            primitives: vec![sample_primitive(0, 2, 3), sample_primitive(1, 3, 6)],
            images: Vec::new(),
            textures: Vec::new(),
            samplers: Vec::new(),
            materials: Vec::new(),
            animations: Vec::new(),
            skins: vec![ModelSkin {
                name: Some("skin".to_string()),
                joints: vec![0],
                inverse_bind_matrices: vec![IDENTITY_MATRIX],
            }],
        };

        assert_eq!(asset.primitive_count(), 2);
        assert_eq!(asset.vertex_count(), 5);
        assert_eq!(asset.index_count(), 9);
        assert_eq!(asset.animation_count(), 0);
        assert_eq!(asset.skin_count(), 1);
        assert_eq!(asset.image_count(), 0);
        assert_eq!(asset.texture_count(), 0);
        assert_eq!(asset.material_count(), 0);
    }

    #[test]
    fn primitive_vertex_packing_matches_static_model_layout() {
        let primitive = ModelPrimitive {
            mesh_index: 0,
            mesh_name: Some("mesh".to_string()),
            material: None,
            vertices: vec![ModelVertex {
                position: [1.0, 2.0, 3.0],
                normal: [0.0, 1.0, 0.0],
                texcoord0: [0.25, 0.75],
                color0: [1.0, 0.5, 0.25, 1.0],
                joints0: [4, 3, 2, 1],
                weights0: [0.4, 0.3, 0.2, 0.1],
            }],
            indices: vec![0, 0, 0],
        };

        let floats = model_primitive_vertex_floats(&primitive);

        assert_eq!(floats.len(), MODEL_VERTEX_FLOATS as usize);
        assert_eq!(&floats[0..3], &[1.0, 2.0, 3.0]);
        assert_eq!(&floats[3..6], &[0.0, 1.0, 0.0]);
        assert_eq!(&floats[6..8], &[0.25, 0.75]);
        assert_eq!(&floats[8..12], &[1.0, 0.5, 0.25, 1.0]);
    }

    #[test]
    fn data_uri_payload_decoder_accepts_base64_and_rejects_other_payloads() {
        assert_eq!(
            decode_data_uri_payload("data:application/octet-stream;base64,AQID").unwrap(),
            vec![1, 2, 3]
        );
        assert!(matches!(
            decode_data_uri_payload("application/octet-stream;base64,AQID"),
            Err(DataUriPayloadError::Unsupported)
        ));
        assert!(matches!(
            decode_data_uri_payload("data:application/octet-stream,AQID"),
            Err(DataUriPayloadError::Unsupported)
        ));
        assert!(matches!(
            decode_data_uri_payload("data:application/octet-stream;base64,%%%"),
            Err(DataUriPayloadError::Decode(_))
        ));
        assert!(matches!(
            decode_data_uri_buffer(4, "data:application/octet-stream,AQID"),
            Err(ModelAssetError::UnsupportedDataUri {
                buffer_index: 4,
                ..
            })
        ));
        assert!(matches!(
            decode_data_uri_buffer(5, "data:application/octet-stream;base64,%%%"),
            Err(ModelAssetError::DataUriDecode {
                buffer_index: 5,
                ..
            })
        ));
    }

    #[test]
    fn private_model_import_validators_report_shape_and_float_errors() {
        assert_eq!(ensure_attribute_len(2, "NORMAL", 3, 3), Ok(()));
        assert_eq!(
            ensure_attribute_len(2, "NORMAL", 2, 3),
            Err(ModelAssetError::InvalidAttributeLength {
                mesh_index: 2,
                attribute: "NORMAL",
                actual: 2,
                expected: 3
            })
        );
        assert_eq!(ensure_finite_vec2(1, "TEXCOORD_0", &[[0.0, 1.0]]), Ok(()));
        assert_eq!(
            ensure_finite_vec3(1, "POSITION", &[[0.0, 1.0, 2.0]]),
            Ok(())
        );
        assert_eq!(
            ensure_finite_vec4(1, "COLOR_0", &[[0.0, f32::NAN, 1.0, 1.0]]),
            Err(ModelAssetError::InvalidFloatData {
                mesh_index: 1,
                attribute: "COLOR_0"
            })
        );
        assert_eq!(
            flatten_gltf_matrix([
                [1.0, 2.0, 3.0, 4.0],
                [5.0, 6.0, 7.0, 8.0],
                [9.0, 10.0, 11.0, 12.0],
                [13.0, 14.0, 15.0, 16.0],
            ]),
            [
                1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0,
                16.0
            ]
        );
    }

    #[test]
    fn model_asset_errors_format_stable_browser_diagnostics() {
        let cases = [
            (
                ModelAssetError::GltfParse("bad".to_string()),
                "Failed to parse glTF",
            ),
            (
                ModelAssetError::MissingBinaryBuffer { buffer_index: 1 },
                "GLB binary chunk",
            ),
            (
                ModelAssetError::InvalidBufferLength {
                    buffer_index: 1,
                    actual: 2,
                    expected: 3,
                },
                "expected at least 3",
            ),
            (
                ModelAssetError::UnsupportedDataUri {
                    buffer_index: 1,
                    uri: "data:text/plain,abc".to_string(),
                },
                "unsupported data URI",
            ),
            (
                ModelAssetError::DataUriDecode {
                    buffer_index: 1,
                    message: "decode".to_string(),
                },
                "could not be decoded",
            ),
            (
                ModelAssetError::UnsupportedImageDataUri {
                    image_index: 2,
                    uri: "data:text/plain,abc".to_string(),
                },
                "glTF image 2 uses unsupported data URI",
            ),
            (
                ModelAssetError::ImageDataUriDecode {
                    image_index: 2,
                    message: "decode".to_string(),
                },
                "glTF image 2 data URI could not be decoded",
            ),
            (
                ModelAssetError::InvalidImageBufferView {
                    image_index: 2,
                    buffer_view_index: 3,
                    buffer_index: 4,
                    actual: 5,
                    expected_end: 6,
                },
                "reads through byte 6",
            ),
            (
                ModelAssetError::UnsupportedExternalBuffer {
                    buffer_index: 1,
                    uri: "external.bin".to_string(),
                },
                "external URI",
            ),
            (
                ModelAssetError::UnsupportedPrimitiveMode {
                    mesh_index: 7,
                    mode: "Lines".to_string(),
                },
                "only triangles",
            ),
            (
                ModelAssetError::MissingPositions { mesh_index: 7 },
                "missing POSITION",
            ),
            (
                ModelAssetError::InvalidAttributeLength {
                    mesh_index: 7,
                    attribute: "NORMAL",
                    actual: 2,
                    expected: 3,
                },
                "attribute NORMAL has 2 values",
            ),
            (
                ModelAssetError::InvalidTriangleIndexCount {
                    mesh_index: 7,
                    index_count: 2,
                },
                "expected a multiple of 3",
            ),
            (
                ModelAssetError::InvalidFloatData {
                    mesh_index: 7,
                    attribute: "POSITION",
                },
                "contains non-finite data",
            ),
            (
                ModelAssetError::MissingAnimationInput {
                    animation_index: 1,
                    channel_index: 2,
                },
                "missing input keyframe times",
            ),
            (
                ModelAssetError::MissingAnimationOutput {
                    animation_index: 1,
                    channel_index: 2,
                },
                "missing output values",
            ),
            (
                ModelAssetError::UnsupportedAnimationInterpolation {
                    animation_index: 1,
                    channel_index: 2,
                    interpolation: "CubicSpline".to_string(),
                },
                "unsupported interpolation",
            ),
            (
                ModelAssetError::UnsupportedAnimationTarget {
                    animation_index: 1,
                    channel_index: 2,
                    target: "weights".to_string(),
                },
                "targets unsupported property",
            ),
            (
                ModelAssetError::InvalidAnimationKeyframes {
                    animation_index: 1,
                    channel_index: 2,
                    input_count: 3,
                    output_count: 4,
                },
                "has 3 input times and 4 output values",
            ),
            (
                ModelAssetError::InvalidAnimationData {
                    animation_index: 1,
                    channel_index: 2,
                    attribute: "rotation output",
                },
                "contains invalid rotation output data",
            ),
            (
                ModelAssetError::InvalidAnimationTargetNode { node_index: 9 },
                "targets missing model node",
            ),
            (
                ModelAssetError::InvalidAnimationBlendTransformCount {
                    from_count: 2,
                    to_count: 3,
                },
                "received 2 source transforms and 3 target transforms",
            ),
            (ModelAssetError::InvalidAnimationTime, "time was not finite"),
            (
                ModelAssetError::InvalidSkinIndex { skin_index: 1 },
                "glTF skin 1 does not exist",
            ),
            (
                ModelAssetError::InvalidSkinJoint {
                    skin_index: 1,
                    joint_index: 2,
                    node_index: 3,
                },
                "references missing node 3",
            ),
            (
                ModelAssetError::InvalidSkinInverseBindCount {
                    skin_index: 1,
                    joint_count: 2,
                    inverse_bind_count: 3,
                },
                "2 joints and 3 inverse bind matrices",
            ),
            (
                ModelAssetError::InvalidSkinData {
                    skin_index: 1,
                    attribute: "inverse bind matrix",
                },
                "contains invalid inverse bind matrix data",
            ),
            (
                ModelAssetError::InvalidSkinVertexJoint {
                    mesh_index: 1,
                    vertex_index: 2,
                    joint_index: 3,
                },
                "vertex 2 references missing skin joint 3",
            ),
            (
                ModelAssetError::InvalidSkinNodeTransformCount {
                    node_count: 2,
                    transform_count: 3,
                },
                "received 3 node transforms for 2 model nodes",
            ),
        ];

        for (error, expected_fragment) in cases {
            assert!(
                error.to_string().contains(expected_fragment),
                "expected '{error}' to contain '{expected_fragment}'"
            );
        }
    }

    fn sample_primitive(
        mesh_index: usize,
        vertex_count: usize,
        index_count: usize,
    ) -> ModelPrimitive {
        ModelPrimitive {
            mesh_index,
            mesh_name: Some(format!("mesh-{mesh_index}")),
            material: None,
            vertices: vec![
                ModelVertex {
                    position: [0.0, 0.0, 0.0],
                    normal: [0.0, 1.0, 0.0],
                    texcoord0: [0.0, 0.0],
                    color0: [1.0, 1.0, 1.0, 1.0],
                    joints0: [0, 0, 0, 0],
                    weights0: [0.0, 0.0, 0.0, 0.0],
                };
                vertex_count
            ],
            indices: vec![0; index_count],
        }
    }
}
