// Renderer-facing preparation for imported glTF model assets.
// This keeps model primitive selection, material packet creation, and CPU
// skinned pose baking out of the WebGPU facade.

use std::fmt;

use crate::materials::{
    build_metallic_roughness_material_packet, build_specular_glossiness_material_packet,
    MaterialPacketError,
};
use crate::model_animation::ModelAnimationClip;
use crate::model_assets::{
    model_primitive_vertex_floats, ModelAsset, ModelAssetError, ModelNodeTransform, ModelPrimitive,
};
use crate::model_materials::{ModelMaterial, ModelMaterialWorkflow};
use crate::model_skinning::{skin_joint_matrices, skin_primitive_vertices};
use crate::render_uniforms::MATERIAL_PACKET_FLOATS;

#[derive(Clone, Debug, PartialEq)]
pub struct ModelRenderAssets {
    pub vertices: Vec<f32>,
    pub indices: Vec<u32>,
    pub material_packet: [f32; MATERIAL_PACKET_FLOATS],
    pub mesh_node_index: usize,
    pub skin_joint_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelRenderAssetError {
    MissingPrimitive,
    MissingMeshNode { mesh_index: usize },
    MissingSkinForNode { node_index: usize },
    ModelAsset(ModelAssetError),
    MaterialPacket(MaterialPacketError),
}

impl fmt::Display for ModelRenderAssetError {
    /// Formats renderer-asset preparation failures for browser diagnostics.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingPrimitive => write!(formatter, "glTF model has no renderable primitives"),
            Self::MissingMeshNode { mesh_index } => write!(
                formatter,
                "glTF model has no node instancing mesh {mesh_index}"
            ),
            Self::MissingSkinForNode { node_index } => write!(
                formatter,
                "glTF model node {node_index} has no skin for CPU skinning"
            ),
            Self::ModelAsset(error) => write!(formatter, "{error}"),
            Self::MaterialPacket(error) => write!(formatter, "{error:?}"),
        }
    }
}

/// Bakes the first skinned primitive into the static model vertex layout.
pub fn skinned_model_render_assets(
    model: &ModelAsset,
    animation: &ModelAnimationClip,
    node_base_transforms: &[ModelNodeTransform],
    pose_time_seconds: f32,
) -> Result<ModelRenderAssets, ModelRenderAssetError> {
    let primitive = model
        .primitives
        .first()
        .ok_or(ModelRenderAssetError::MissingPrimitive)?;
    let mesh_node_index = first_primitive_node_index(model)?;
    let mesh_node = &model.nodes[mesh_node_index];
    let skin_index = mesh_node
        .skin
        .ok_or(ModelRenderAssetError::MissingSkinForNode {
            node_index: mesh_node_index,
        })?;
    let posed_transforms = animation
        .sample_transforms(node_base_transforms, pose_time_seconds)
        .map_err(ModelRenderAssetError::ModelAsset)?;
    let joint_matrices = skin_joint_matrices(model, skin_index, &posed_transforms)
        .map_err(ModelRenderAssetError::ModelAsset)?;
    let skinned_vertices = skin_primitive_vertices(primitive, &joint_matrices)
        .map_err(ModelRenderAssetError::ModelAsset)?;
    let skinned_primitive = ModelPrimitive {
        vertices: skinned_vertices,
        ..primitive.clone()
    };
    let material = primitive
        .material
        .and_then(|material_index| model.materials.get(material_index));

    Ok(ModelRenderAssets {
        vertices: model_primitive_vertex_floats(&skinned_primitive),
        indices: primitive.indices.clone(),
        material_packet: model_material_packet(material)?,
        mesh_node_index,
        skin_joint_count: model.skins[skin_index].joints.len(),
    })
}

/// Finds the first node that instances the first imported primitive's mesh.
pub fn first_primitive_node_index(model: &ModelAsset) -> Result<usize, ModelRenderAssetError> {
    let primitive = model
        .primitives
        .first()
        .ok_or(ModelRenderAssetError::MissingPrimitive)?;

    model
        .nodes
        .iter()
        .position(|node| node.mesh == Some(primitive.mesh_index))
        .ok_or(ModelRenderAssetError::MissingMeshNode {
            mesh_index: primitive.mesh_index,
        })
}

/// Builds a renderer packet from one imported glTF material workflow.
pub fn model_material_packet(
    material: Option<&ModelMaterial>,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], ModelRenderAssetError> {
    match material.map(|material| &material.workflow) {
        Some(ModelMaterialWorkflow::MetallicRoughness {
            base_color_factor,
            metallic_factor,
            roughness_factor,
            ..
        }) => build_metallic_roughness_material_packet(
            *base_color_factor,
            *metallic_factor,
            *roughness_factor,
            1.0,
        ),
        Some(ModelMaterialWorkflow::SpecularGlossiness {
            diffuse_factor,
            specular_factor,
            glossiness_factor,
            ..
        }) => build_specular_glossiness_material_packet(
            *diffuse_factor,
            *specular_factor,
            *glossiness_factor,
            1.0,
        ),
        None => build_metallic_roughness_material_packet([1.0, 1.0, 1.0, 1.0], 1.0, 1.0, 1.0),
    }
    .map_err(ModelRenderAssetError::MaterialPacket)
}
