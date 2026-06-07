// Rust-owned browser game state: terrain/player setup plus render-facing scene
// mesh items for imported GLTF models and the player character.
use engine_core::{
    Engine, EngineError, EngineUpdateInput, EntityId, LocalTransform, MaterialId, MeshId,
    MeshRendererComponent, PlayerMode, PlayerMovementIntent, Quat, TerrainComponent, Vec3,
    RENDER_SNAPSHOT_FLOAT_COUNT,
};
use terrain_core::{height_at, DEFAULT_TERRAIN_PRESET};

use crate::model_animation::ModelAnimationClip;
use crate::model_assets::{ModelAssetError, ModelNodeTransform};

const INITIAL_PLAYER_X: f32 = 0.0;
const INITIAL_PLAYER_Z: f32 = 0.0;
const INITIAL_PLAYER_YAW: f32 = std::f32::consts::PI * 0.18;
const INITIAL_PLAYER_PITCH: f32 = -0.08;
const INITIAL_STATIC_MODEL_X: f32 = 3.0;
const INITIAL_STATIC_MODEL_Z: f32 = 6.0;
const INITIAL_STATIC_MODEL_HEIGHT_OFFSET: f32 = 1.2;
const INITIAL_STATIC_MODEL_SCALE: f32 = 2.0;
const MODEL_FOLLOWS_PLAYER_EPSILON: f32 = 0.001;
const INITIAL_DEBUG_X: f32 = 14.0;
const INITIAL_DEBUG_Z: f32 = 18.0;
const INITIAL_DEBUG_HEIGHT_OFFSET: f32 = 12.0;
const INITIAL_DEBUG_YAW: f32 = std::f32::consts::PI * 1.24;
const INITIAL_DEBUG_PITCH: f32 = -0.48;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserGameInput {
    pub delta_seconds: f32,
    pub forward: f32,
    pub right: f32,
    pub up: f32,
    pub fast: bool,
    pub look_delta_x: f32,
    pub look_delta_y: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub enum BrowserGameStateError {
    Engine(EngineError),
    ModelAnimation(ModelAssetError),
    InvalidTerrainHeight { x: f32, z: f32 },
    InvalidModelSceneScale(f32),
    InvalidModelSceneHeightOffset(f32),
    MissingSceneMeshResource(MeshId),
    MissingSceneMaterialResource(MaterialId),
}

#[derive(Clone, Debug, PartialEq)]
pub struct BrowserSceneMeshItem {
    pub entity: EntityId,
    pub mesh: MeshId,
    pub mesh_label: String,
    pub material: MaterialId,
    pub material_label: String,
    pub world_matrix: [f32; 16],
}

#[derive(Clone, Debug, PartialEq)]
pub struct BrowserModelAnimationSnapshot {
    pub runtime: &'static str,
    pub clip_name: Option<String>,
    pub time_seconds: f32,
    pub duration_seconds: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserPlayerCharacterSceneSnapshot {
    pub runtime: &'static str,
    pub visible: bool,
    pub follows_player: bool,
    pub debug_marker_visible: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserSkySnapshot {
    pub runtime: &'static str,
    pub day_phase: f32,
    pub sun_elevation: f32,
    pub cloud_coverage: f32,
    pub star_intensity: f32,
}

pub struct BrowserGameState {
    engine: Engine,
    terrain_seed: u32,
    terrain_preset: u32,
    static_model_scene: Option<StaticModelSceneConfig>,
    static_model_scene_state: Option<ModelSceneState>,
    static_model_animation_time_seconds: f32,
    player_character_scene: Option<PlayerCharacterSceneConfig>,
    player_character_scene_state: Option<PlayerCharacterSceneState>,
}

#[derive(Clone, Debug, PartialEq)]
struct StaticModelSceneConfig {
    mesh_label: String,
    material_label: String,
    scale: f32,
    height_offset: f32,
    animation: Option<StaticModelAnimationConfig>,
}

#[derive(Clone, Debug, PartialEq)]
struct StaticModelAnimationConfig {
    clip: ModelAnimationClip,
    node_index: usize,
    node_base_transforms: Vec<ModelNodeTransform>,
}

#[derive(Clone, Debug, PartialEq)]
struct PlayerCharacterSceneConfig {
    parts: Vec<PlayerCharacterScenePartConfig>,
    scale: f32,
    height_offset: f32,
}

#[derive(Clone, Debug, PartialEq)]
struct PlayerCharacterScenePartConfig {
    mesh_label: String,
    material_label: String,
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct ModelSceneState {
    root_entity: EntityId,
    mesh_entity: EntityId,
}

#[derive(Clone, Debug, PartialEq)]
struct PlayerCharacterSceneState {
    root_entity: EntityId,
    mesh_entities: Vec<EntityId>,
}

impl BrowserGameState {
    pub fn new() -> Self {
        Self {
            engine: Engine::new(),
            terrain_seed: 0,
            terrain_preset: DEFAULT_TERRAIN_PRESET,
            static_model_scene: None,
            static_model_scene_state: None,
            static_model_animation_time_seconds: 0.0,
            player_character_scene: None,
            player_character_scene_state: None,
        }
    }

    pub fn reset_game(
        &mut self,
        terrain_seed: u32,
        terrain_preset: u32,
    ) -> Result<(), BrowserGameStateError> {
        self.engine = Engine::new();
        self.terrain_seed = terrain_seed;
        self.terrain_preset = terrain_preset;
        self.static_model_scene_state = None;
        self.static_model_animation_time_seconds = 0.0;
        self.player_character_scene_state = None;

        let terrain_entity = self.engine.scene_mut().create_entity();
        {
            let mut entity = self
                .engine
                .scene_mut()
                .entity_mut(terrain_entity)
                .map_err(EngineError::from)?;
            entity.add_terrain(TerrainComponent {
                seed: terrain_seed,
                preset: terrain_preset,
            });
        }
        self.engine
            .scene_mut()
            .set_terrain(Some(terrain_entity))
            .map_err(EngineError::from)?;

        let initial_height = self.terrain_height_at(INITIAL_PLAYER_X, INITIAL_PLAYER_Z)?;
        self.engine.create_player(Vec3::new(
            INITIAL_PLAYER_X,
            initial_height,
            INITIAL_PLAYER_Z,
        ));
        self.remove_debug_player_marker_renderer()?;
        self.engine
            .set_player_view(INITIAL_PLAYER_YAW, INITIAL_PLAYER_PITCH)?;
        self.engine.set_debug_camera(
            Vec3::new(
                INITIAL_DEBUG_X,
                initial_height + INITIAL_DEBUG_HEIGHT_OFFSET,
                INITIAL_DEBUG_Z,
            ),
            INITIAL_DEBUG_YAW,
            INITIAL_DEBUG_PITCH,
        )?;
        self.engine.set_player_mode(PlayerMode::FirstPerson)?;
        self.spawn_configured_static_model()?;
        self.spawn_configured_player_character()?;

        Ok(())
    }

    pub fn configure_static_model_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
    ) -> Result<(), BrowserGameStateError> {
        self.static_model_scene = Some(StaticModelSceneConfig {
            mesh_label: mesh_label.into(),
            material_label: material_label.into(),
            scale: INITIAL_STATIC_MODEL_SCALE,
            height_offset: INITIAL_STATIC_MODEL_HEIGHT_OFFSET,
            animation: None,
        });
        if self.engine.player_rig().is_some() {
            self.replace_configured_static_model()?;
        }

        Ok(())
    }

    pub fn configure_animated_static_model_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
        clip: ModelAnimationClip,
        node_index: usize,
        node_base_transforms: Vec<ModelNodeTransform>,
    ) -> Result<(), BrowserGameStateError> {
        self.configure_scaled_animated_static_model_scene(
            mesh_label,
            material_label,
            INITIAL_STATIC_MODEL_SCALE,
            clip,
            node_index,
            node_base_transforms,
        )
    }

    pub fn configure_scaled_animated_static_model_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
        scale: f32,
        clip: ModelAnimationClip,
        node_index: usize,
        node_base_transforms: Vec<ModelNodeTransform>,
    ) -> Result<(), BrowserGameStateError> {
        self.configure_model_scene(
            mesh_label,
            material_label,
            scale,
            INITIAL_STATIC_MODEL_HEIGHT_OFFSET,
            Some(StaticModelAnimationConfig {
                clip,
                node_index,
                node_base_transforms,
            }),
        )
    }

    pub fn configure_scaled_static_model_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
        scale: f32,
        height_offset: f32,
    ) -> Result<(), BrowserGameStateError> {
        self.configure_model_scene(mesh_label, material_label, scale, height_offset, None)
    }

    pub fn configure_player_character_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
        scale: f32,
        height_offset: f32,
    ) -> Result<(), BrowserGameStateError> {
        self.configure_player_character_scene_parts(
            vec![(mesh_label.into(), material_label.into())],
            scale,
            height_offset,
        )
    }

    pub fn configure_player_character_scene_parts(
        &mut self,
        parts: Vec<(String, String)>,
        scale: f32,
        height_offset: f32,
    ) -> Result<(), BrowserGameStateError> {
        validate_model_scene_transform(scale, height_offset)?;
        let parts = parts
            .into_iter()
            .map(
                |(mesh_label, material_label)| PlayerCharacterScenePartConfig {
                    mesh_label,
                    material_label,
                },
            )
            .collect();
        self.player_character_scene = Some(PlayerCharacterSceneConfig {
            parts,
            scale,
            height_offset,
        });
        if self.engine.player_rig().is_some() {
            self.replace_configured_player_character()?;
        }

        Ok(())
    }

    pub fn tick(&mut self, input: BrowserGameInput) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine
            .set_player_movement_intent(PlayerMovementIntent {
                forward: input.forward,
                right: input.right,
                up: input.up,
                fast: input.fast,
                look_delta_x: input.look_delta_x,
                look_delta_y: input.look_delta_y,
            })?;

        let player_mode = self.player_mode()?;
        let terrain_height = if matches!(
            player_mode,
            PlayerMode::FirstPerson | PlayerMode::ThirdPerson
        ) {
            let preview = self.engine.preview_player_position(input.delta_seconds)?;
            Some(self.terrain_height_at(preview.x, preview.z)?)
        } else {
            None
        };

        self.engine
            .update_player(input.delta_seconds, terrain_height)?;
        self.engine.update(EngineUpdateInput {
            delta_seconds: input.delta_seconds,
        })?;
        self.advance_static_model_animation(input.delta_seconds)?;
        self.sync_player_character_scene()?;

        Ok(())
    }

    pub fn toggle_player_mode(&mut self) -> Result<PlayerMode, BrowserGameStateError> {
        self.ensure_player()?;
        let mode = self.engine.toggle_player_mode()?;
        self.sync_player_character_scene()?;
        Ok(mode)
    }

    pub fn player_mode(&self) -> Result<PlayerMode, BrowserGameStateError> {
        Ok(self.engine.player_mode()?)
    }

    pub fn set_player_mode(&mut self, mode: PlayerMode) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine.set_player_mode(mode)?;
        self.sync_player_character_scene()?;
        Ok(())
    }

    pub fn player_position(&self) -> Result<Vec3, BrowserGameStateError> {
        Ok(self.engine.player_position()?)
    }

    pub fn terrain_seed(&self) -> u32 {
        self.terrain_seed
    }

    pub fn terrain_preset(&self) -> u32 {
        self.terrain_preset
    }

    pub fn set_player_position_xz(
        &mut self,
        x: f32,
        z: f32,
    ) -> Result<Vec3, BrowserGameStateError> {
        self.ensure_player()?;
        let height = self.terrain_height_at(x, z)?;
        let position = Vec3::new(x, height, z);
        self.engine.set_player_position(position)?;
        self.sync_player_character_scene()?;
        Ok(position)
    }

    pub fn set_debug_camera(
        &mut self,
        position: Vec3,
        yaw: f32,
        pitch: f32,
    ) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine.set_debug_camera(position, yaw, pitch)?;
        self.sync_player_character_scene()?;
        Ok(())
    }

    pub fn render_snapshot_values(
        &self,
    ) -> Result<[f32; RENDER_SNAPSHOT_FLOAT_COUNT], BrowserGameStateError> {
        let mut values = [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];
        self.engine.render_snapshot()?.write_f32s(&mut values);
        Ok(values)
    }

    pub fn sky_snapshot(&self) -> BrowserSkySnapshot {
        let sky = self.engine.sky_render_state().sky;
        BrowserSkySnapshot {
            runtime: "rust",
            day_phase: sky.day_phase,
            sun_elevation: sky.sun_elevation,
            cloud_coverage: sky.cloud_coverage,
            star_intensity: sky.star_intensity,
        }
    }

    pub fn render_mesh_items(&self) -> Result<Vec<BrowserSceneMeshItem>, BrowserGameStateError> {
        let items = self.engine.render_mesh_items()?;
        let resources = self.engine.scene().resources();
        let mut browser_items = Vec::with_capacity(items.len());

        for item in items {
            let mesh_label = resources
                .mesh(item.mesh)
                .ok_or(BrowserGameStateError::MissingSceneMeshResource(item.mesh))?
                .label
                .clone();
            let material_label = resources
                .material(item.material)
                .ok_or(BrowserGameStateError::MissingSceneMaterialResource(
                    item.material,
                ))?
                .label
                .clone();

            browser_items.push(BrowserSceneMeshItem {
                entity: item.entity,
                mesh: item.mesh,
                mesh_label,
                material: item.material,
                material_label,
                world_matrix: item.world_matrix,
            });
        }

        Ok(browser_items)
    }

    pub fn model_animation_snapshot(&self) -> Option<BrowserModelAnimationSnapshot> {
        let animation = self.static_model_scene.as_ref()?.animation.as_ref()?;
        self.static_model_scene_state?;

        Some(BrowserModelAnimationSnapshot {
            runtime: "rust",
            clip_name: animation.clip.name.clone(),
            time_seconds: animation
                .clip
                .wrapped_time(self.static_model_animation_time_seconds),
            duration_seconds: animation.clip.duration_seconds,
        })
    }

    pub fn player_character_scene_snapshot(
        &self,
    ) -> Result<Option<BrowserPlayerCharacterSceneSnapshot>, BrowserGameStateError> {
        let Some(state) = self.player_character_scene_state.as_ref() else {
            return Ok(None);
        };
        let Some(mesh_entity) = state.mesh_entities.first().copied() else {
            return Ok(None);
        };
        let visible = self
            .engine
            .scene()
            .entity(mesh_entity)
            .map_err(EngineError::from)?
            .mesh_renderer()
            .map(|renderer| renderer.visible)
            .unwrap_or(false);

        Ok(Some(BrowserPlayerCharacterSceneSnapshot {
            runtime: "rust",
            visible,
            follows_player: self.player_character_follows_player(state)?,
            debug_marker_visible: self.debug_player_marker_visible()?,
        }))
    }

    #[cfg(test)]
    pub(crate) fn terrain_component(&self) -> Option<TerrainComponent> {
        let terrain = self.engine.scene().terrain_id()?;
        self.engine.scene().entity(terrain).ok()?.terrain().copied()
    }

    fn ensure_player(&mut self) -> Result<(), BrowserGameStateError> {
        if self.engine.player_rig().is_some() {
            return Ok(());
        }

        self.reset_game(self.terrain_seed, self.terrain_preset)
    }

    fn remove_debug_player_marker_renderer(&mut self) -> Result<(), BrowserGameStateError> {
        let Some(rig) = self.engine.player_rig() else {
            return Ok(());
        };

        self.engine
            .scene_mut()
            .entity_mut(rig.player_entity)
            .map_err(EngineError::from)?
            .remove_mesh_renderer();
        Ok(())
    }

    fn spawn_configured_static_model(&mut self) -> Result<(), BrowserGameStateError> {
        let Some(config) = self.static_model_scene.clone() else {
            return Ok(());
        };

        let terrain_height =
            self.terrain_height_at(INITIAL_STATIC_MODEL_X, INITIAL_STATIC_MODEL_Z)?;
        let node_transform = config
            .animation
            .as_ref()
            .and_then(|animation| animation.node_base_transforms.get(animation.node_index))
            .copied()
            .unwrap_or_default();
        self.static_model_scene_state = Some(self.spawn_model_scene_item(
            &config.mesh_label,
            &config.material_label,
            Vec3::new(
                INITIAL_STATIC_MODEL_X,
                terrain_height + config.height_offset,
                INITIAL_STATIC_MODEL_Z,
            ),
            config.scale,
            node_transform,
            true,
        )?);
        self.advance_static_model_animation(0.0)
    }

    fn spawn_configured_player_character(&mut self) -> Result<(), BrowserGameStateError> {
        let Some(config) = self.player_character_scene.clone() else {
            return Ok(());
        };
        if config.parts.is_empty() {
            return Ok(());
        }
        let player_position = self.engine.player_position()?;
        self.player_character_scene_state = Some(self.spawn_player_character_scene_items(
            &config.parts,
            Vec3::new(
                player_position.x,
                player_position.y + config.height_offset,
                player_position.z,
            ),
            config.scale,
            ModelNodeTransform::default(),
            false,
        )?);
        self.sync_player_character_scene()
    }

    fn spawn_player_character_scene_items(
        &mut self,
        parts: &[PlayerCharacterScenePartConfig],
        position: Vec3,
        scale: f32,
        node_transform: ModelNodeTransform,
        visible: bool,
    ) -> Result<PlayerCharacterSceneState, BrowserGameStateError> {
        let root_entity = self.engine.scene_mut().create_entity();
        self.engine
            .scene_mut()
            .set_local_transform(
                root_entity,
                LocalTransform {
                    translation: position,
                    rotation: Quat::IDENTITY,
                    scale: Vec3::new(scale, scale, scale),
                },
            )
            .map_err(EngineError::from)?;

        let mut mesh_entities = Vec::with_capacity(parts.len());
        for part in parts {
            let mesh = self
                .engine
                .scene_mut()
                .resources_mut()
                .register_mesh(&part.mesh_label);
            let material = self
                .engine
                .scene_mut()
                .resources_mut()
                .register_material(&part.material_label);
            let mesh_entity = self
                .engine
                .scene_mut()
                .create_child(root_entity)
                .map_err(EngineError::from)?;
            {
                let mut entity_ref = self
                    .engine
                    .scene_mut()
                    .entity_mut(mesh_entity)
                    .map_err(EngineError::from)?;
                entity_ref.add_mesh_renderer(MeshRendererComponent {
                    mesh,
                    material,
                    visible,
                });
            }
            self.engine
                .scene_mut()
                .set_local_transform(mesh_entity, local_transform_from_model(node_transform))
                .map_err(EngineError::from)?;
            mesh_entities.push(mesh_entity);
        }
        self.engine.scene_mut().update_world_transforms();

        Ok(PlayerCharacterSceneState {
            root_entity,
            mesh_entities,
        })
    }

    fn spawn_model_scene_item(
        &mut self,
        mesh_label: &str,
        material_label: &str,
        position: Vec3,
        scale: f32,
        node_transform: ModelNodeTransform,
        visible: bool,
    ) -> Result<ModelSceneState, BrowserGameStateError> {
        let mesh = self
            .engine
            .scene_mut()
            .resources_mut()
            .register_mesh(mesh_label);
        let material = self
            .engine
            .scene_mut()
            .resources_mut()
            .register_material(material_label);
        let root_entity = self.engine.scene_mut().create_entity();
        self.engine
            .scene_mut()
            .set_local_transform(
                root_entity,
                LocalTransform {
                    translation: position,
                    rotation: Quat::IDENTITY,
                    scale: Vec3::new(scale, scale, scale),
                },
            )
            .map_err(EngineError::from)?;
        let mesh_entity = self
            .engine
            .scene_mut()
            .create_child(root_entity)
            .map_err(EngineError::from)?;
        {
            let mut entity_ref = self
                .engine
                .scene_mut()
                .entity_mut(mesh_entity)
                .map_err(EngineError::from)?;
            entity_ref.add_mesh_renderer(MeshRendererComponent {
                mesh,
                material,
                visible,
            });
        }
        self.engine
            .scene_mut()
            .set_local_transform(mesh_entity, local_transform_from_model(node_transform))
            .map_err(EngineError::from)?;
        self.engine.scene_mut().update_world_transforms();

        Ok(ModelSceneState {
            root_entity,
            mesh_entity,
        })
    }

    fn replace_configured_static_model(&mut self) -> Result<(), BrowserGameStateError> {
        if let Some(state) = self.static_model_scene_state.take() {
            self.destroy_model_scene(state)?;
        }

        self.spawn_configured_static_model()
    }

    fn replace_configured_player_character(&mut self) -> Result<(), BrowserGameStateError> {
        if let Some(state) = self.player_character_scene_state.take() {
            self.destroy_player_character_scene(state)?;
        }

        self.spawn_configured_player_character()
    }

    fn destroy_model_scene(&mut self, state: ModelSceneState) -> Result<(), BrowserGameStateError> {
        if self.engine.scene().is_alive(state.root_entity) {
            self.engine
                .scene_mut()
                .destroy_entity(state.root_entity)
                .map_err(EngineError::from)?;
        }

        Ok(())
    }

    fn destroy_player_character_scene(
        &mut self,
        state: PlayerCharacterSceneState,
    ) -> Result<(), BrowserGameStateError> {
        if self.engine.scene().is_alive(state.root_entity) {
            self.engine
                .scene_mut()
                .destroy_entity(state.root_entity)
                .map_err(EngineError::from)?;
        }

        Ok(())
    }

    fn advance_static_model_animation(
        &mut self,
        delta_seconds: f32,
    ) -> Result<(), BrowserGameStateError> {
        let Some(config) = self.static_model_scene.as_ref() else {
            return Ok(());
        };
        let Some(animation) = config.animation.as_ref() else {
            return Ok(());
        };
        let Some(state) = self.static_model_scene_state else {
            return Ok(());
        };

        self.static_model_animation_time_seconds += delta_seconds;
        let transforms = animation
            .clip
            .sample_transforms(
                &animation.node_base_transforms,
                self.static_model_animation_time_seconds,
            )
            .map_err(BrowserGameStateError::ModelAnimation)?;
        let node_transform =
            transforms
                .get(animation.node_index)
                .ok_or(BrowserGameStateError::ModelAnimation(
                    ModelAssetError::InvalidAnimationTargetNode {
                        node_index: animation.node_index,
                    },
                ))?;
        self.engine
            .scene_mut()
            .set_local_transform(
                state.mesh_entity,
                local_transform_from_model(*node_transform),
            )
            .map_err(EngineError::from)?;
        self.engine.scene_mut().update_world_transforms();

        Ok(())
    }

    fn sync_player_character_scene(&mut self) -> Result<(), BrowserGameStateError> {
        let Some(config) = self.player_character_scene.clone() else {
            return Ok(());
        };
        let Some(state) = self.player_character_scene_state.clone() else {
            return Ok(());
        };
        let rig = self.engine.player_rig().ok_or(EngineError::MissingPlayer)?;
        let player_world = self
            .engine
            .scene()
            .world_transform(rig.player_entity)
            .map_err(EngineError::from)?;
        let visible = matches!(
            self.engine.player_mode()?,
            PlayerMode::ThirdPerson | PlayerMode::DebugFly
        );

        self.engine
            .scene_mut()
            .set_local_transform(
                state.root_entity,
                LocalTransform {
                    translation: Vec3::new(
                        player_world.translation.x,
                        player_world.translation.y + config.height_offset,
                        player_world.translation.z,
                    ),
                    rotation: player_world.rotation,
                    scale: Vec3::new(config.scale, config.scale, config.scale),
                },
            )
            .map_err(EngineError::from)?;
        {
            for mesh_entity in state.mesh_entities {
                let mut entity_ref = self
                    .engine
                    .scene_mut()
                    .entity_mut(mesh_entity)
                    .map_err(EngineError::from)?;
                if let Some(renderer) = entity_ref.mesh_renderer_mut() {
                    renderer.visible = visible;
                }
            }
        }
        self.engine.scene_mut().update_world_transforms();

        Ok(())
    }

    fn player_character_follows_player(
        &self,
        state: &PlayerCharacterSceneState,
    ) -> Result<bool, BrowserGameStateError> {
        let Some(config) = self.player_character_scene.as_ref() else {
            return Ok(false);
        };
        let player_position = self.engine.player_position()?;
        let root_position = self
            .engine
            .scene()
            .world_transform(state.root_entity)
            .map_err(EngineError::from)?
            .translation;

        Ok(
            (root_position.x - player_position.x).abs() <= MODEL_FOLLOWS_PLAYER_EPSILON
                && (root_position.y - (player_position.y + config.height_offset)).abs()
                    <= MODEL_FOLLOWS_PLAYER_EPSILON
                && (root_position.z - player_position.z).abs() <= MODEL_FOLLOWS_PLAYER_EPSILON,
        )
    }

    fn debug_player_marker_visible(&self) -> Result<bool, BrowserGameStateError> {
        let Some(rig) = self.engine.player_rig() else {
            return Ok(false);
        };
        let player = self
            .engine
            .scene()
            .entity(rig.player_entity)
            .map_err(EngineError::from)?;

        Ok(player
            .mesh_renderer()
            .map(|renderer| renderer.visible)
            .unwrap_or(false))
    }

    fn terrain_height_at(&self, x: f32, z: f32) -> Result<f32, BrowserGameStateError> {
        let height = height_at(
            self.terrain_seed,
            self.terrain_preset,
            f64::from(x),
            f64::from(z),
        ) as f32;
        if !height.is_finite() {
            return Err(BrowserGameStateError::InvalidTerrainHeight { x, z });
        }

        Ok(height)
    }
}

