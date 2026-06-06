// Rust-owned locomotion animation for the temporary player GLTF model.
// This module keeps clip selection, crossfade state, CPU skinning, and
// renderer-ready vertex baking out of the WebGPU facade.

use std::fmt;

use crate::materials::{build_material_packet, MaterialPacketError};
use crate::model_animation::{blend_node_transforms, ModelAnimationClip};
use crate::model_assets::{
    model_primitive_vertex_floats, ModelAsset, ModelAssetError, ModelMaterial, ModelNodeTransform,
    ModelPrimitive,
};
use crate::model_render_assets::{first_primitive_node_index, ModelRenderAssetError};
use crate::model_skinning::{skin_joint_matrices, skin_primitive_vertices};
use crate::render_uniforms::MATERIAL_PACKET_FLOATS;

pub const QUATERNIUS_IDLE_CLIP_NAME: &str = "Idle_FoldArms_Loop";
pub const QUATERNIUS_WALK_CLIP_NAME: &str = "Walk_Carry_Loop";
const DEFAULT_BLEND_DURATION_SECONDS: f32 = 0.18;
const MOVEMENT_EPSILON: f32 = 0.01;

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerCharacterModel {
    model: ModelAsset,
    primitive: ModelPrimitive,
    skin_index: usize,
    node_base_transforms: Vec<ModelNodeTransform>,
    controller: LocomotionAnimationController,
    material_packet: [f32; MATERIAL_PACKET_FLOATS],
    mesh_node_index: usize,
    skin_joint_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerCharacterAnimationSnapshot {
    pub runtime: &'static str,
    pub active_clip_name: String,
    pub next_clip_name: Option<String>,
    pub time_seconds: f32,
    pub duration_seconds: f32,
    pub blend_weight: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub enum PlayerCharacterModelError {
    MissingAnimationClip { name: String },
    InvalidBlendDuration(f32),
    InvalidDeltaSeconds(f32),
    ModelAsset(ModelAssetError),
    MaterialPacket(MaterialPacketError),
    RenderAsset(ModelRenderAssetError),
}

#[derive(Clone, Debug, PartialEq)]
pub struct LocomotionAnimationController {
    idle_clip: ModelAnimationClip,
    walk_clip: ModelAnimationClip,
    idle_time_seconds: f32,
    walk_time_seconds: f32,
    active: LocomotionClip,
    next: Option<LocomotionClip>,
    blend_elapsed_seconds: f32,
    blend_duration_seconds: f32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LocomotionClip {
    Idle,
    Walk,
}

impl PlayerCharacterModel {
    /// Builds the runtime player character from a skinned GLTF model asset.
    pub fn from_model(model: ModelAsset) -> Result<Self, PlayerCharacterModelError> {
        let primitive =
            model
                .primitives
                .first()
                .cloned()
                .ok_or(PlayerCharacterModelError::RenderAsset(
                    ModelRenderAssetError::MissingPrimitive,
                ))?;
        let mesh_node_index =
            first_primitive_node_index(&model).map_err(PlayerCharacterModelError::RenderAsset)?;
        let skin_index =
            model.nodes[mesh_node_index]
                .skin
                .ok_or(PlayerCharacterModelError::RenderAsset(
                    ModelRenderAssetError::MissingSkinForNode {
                        node_index: mesh_node_index,
                    },
                ))?;
        let skin_joint_count = model
            .skins
            .get(skin_index)
            .ok_or(PlayerCharacterModelError::ModelAsset(
                ModelAssetError::InvalidSkinIndex { skin_index },
            ))?
            .joints
            .len();
        let idle_clip = named_animation_clip(&model, QUATERNIUS_IDLE_CLIP_NAME)?;
        let walk_clip = named_animation_clip(&model, QUATERNIUS_WALK_CLIP_NAME)?;
        let material = primitive
            .material
            .and_then(|material_index| model.materials.get(material_index));
        let material_packet = model_material_packet(material)?;
        let node_base_transforms = model
            .nodes
            .iter()
            .map(|node| node.local_transform)
            .collect();

        Ok(Self {
            model,
            primitive,
            skin_index,
            node_base_transforms,
            controller: LocomotionAnimationController::new(
                idle_clip,
                walk_clip,
                DEFAULT_BLEND_DURATION_SECONDS,
            )?,
            material_packet,
            mesh_node_index,
            skin_joint_count,
        })
    }

    /// Returns renderer-ready vertices for the current animation state.
    pub fn current_vertices(&self) -> Result<Vec<f32>, PlayerCharacterModelError> {
        let pose = self
            .controller
            .current_pose(&self.node_base_transforms)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        self.vertices_for_pose(&pose)
    }

    /// Advances locomotion animation and returns renderer-ready skinned vertices.
    pub fn tick_vertices(
        &mut self,
        delta_seconds: f32,
        horizontal_movement: [f32; 2],
    ) -> Result<Vec<f32>, PlayerCharacterModelError> {
        let moving = horizontal_movement_is_active(horizontal_movement);
        let pose =
            self.controller
                .advance_pose(&self.node_base_transforms, delta_seconds, moving)?;
        self.vertices_for_pose(&pose)
    }

    /// Returns the imported index buffer for the selected primitive.
    pub fn indices(&self) -> &[u32] {
        &self.primitive.indices
    }

    /// Returns the material packet for the selected primitive.
    pub fn material_packet(&self) -> [f32; MATERIAL_PACKET_FLOATS] {
        self.material_packet
    }

    /// Returns the GLTF node that instances the selected mesh primitive.
    pub fn mesh_node_index(&self) -> usize {
        self.mesh_node_index
    }

    /// Returns the number of joints in the selected skin.
    pub fn skin_joint_count(&self) -> usize {
        self.skin_joint_count
    }

    /// Returns the current locomotion debug snapshot.
    pub fn animation_snapshot(&self) -> PlayerCharacterAnimationSnapshot {
        self.controller.snapshot()
    }

    /// CPU-skins one sampled pose into the renderer's model vertex layout.
    fn vertices_for_pose(
        &self,
        pose: &[ModelNodeTransform],
    ) -> Result<Vec<f32>, PlayerCharacterModelError> {
        let joint_matrices = skin_joint_matrices(&self.model, self.skin_index, pose)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let skinned_vertices = skin_primitive_vertices(&self.primitive, &joint_matrices)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let skinned_primitive = ModelPrimitive {
            vertices: skinned_vertices,
            ..self.primitive.clone()
        };

        Ok(model_primitive_vertex_floats(&skinned_primitive))
    }
}

impl LocomotionAnimationController {
    /// Creates an idle/walk animation controller with a fixed crossfade time.
    pub fn new(
        idle_clip: ModelAnimationClip,
        walk_clip: ModelAnimationClip,
        blend_duration_seconds: f32,
    ) -> Result<Self, PlayerCharacterModelError> {
        if !blend_duration_seconds.is_finite() || blend_duration_seconds <= 0.0 {
            return Err(PlayerCharacterModelError::InvalidBlendDuration(
                blend_duration_seconds,
            ));
        }

        Ok(Self {
            idle_clip,
            walk_clip,
            idle_time_seconds: 0.0,
            walk_time_seconds: 0.0,
            active: LocomotionClip::Idle,
            next: None,
            blend_elapsed_seconds: 0.0,
            blend_duration_seconds,
        })
    }

    /// Samples the current controller pose without advancing time.
    pub fn current_pose(
        &self,
        base_transforms: &[ModelNodeTransform],
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        self.sample_pose(self.active, base_transforms)
    }

    /// Advances clip clocks, updates the target clip, and returns the blended pose.
    pub fn advance_pose(
        &mut self,
        base_transforms: &[ModelNodeTransform],
        delta_seconds: f32,
        moving: bool,
    ) -> Result<Vec<ModelNodeTransform>, PlayerCharacterModelError> {
        if !delta_seconds.is_finite() || delta_seconds < 0.0 {
            return Err(PlayerCharacterModelError::InvalidDeltaSeconds(
                delta_seconds,
            ));
        }

        let desired = if moving {
            LocomotionClip::Walk
        } else {
            LocomotionClip::Idle
        };
        if desired != self.target_clip() {
            self.next = Some(desired);
            self.blend_elapsed_seconds = 0.0;
        }

        self.idle_time_seconds += delta_seconds;
        self.walk_time_seconds += delta_seconds;

        let Some(next) = self.next else {
            return self
                .sample_pose(self.active, base_transforms)
                .map_err(PlayerCharacterModelError::ModelAsset);
        };

        self.blend_elapsed_seconds += delta_seconds;
        let blend_weight = self.blend_weight();
        let from_pose = self
            .sample_pose(self.active, base_transforms)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let to_pose = self
            .sample_pose(next, base_transforms)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        if blend_weight >= 1.0 {
            self.active = next;
            self.next = None;
            self.blend_elapsed_seconds = 0.0;
            return Ok(to_pose);
        }

        blend_node_transforms(&from_pose, &to_pose, blend_weight)
            .map_err(PlayerCharacterModelError::ModelAsset)
    }

    /// Returns the current animation debug state.
    pub fn snapshot(&self) -> PlayerCharacterAnimationSnapshot {
        let active_clip = self.clip(self.active);

        PlayerCharacterAnimationSnapshot {
            runtime: "rust",
            active_clip_name: self.clip_name(self.active),
            next_clip_name: self.next.map(|clip| self.clip_name(clip)),
            time_seconds: active_clip.wrapped_time(self.clip_time_seconds(self.active)),
            duration_seconds: active_clip.duration_seconds,
            blend_weight: self.blend_weight(),
        }
    }

    /// Returns the active clip, or an in-flight transition target.
    fn target_clip(&self) -> LocomotionClip {
        self.next.unwrap_or(self.active)
    }

    /// Samples one controller clip over the base model pose.
    fn sample_pose(
        &self,
        clip: LocomotionClip,
        base_transforms: &[ModelNodeTransform],
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        self.clip(clip)
            .sample_transforms(base_transforms, self.clip_time_seconds(clip))
    }

    /// Returns one of the controller clips.
    fn clip(&self, clip: LocomotionClip) -> &ModelAnimationClip {
        match clip {
            LocomotionClip::Idle => &self.idle_clip,
            LocomotionClip::Walk => &self.walk_clip,
        }
    }

    /// Returns one of the controller clip clocks.
    fn clip_time_seconds(&self, clip: LocomotionClip) -> f32 {
        match clip {
            LocomotionClip::Idle => self.idle_time_seconds,
            LocomotionClip::Walk => self.walk_time_seconds,
        }
    }

    /// Returns a stable debug name for one controller clip.
    fn clip_name(&self, clip: LocomotionClip) -> String {
        self.clip(clip).name.clone().unwrap_or_else(|| match clip {
            LocomotionClip::Idle => "idle".to_string(),
            LocomotionClip::Walk => "walk".to_string(),
        })
    }

    /// Returns the normalized crossfade weight.
    fn blend_weight(&self) -> f32 {
        if self.next.is_none() {
            return 0.0;
        }

        (self.blend_elapsed_seconds / self.blend_duration_seconds).clamp(0.0, 1.0)
    }
}

impl fmt::Display for PlayerCharacterModelError {
    /// Formats player character model and locomotion errors for browser logs.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingAnimationClip { name } => {
                write!(
                    formatter,
                    "glTF player model is missing animation clip '{name}'"
                )
            }
            Self::InvalidBlendDuration(duration) => write!(
                formatter,
                "glTF player animation blend duration was invalid: {duration}"
            ),
            Self::InvalidDeltaSeconds(delta_seconds) => write!(
                formatter,
                "glTF player animation delta time was invalid: {delta_seconds}"
            ),
            Self::ModelAsset(error) => write!(formatter, "{error}"),
            Self::MaterialPacket(error) => write!(formatter, "{error:?}"),
            Self::RenderAsset(error) => write!(formatter, "{error}"),
        }
    }
}

