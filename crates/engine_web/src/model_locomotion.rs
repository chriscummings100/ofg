// Rust-owned locomotion animation for the temporary player GLTF model.
// This module keeps clip selection, crossfade state, CPU skinning, and
// renderer-ready vertex baking out of the WebGPU facade.

use std::fmt;

use crate::materials::{
    build_metallic_roughness_material_packet, build_specular_glossiness_material_packet,
    MaterialPacketError,
};
use crate::model_animation::{blend_node_transforms, ModelAnimationClip};
use crate::model_assets::{
    model_primitive_vertex_floats, ModelAsset, ModelAssetError, ModelNodeTransform, ModelPrimitive,
};
use crate::model_materials::{ModelMaterial, ModelMaterialWorkflow};
use crate::model_render_assets::ModelRenderAssetError;
use crate::model_skinning::{skin_joint_matrices, skin_primitive_vertices};
use crate::render_uniforms::MATERIAL_PACKET_FLOATS;

pub const QUATERNIUS_IDLE_CLIP_NAME: &str = "Idle_Loop";
pub const QUATERNIUS_WALK_CLIP_NAME: &str = "Walk_Loop";
pub const QUATERNIUS_RUN_CLIP_NAME: &str = "Sprint_Loop";
const DEFAULT_BLEND_DURATION_SECONDS: f32 = 0.18;
const MOVEMENT_EPSILON: f32 = 0.01;

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerCharacterModel {
    model: ModelAsset,
    parts: Vec<PlayerCharacterPart>,
    node_base_transforms: Vec<ModelNodeTransform>,
    controller: LocomotionAnimationController,
    skin_joint_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
struct PlayerCharacterPart {
    primitive: ModelPrimitive,
    skin_index: usize,
    material_packet: [f32; MATERIAL_PACKET_FLOATS],
    mesh_node_index: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerCharacterAnimationSnapshot {
    pub runtime: &'static str,
    pub active_clip_name: String,
    pub next_clip_name: Option<String>,
    pub time_seconds: f32,
    pub duration_seconds: f32,
    pub blend_weight: f32,
    pub walk_run_blend_weight: f32,
    pub playback_scale: f32,
    pub locomotion_speed_meters_per_second: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerCharacterLocomotionTuning {
    pub walk_speed_meters_per_second: f32,
    pub run_speed_meters_per_second: f32,
    pub idle_playback_scale: f32,
    pub walk_playback_scale: f32,
    pub run_playback_scale: f32,
}

impl Default for PlayerCharacterLocomotionTuning {
    fn default() -> Self {
        Self {
            walk_speed_meters_per_second: 5.5,
            run_speed_meters_per_second: 16.5,
            idle_playback_scale: 1.0,
            walk_playback_scale: 1.0,
            run_playback_scale: 1.0,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum PlayerCharacterModelError {
    MissingAnimationClip { name: String },
    InvalidBlendDuration(f32),
    InvalidDeltaSeconds(f32),
    InvalidLocomotionSpeed(f32),
    InvalidLocomotionTuning(&'static str, f32),
    ModelAsset(ModelAssetError),
    MaterialPacket(MaterialPacketError),
    RenderAsset(ModelRenderAssetError),
}

#[derive(Clone, Debug, PartialEq)]
pub struct LocomotionAnimationController {
    idle_clip: ModelAnimationClip,
    walk_clip: ModelAnimationClip,
    run_clip: ModelAnimationClip,
    idle_time_seconds: f32,
    walk_time_seconds: f32,
    run_time_seconds: f32,
    active: LocomotionState,
    next: Option<LocomotionState>,
    blend_elapsed_seconds: f32,
    blend_duration_seconds: f32,
    tuning: PlayerCharacterLocomotionTuning,
    last_locomotion_speed_meters_per_second: f32,
    last_walk_run_blend_weight: f32,
    last_playback_scale: f32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LocomotionState {
    Idle,
    Moving,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MovementClip {
    Walk,
    Run,
}

impl PlayerCharacterModel {
    /// Builds the runtime player character from a skinned GLTF model asset.
    pub fn from_model(model: ModelAsset) -> Result<Self, PlayerCharacterModelError> {
        Self::from_body_and_animation_models(model.clone(), &model)
    }

    /// Builds the runtime player character from a skinned body and animation source model.
    pub fn from_body_and_animation_models(
        body_model: ModelAsset,
        animation_model: &ModelAsset,
    ) -> Result<Self, PlayerCharacterModelError> {
        let parts = skinned_primitive_parts(&body_model)?;
        let skin_joint_count = parts
            .iter()
            .filter_map(|part| body_model.skins.get(part.skin_index))
            .map(|skin| skin.joints.len())
            .max()
            .unwrap_or(0);
        let idle_clip = named_animation_clip(animation_model, QUATERNIUS_IDLE_CLIP_NAME)?;
        let walk_clip = named_animation_clip(animation_model, QUATERNIUS_WALK_CLIP_NAME)?;
        let run_clip = named_animation_clip(animation_model, QUATERNIUS_RUN_CLIP_NAME)?;
        let node_base_transforms = body_model
            .nodes
            .iter()
            .map(|node| node.local_transform)
            .collect();

        Ok(Self {
            model: body_model,
            parts,
            node_base_transforms,
            controller: LocomotionAnimationController::new(
                idle_clip,
                walk_clip,
                run_clip,
                DEFAULT_BLEND_DURATION_SECONDS,
                PlayerCharacterLocomotionTuning::default(),
            )?,
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

    /// Returns renderer-ready vertices for every character primitive.
    pub fn current_part_vertices(&self) -> Result<Vec<Vec<f32>>, PlayerCharacterModelError> {
        let pose = self
            .controller
            .current_pose(&self.node_base_transforms)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        self.part_vertices_for_pose(&pose)
    }

    /// Advances locomotion animation and returns renderer-ready skinned vertices.
    pub fn tick_vertices(
        &mut self,
        delta_seconds: f32,
        locomotion_speed_meters_per_second: f32,
    ) -> Result<Vec<f32>, PlayerCharacterModelError> {
        let pose = self.controller.advance_pose(
            &self.node_base_transforms,
            delta_seconds,
            locomotion_speed_meters_per_second,
        )?;
        self.vertices_for_pose(&pose)
    }

    /// Advances locomotion animation and returns renderer-ready vertices per primitive.
    pub fn tick_part_vertices(
        &mut self,
        delta_seconds: f32,
        locomotion_speed_meters_per_second: f32,
    ) -> Result<Vec<Vec<f32>>, PlayerCharacterModelError> {
        let pose = self.controller.advance_pose(
            &self.node_base_transforms,
            delta_seconds,
            locomotion_speed_meters_per_second,
        )?;
        self.part_vertices_for_pose(&pose)
    }

    /// Returns the imported index buffer for the selected primitive.
    pub fn indices(&self) -> &[u32] {
        self.part_indices(0)
    }

    /// Returns the number of skinned primitives in this character model.
    pub fn part_count(&self) -> usize {
        self.parts.len()
    }

    /// Returns one imported index buffer for a character primitive.
    pub fn part_indices(&self, part_index: usize) -> &[u32] {
        &self.parts[part_index].primitive.indices
    }

    /// Returns the material packet for the selected primitive.
    pub fn material_packet(&self) -> [f32; MATERIAL_PACKET_FLOATS] {
        self.part_material_packet(0)
    }

    /// Returns one material packet for a character primitive.
    pub fn part_material_packet(&self, part_index: usize) -> [f32; MATERIAL_PACKET_FLOATS] {
        self.parts[part_index].material_packet
    }

    /// Returns the imported glTF material index for one character primitive.
    pub fn part_material_index(&self, part_index: usize) -> Option<usize> {
        self.parts[part_index].primitive.material
    }

    /// Returns the GLTF node that instances the selected mesh primitive.
    pub fn mesh_node_index(&self) -> usize {
        self.part_mesh_node_index(0)
    }

    /// Returns the GLTF node that instances one mesh primitive.
    pub fn part_mesh_node_index(&self, part_index: usize) -> usize {
        self.parts[part_index].mesh_node_index
    }

    /// Returns the number of joints in the selected skin.
    pub fn skin_joint_count(&self) -> usize {
        self.skin_joint_count
    }

    /// Returns the current locomotion debug snapshot.
    pub fn animation_snapshot(&self) -> PlayerCharacterAnimationSnapshot {
        self.controller.snapshot()
    }

    /// Returns the current numeric locomotion tuning.
    pub fn locomotion_tuning(&self) -> PlayerCharacterLocomotionTuning {
        self.controller.tuning()
    }

    /// Replaces numeric locomotion tuning for this character.
    pub fn set_locomotion_tuning(
        &mut self,
        tuning: PlayerCharacterLocomotionTuning,
    ) -> Result<(), PlayerCharacterModelError> {
        self.controller.set_tuning(tuning)
    }

    /// CPU-skins one sampled pose into the renderer's model vertex layout.
    fn vertices_for_pose(
        &self,
        pose: &[ModelNodeTransform],
    ) -> Result<Vec<f32>, PlayerCharacterModelError> {
        self.part_vertices_for_pose(pose)?.into_iter().next().ok_or(
            PlayerCharacterModelError::RenderAsset(ModelRenderAssetError::MissingPrimitive),
        )
    }

    /// CPU-skins every sampled pose part into the renderer's model vertex layout.
    fn part_vertices_for_pose(
        &self,
        pose: &[ModelNodeTransform],
    ) -> Result<Vec<Vec<f32>>, PlayerCharacterModelError> {
        self.parts
            .iter()
            .map(|part| self.vertices_for_part_pose(part, pose))
            .collect()
    }

    /// CPU-skins one part of a sampled pose into the renderer's model vertex layout.
    fn vertices_for_part_pose(
        &self,
        part: &PlayerCharacterPart,
        pose: &[ModelNodeTransform],
    ) -> Result<Vec<f32>, PlayerCharacterModelError> {
        let joint_matrices = skin_joint_matrices(&self.model, part.skin_index, pose)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let skinned_vertices = skin_primitive_vertices(&part.primitive, &joint_matrices)
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let skinned_primitive = ModelPrimitive {
            vertices: skinned_vertices,
            ..part.primitive.clone()
        };

        Ok(model_primitive_vertex_floats(&skinned_primitive))
    }
}

impl LocomotionAnimationController {
    /// Creates an idle/walk/run animation controller with a fixed crossfade time.
    pub fn new(
        idle_clip: ModelAnimationClip,
        walk_clip: ModelAnimationClip,
        run_clip: ModelAnimationClip,
        blend_duration_seconds: f32,
        tuning: PlayerCharacterLocomotionTuning,
    ) -> Result<Self, PlayerCharacterModelError> {
        if !blend_duration_seconds.is_finite() || blend_duration_seconds <= 0.0 {
            return Err(PlayerCharacterModelError::InvalidBlendDuration(
                blend_duration_seconds,
            ));
        }
        validate_locomotion_tuning(tuning)?;

        Ok(Self {
            idle_clip,
            walk_clip,
            run_clip,
            idle_time_seconds: 0.0,
            walk_time_seconds: 0.0,
            run_time_seconds: 0.0,
            active: LocomotionState::Idle,
            next: None,
            blend_elapsed_seconds: 0.0,
            blend_duration_seconds,
            tuning,
            last_locomotion_speed_meters_per_second: 0.0,
            last_walk_run_blend_weight: 0.0,
            last_playback_scale: tuning.idle_playback_scale,
        })
    }

    /// Samples the current controller pose without advancing time.
    pub fn current_pose(
        &self,
        base_transforms: &[ModelNodeTransform],
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        self.sample_state_pose(
            self.active,
            base_transforms,
            self.last_locomotion_speed_meters_per_second,
        )
    }

    /// Advances clip clocks, updates target locomotion state, and returns the blended pose.
    pub fn advance_pose(
        &mut self,
        base_transforms: &[ModelNodeTransform],
        delta_seconds: f32,
        locomotion_speed_meters_per_second: f32,
    ) -> Result<Vec<ModelNodeTransform>, PlayerCharacterModelError> {
        if !delta_seconds.is_finite() || delta_seconds < 0.0 {
            return Err(PlayerCharacterModelError::InvalidDeltaSeconds(
                delta_seconds,
            ));
        }
        if !locomotion_speed_meters_per_second.is_finite()
            || locomotion_speed_meters_per_second < 0.0
        {
            return Err(PlayerCharacterModelError::InvalidLocomotionSpeed(
                locomotion_speed_meters_per_second,
            ));
        }

        let desired = if locomotion_speed_meters_per_second > MOVEMENT_EPSILON {
            LocomotionState::Moving
        } else {
            LocomotionState::Idle
        };
        if desired != self.target_state() {
            self.next = Some(desired);
            self.blend_elapsed_seconds = 0.0;
        }

        self.last_locomotion_speed_meters_per_second = locomotion_speed_meters_per_second;
        self.last_walk_run_blend_weight =
            walk_run_blend_weight(locomotion_speed_meters_per_second, self.tuning);
        self.last_playback_scale =
            playback_scale_for_speed(locomotion_speed_meters_per_second, self.tuning);
        self.idle_time_seconds += delta_seconds * self.tuning.idle_playback_scale;
        self.walk_time_seconds += delta_seconds * self.tuning.walk_playback_scale;
        self.run_time_seconds += delta_seconds * self.tuning.run_playback_scale;

        let Some(next) = self.next else {
            return self
                .sample_state_pose(
                    self.active,
                    base_transforms,
                    locomotion_speed_meters_per_second,
                )
                .map_err(PlayerCharacterModelError::ModelAsset);
        };

        self.blend_elapsed_seconds += delta_seconds;
        let blend_weight = self.blend_weight();
        let from_pose = self
            .sample_state_pose(
                self.active,
                base_transforms,
                locomotion_speed_meters_per_second,
            )
            .map_err(PlayerCharacterModelError::ModelAsset)?;
        let to_pose = self
            .sample_state_pose(next, base_transforms, locomotion_speed_meters_per_second)
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
        let active_clip =
            self.state_clip(self.active, self.last_locomotion_speed_meters_per_second);

        PlayerCharacterAnimationSnapshot {
            runtime: "rust",
            active_clip_name: self
                .state_clip_name(self.active, self.last_locomotion_speed_meters_per_second),
            next_clip_name: self.next.map(|clip| {
                self.state_clip_name(clip, self.last_locomotion_speed_meters_per_second)
            }),
            time_seconds: active_clip.wrapped_time(self.state_clip_time_seconds(
                self.active,
                self.last_locomotion_speed_meters_per_second,
            )),
            duration_seconds: active_clip.duration_seconds,
            blend_weight: self.blend_weight(),
            walk_run_blend_weight: self.last_walk_run_blend_weight,
            playback_scale: self.last_playback_scale,
            locomotion_speed_meters_per_second: self.last_locomotion_speed_meters_per_second,
        }
    }

    /// Returns the active state, or an in-flight transition target.
    fn target_state(&self) -> LocomotionState {
        self.next.unwrap_or(self.active)
    }

    /// Samples one controller state over the base model pose.
    fn sample_state_pose(
        &self,
        state: LocomotionState,
        base_transforms: &[ModelNodeTransform],
        locomotion_speed_meters_per_second: f32,
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        match state {
            LocomotionState::Idle => self
                .idle_clip
                .sample_transforms(base_transforms, self.idle_time_seconds),
            LocomotionState::Moving => {
                let walk_pose = self
                    .walk_clip
                    .sample_transforms(base_transforms, self.walk_time_seconds)?;
                let run_pose = self
                    .run_clip
                    .sample_transforms(base_transforms, self.run_time_seconds)?;
                blend_node_transforms(
                    &walk_pose,
                    &run_pose,
                    walk_run_blend_weight(locomotion_speed_meters_per_second, self.tuning),
                )
            }
        }
    }

    /// Returns the dominant debug clip for one controller state.
    fn state_clip(
        &self,
        state: LocomotionState,
        locomotion_speed_meters_per_second: f32,
    ) -> &ModelAnimationClip {
        match state {
            LocomotionState::Idle => &self.idle_clip,
            LocomotionState::Moving => {
                match dominant_movement_clip(locomotion_speed_meters_per_second, self.tuning) {
                    MovementClip::Walk => &self.walk_clip,
                    MovementClip::Run => &self.run_clip,
                }
            }
        }
    }

    /// Returns the dominant debug clock for one controller state.
    fn state_clip_time_seconds(
        &self,
        state: LocomotionState,
        locomotion_speed_meters_per_second: f32,
    ) -> f32 {
        match state {
            LocomotionState::Idle => self.idle_time_seconds,
            LocomotionState::Moving => {
                match dominant_movement_clip(locomotion_speed_meters_per_second, self.tuning) {
                    MovementClip::Walk => self.walk_time_seconds,
                    MovementClip::Run => self.run_time_seconds,
                }
            }
        }
    }

    /// Returns a stable debug name for one controller state.
    fn state_clip_name(
        &self,
        state: LocomotionState,
        locomotion_speed_meters_per_second: f32,
    ) -> String {
        self.state_clip(state, locomotion_speed_meters_per_second)
            .name
            .clone()
            .unwrap_or_else(|| match state {
                LocomotionState::Idle => "idle".to_string(),
                LocomotionState::Moving => {
                    match dominant_movement_clip(locomotion_speed_meters_per_second, self.tuning) {
                        MovementClip::Walk => "walk".to_string(),
                        MovementClip::Run => "run".to_string(),
                    }
                }
            })
    }

    /// Returns the configured locomotion tuning.
    pub fn tuning(&self) -> PlayerCharacterLocomotionTuning {
        self.tuning
    }

    /// Replaces numeric locomotion tuning after validating all values.
    pub fn set_tuning(
        &mut self,
        tuning: PlayerCharacterLocomotionTuning,
    ) -> Result<(), PlayerCharacterModelError> {
        validate_locomotion_tuning(tuning)?;
        self.tuning = tuning;
        self.last_walk_run_blend_weight =
            walk_run_blend_weight(self.last_locomotion_speed_meters_per_second, self.tuning);
        self.last_playback_scale =
            playback_scale_for_speed(self.last_locomotion_speed_meters_per_second, self.tuning);
        Ok(())
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
            Self::InvalidLocomotionSpeed(speed) => write!(
                formatter,
                "glTF player locomotion speed was invalid: {speed}"
            ),
            Self::InvalidLocomotionTuning(field, value) => write!(
                formatter,
                "glTF player locomotion tuning field '{field}' was invalid: {value}"
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

/// Collects every primitive that is attached to a skinned mesh node.
fn skinned_primitive_parts(
    model: &ModelAsset,
) -> Result<Vec<PlayerCharacterPart>, PlayerCharacterModelError> {
    if model.primitives.is_empty() {
        return Err(PlayerCharacterModelError::RenderAsset(
            ModelRenderAssetError::MissingPrimitive,
        ));
    }

    let mut parts = Vec::new();
    for primitive in &model.primitives {
        let Some((node_index, node)) = model
            .nodes
            .iter()
            .enumerate()
            .find(|(_, node)| node.mesh == Some(primitive.mesh_index) && node.skin.is_some())
        else {
            continue;
        };
        let skin_index = node.skin.expect("node skin was checked above");
        let material = primitive
            .material
            .and_then(|material_index| model.materials.get(material_index));
        parts.push(PlayerCharacterPart {
            primitive: primitive.clone(),
            skin_index,
            material_packet: model_material_packet(material)?,
            mesh_node_index: node_index,
        });
    }

    if !parts.is_empty() {
        return Ok(parts);
    }

    let primitive = model
        .primitives
        .first()
        .ok_or(PlayerCharacterModelError::RenderAsset(
            ModelRenderAssetError::MissingPrimitive,
        ))?;
    let Some(node_index) = model
        .nodes
        .iter()
        .position(|node| node.mesh == Some(primitive.mesh_index))
    else {
        return Err(PlayerCharacterModelError::RenderAsset(
            ModelRenderAssetError::MissingMeshNode {
                mesh_index: primitive.mesh_index,
            },
        ));
    };

    Err(PlayerCharacterModelError::RenderAsset(
        ModelRenderAssetError::MissingSkinForNode { node_index },
    ))
}

/// Returns the movement blend from walk to run for a controller speed.
fn walk_run_blend_weight(
    locomotion_speed_meters_per_second: f32,
    tuning: PlayerCharacterLocomotionTuning,
) -> f32 {
    if locomotion_speed_meters_per_second <= MOVEMENT_EPSILON {
        return 0.0;
    }

    let range = tuning.run_speed_meters_per_second - tuning.walk_speed_meters_per_second;
    ((locomotion_speed_meters_per_second - tuning.walk_speed_meters_per_second) / range)
        .clamp(0.0, 1.0)
}

/// Returns the playback scale that best describes the current locomotion speed.
fn playback_scale_for_speed(
    locomotion_speed_meters_per_second: f32,
    tuning: PlayerCharacterLocomotionTuning,
) -> f32 {
    if locomotion_speed_meters_per_second <= MOVEMENT_EPSILON {
        return tuning.idle_playback_scale;
    }

    let run_weight = walk_run_blend_weight(locomotion_speed_meters_per_second, tuning);
    tuning.walk_playback_scale * (1.0 - run_weight) + tuning.run_playback_scale * run_weight
}

/// Returns the dominant named movement clip for debug snapshots.
fn dominant_movement_clip(
    locomotion_speed_meters_per_second: f32,
    tuning: PlayerCharacterLocomotionTuning,
) -> MovementClip {
    if walk_run_blend_weight(locomotion_speed_meters_per_second, tuning) >= 0.5 {
        MovementClip::Run
    } else {
        MovementClip::Walk
    }
}

/// Validates numeric locomotion tuning before it can affect animation state.
fn validate_locomotion_tuning(
    tuning: PlayerCharacterLocomotionTuning,
) -> Result<(), PlayerCharacterModelError> {
    let positive_fields = [
        (
            "walkSpeedMetersPerSecond",
            tuning.walk_speed_meters_per_second,
        ),
        (
            "runSpeedMetersPerSecond",
            tuning.run_speed_meters_per_second,
        ),
        ("idlePlaybackScale", tuning.idle_playback_scale),
        ("walkPlaybackScale", tuning.walk_playback_scale),
        ("runPlaybackScale", tuning.run_playback_scale),
    ];
    for (field, value) in positive_fields {
        if !value.is_finite() || value <= 0.0 {
            return Err(PlayerCharacterModelError::InvalidLocomotionTuning(
                field, value,
            ));
        }
    }
    if tuning.run_speed_meters_per_second <= tuning.walk_speed_meters_per_second {
        return Err(PlayerCharacterModelError::InvalidLocomotionTuning(
            "runSpeedMetersPerSecond",
            tuning.run_speed_meters_per_second,
        ));
    }

    Ok(())
}

/// Builds the fallback material packet for the current model pipeline.
fn model_material_packet(
    material: Option<&ModelMaterial>,
) -> Result<[f32; MATERIAL_PACKET_FLOATS], PlayerCharacterModelError> {
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
    .map_err(PlayerCharacterModelError::MaterialPacket)
}