impl Default for BrowserGameState {
    fn default() -> Self {
        Self::new()
    }
}

impl From<EngineError> for BrowserGameStateError {
    fn from(error: EngineError) -> Self {
        Self::Engine(error)
    }
}

impl std::fmt::Display for BrowserGameStateError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Engine(error) => write!(formatter, "Rust browser game engine error: {error:?}"),
            Self::ModelAnimation(error) => {
                write!(
                    formatter,
                    "Rust browser game model animation error: {error}"
                )
            }
            Self::InvalidTerrainHeight { x, z } => {
                write!(
                    formatter,
                    "Rust browser game terrain height was invalid at ({x}, {z})"
                )
            }
            Self::InvalidModelSceneScale(scale) => {
                write!(
                    formatter,
                    "Rust browser game model scene scale was invalid: {scale}"
                )
            }
            Self::InvalidModelSceneHeightOffset(height_offset) => {
                write!(
                    formatter,
                    "Rust browser game model scene height offset was invalid: {height_offset}"
                )
            }
            Self::MissingSceneMeshResource(mesh) => {
                write!(
                    formatter,
                    "Rust browser game scene mesh {mesh:?} was missing"
                )
            }
            Self::MissingSceneMaterialResource(material) => {
                write!(
                    formatter,
                    "Rust browser game scene material {material:?} was missing"
                )
            }
        }
    }
}