impl std::error::Error for PlayerCharacterModelError {}

/// Returns true when horizontal movement input should drive the walk clip.
pub fn horizontal_movement_is_active(horizontal_movement: [f32; 2]) -> bool {
    horizontal_movement[0].abs() > MOVEMENT_EPSILON
        || horizontal_movement[1].abs() > MOVEMENT_EPSILON
}

/// Finds an animation clip by exact imported glTF name.
fn named_animation_clip(
    model: &ModelAsset,
    name: &str,
) -> Result<ModelAnimationClip, PlayerCharacterModelError> {
    model
        .animations
        .iter()
        .find(|clip| clip.name.as_deref() == Some(name))
        .cloned()
        .ok_or_else(|| PlayerCharacterModelError::MissingAnimationClip {
            name: name.to_string(),
        })
}

/// Builds the fallback material packet for the current model pipeline.
fn model_material_packet(
    material: Option<&ModelMaterial>,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], PlayerCharacterModelError> {
    let albedo = material
        .map(|material| material.base_color_factor)
        .unwrap_or([1.0, 1.0, 1.0, 1.0]);

    build_material_packet(albedo, [0.08, 0.08, 0.08], 0.18, 0.0, 1.0)
        .map_err(PlayerCharacterModelError::MaterialPacket)
}
