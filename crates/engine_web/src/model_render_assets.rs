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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::materials::{
        MATERIAL_WORKFLOW_METALLIC_ROUGHNESS, MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS,
    };
    use crate::model_animation::ModelAnimationClip;
    use crate::model_assets::{ModelNode, ModelSkin, ModelVertex};
    use crate::model_materials::ModelAlphaMode;

    #[test]
    fn first_primitive_node_index_reports_missing_primitive_and_mesh_nodes() {
        assert_eq!(
            first_primitive_node_index(&empty_model_asset()),
            Err(ModelRenderAssetError::MissingPrimitive)
        );

        let mut model = empty_model_asset();
        model.primitives.push(test_primitive(4, None));
        model.nodes.push(test_node(None, None));

        assert_eq!(
            first_primitive_node_index(&model),
            Err(ModelRenderAssetError::MissingMeshNode { mesh_index: 4 })
        );

        model.nodes.push(test_node(Some(4), None));
        assert_eq!(first_primitive_node_index(&model), Ok(1));
    }

    #[test]
    fn skinned_model_render_assets_reports_boundary_errors() {
        let animation = empty_animation();
        assert_eq!(
            skinned_model_render_assets(&empty_model_asset(), &animation, &[], 0.0),
            Err(ModelRenderAssetError::MissingPrimitive)
        );

        let mut no_skin = empty_model_asset();
        no_skin.primitives.push(test_primitive(0, None));
        no_skin.nodes.push(test_node(Some(0), None));
        assert_eq!(
            skinned_model_render_assets(
                &no_skin,
                &animation,
                &[ModelNodeTransform::default()],
                0.0
            ),
            Err(ModelRenderAssetError::MissingSkinForNode { node_index: 0 })
        );

        let mut invalid_time = no_skin;
        invalid_time.nodes[0].skin = Some(0);
        invalid_time.skins.push(ModelSkin {
            name: None,
            joints: vec![],
            inverse_bind_matrices: vec![],
        });
        assert!(matches!(
            skinned_model_render_assets(
                &invalid_time,
                &animation,
                &[ModelNodeTransform::default()],
                f32::NAN,
            ),
            Err(ModelRenderAssetError::ModelAsset(
                ModelAssetError::InvalidAnimationTime
            ))
        ));
    }

    #[test]
    fn material_packets_cover_default_and_gltf_workflows() {
        let default_packet = model_material_packet(None).expect("default material should build");
        assert_eq!(default_packet[8], MATERIAL_WORKFLOW_METALLIC_ROUGHNESS);
        assert_eq!(default_packet[0..4], [1.0, 1.0, 1.0, 1.0]);

        let specular = material_with_workflow(ModelMaterialWorkflow::SpecularGlossiness {
            diffuse_factor: [0.2, 0.3, 0.4, 0.5],
            diffuse_texture: None,
            specular_factor: [0.6, 0.7, 0.8],
            glossiness_factor: 0.9,
            specular_glossiness_texture: None,
        });
        let packet =
            model_material_packet(Some(&specular)).expect("specular material should build");
        assert_eq!(packet[0..4], [0.2, 0.3, 0.4, 0.5]);
        assert_eq!(packet[4..7], [0.6, 0.7, 0.8]);
        assert_eq!(packet[7], 0.9);
        assert_eq!(packet[8], MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS);

        let invalid = material_with_workflow(ModelMaterialWorkflow::MetallicRoughness {
            base_color_factor: [1.0, f32::NAN, 1.0, 1.0],
            base_color_texture: None,
            metallic_factor: 1.0,
            roughness_factor: 1.0,
            metallic_roughness_texture: None,
        });
        assert_eq!(
            model_material_packet(Some(&invalid)),
            Err(ModelRenderAssetError::MaterialPacket(
                MaterialPacketError::InvalidValue
            ))
        );
    }

    #[test]
    fn render_asset_errors_format_stable_diagnostics() {
        assert_eq!(
            ModelRenderAssetError::MissingPrimitive.to_string(),
            "glTF model has no renderable primitives"
        );
        assert_eq!(
            ModelRenderAssetError::MissingMeshNode { mesh_index: 8 }.to_string(),
            "glTF model has no node instancing mesh 8"
        );
        assert_eq!(
            ModelRenderAssetError::MissingSkinForNode { node_index: 2 }.to_string(),
            "glTF model node 2 has no skin for CPU skinning"
        );
        assert!(
            !ModelRenderAssetError::ModelAsset(ModelAssetError::InvalidAnimationTime)
                .to_string()
                .is_empty()
        );
        assert_eq!(
            ModelRenderAssetError::MaterialPacket(MaterialPacketError::InvalidValue).to_string(),
            "InvalidValue"
        );
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

    fn test_primitive(mesh_index: usize, material: Option<usize>) -> ModelPrimitive {
        ModelPrimitive {
            mesh_index,
            mesh_name: Some("mesh".into()),
            material,
            vertices: vec![ModelVertex {
                position: [0.0, 0.0, 0.0],
                normal: [0.0, 1.0, 0.0],
                texcoord0: [0.0, 0.0],
                color0: [1.0, 1.0, 1.0, 1.0],
                joints0: [0, 0, 0, 0],
                weights0: [1.0, 0.0, 0.0, 0.0],
            }],
            indices: vec![0],
        }
    }

    fn test_node(mesh: Option<usize>, skin: Option<usize>) -> ModelNode {
        ModelNode {
            name: Some("node".into()),
            parent: None,
            children: vec![],
            mesh,
            skin,
            local_transform: ModelNodeTransform::default(),
        }
    }

    fn empty_animation() -> ModelAnimationClip {
        ModelAnimationClip {
            name: Some("empty".into()),
            duration_seconds: 1.0,
            channels: vec![],
        }
    }

    fn material_with_workflow(workflow: ModelMaterialWorkflow) -> ModelMaterial {
        ModelMaterial {
            name: Some("material".into()),
            workflow,
            base_color_factor: [1.0, 1.0, 1.0, 1.0],
            metallic_factor: 1.0,
            roughness_factor: 1.0,
            normal_texture: None,
            occlusion_texture: None,
            emissive_texture: None,
            emissive_factor: [0.0, 0.0, 0.0],
            alpha_mode: ModelAlphaMode::Opaque,
            alpha_cutoff: 0.5,
            double_sided: false,
        }
    }
}