impl BrowserGameState {
    /// Configures the optional imported model scene item.
    fn configure_model_scene(
        &mut self,
        mesh_label: impl Into<String>,
        material_label: impl Into<String>,
        scale: f32,
        height_offset: f32,
        animation: Option<StaticModelAnimationConfig>,
    ) -> Result<(), BrowserGameStateError> {
        validate_model_scene_transform(scale, height_offset)?;

        self.static_model_scene = Some(StaticModelSceneConfig {
            mesh_label: mesh_label.into(),
            material_label: material_label.into(),
            scale,
            height_offset,
            animation,
        });
        self.static_model_animation_time_seconds = 0.0;
        if self.engine.player_rig().is_some() {
            self.replace_configured_static_model()?;
        }

        Ok(())
    }
}

fn validate_model_scene_transform(
    scale: f32,
    height_offset: f32,
) -> Result<(), BrowserGameStateError> {
    if !scale.is_finite() || scale <= 0.0 {
        return Err(BrowserGameStateError::InvalidModelSceneScale(scale));
    }
    if !height_offset.is_finite() {
        return Err(BrowserGameStateError::InvalidModelSceneHeightOffset(
            height_offset,
        ));
    }

    Ok(())
}

pub fn player_mode_code(mode: PlayerMode) -> u32 {
    mode.code()
}

pub fn player_mode_from_code(code: u32) -> Option<PlayerMode> {
    PlayerMode::from_code(code)
}

fn local_transform_from_model(transform: ModelNodeTransform) -> LocalTransform {
    LocalTransform {
        translation: Vec3::new(
            transform.translation[0],
            transform.translation[1],
            transform.translation[2],
        ),
        rotation: Quat::new(
            transform.rotation[0],
            transform.rotation[1],
            transform.rotation[2],
            transform.rotation[3],
        )
        .normalize(),
        scale: Vec3::new(transform.scale[0], transform.scale[1], transform.scale[2]),
    }
}
