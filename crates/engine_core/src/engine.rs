use crate::math::{Quat, Vec3};
use crate::player::{
    speed_multiplier, yaw_pitch_forward, yaw_right, EyeTransform, PlayerMode, PlayerMovementIntent,
    PlayerRig,
};
use crate::render_packet::RenderSnapshot;
use crate::scene::{EntityId, LocalTransform, Scene, SceneError};
use crate::scene_components::{CameraComponent, MeshRendererComponent, PlayerComponent};
use crate::scene_resources::{DEBUG_PLAYER_MARKER_MATERIAL_LABEL, DEBUG_PLAYER_MARKER_MESH_LABEL};
use crate::sky::{sky_state_at_elapsed_seconds, SkyRenderState};
use crate::ENGINE_CORE_VERSION;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineUpdateInput {
    pub delta_seconds: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineUpdateSummary {
    pub tick: u64,
    pub delta_seconds: f32,
    pub elapsed_seconds: f64,
    pub entity_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub enum EngineError {
    InvalidDeltaSeconds(f32),
    InvalidPlayerMode(u32),
    MissingPlayer,
    Scene(SceneError),
}

impl From<SceneError> for EngineError {
    fn from(error: SceneError) -> Self {
        Self::Scene(error)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineDebugSnapshot {
    pub version: u32,
    pub tick: u64,
    pub elapsed_seconds: f64,
    pub entity_count: usize,
}

#[derive(Default)]
pub struct Engine {
    scene: Scene,
    tick: u64,
    elapsed_seconds: f64,
}

impl Engine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn scene(&self) -> &Scene {
        &self.scene
    }

    pub fn scene_mut(&mut self) -> &mut Scene {
        &mut self.scene
    }

    pub fn tick(&self) -> u64 {
        self.tick
    }

    pub fn elapsed_seconds(&self) -> f64 {
        self.elapsed_seconds
    }

    pub fn debug_snapshot(&self) -> EngineDebugSnapshot {
        EngineDebugSnapshot {
            version: ENGINE_CORE_VERSION,
            tick: self.tick,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.scene.entity_count(),
        }
    }

    pub fn sky_render_state(&self) -> SkyRenderState {
        sky_state_at_elapsed_seconds(self.elapsed_seconds)
    }

    pub fn create_player(&mut self, position: Vec3) -> PlayerRig {
        if let Some(previous_player) = self.scene.player_id() {
            if let Ok(mut previous_player) = self.scene.entity_mut(previous_player) {
                if let Some(renderer) = previous_player.mesh_renderer_mut() {
                    renderer.visible = false;
                }
            }
        }

        let player_entity = self.scene.create_entity();
        let camera_entity = self.scene.create_entity();
        let rig = PlayerRig {
            player_entity,
            camera_entity,
        };
        let mut player = PlayerComponent::new(camera_entity);
        player.debug_position = Vec3::new(position.x, position.y + 12.0, position.z);
        let marker_mesh = self
            .scene
            .resources_mut()
            .register_mesh(DEBUG_PLAYER_MARKER_MESH_LABEL);
        let marker_material = self
            .scene
            .resources_mut()
            .register_material(DEBUG_PLAYER_MARKER_MATERIAL_LABEL);

        {
            let mut player_entity_ref = self
                .scene
                .entity_mut(player_entity)
                .expect("newly-created player entity should be valid");
            player_entity_ref.add_player(player);
            player_entity_ref.add_mesh_renderer(MeshRendererComponent {
                mesh: marker_mesh,
                material: marker_material,
                visible: false,
            });
        }
        {
            let mut camera_entity_ref = self
                .scene
                .entity_mut(camera_entity)
                .expect("newly-created camera entity should be valid");
            camera_entity_ref.add_camera(CameraComponent::default());
        }
        self.scene
            .set_player(Some(player_entity))
            .expect("newly-created player entity should be valid");
        self.scene
            .set_active_camera(Some(camera_entity))
            .expect("newly-created camera entity should be valid");
        self.scene
            .set_local_transform(
                player_entity,
                LocalTransform {
                    translation: position,
                    rotation: Quat::IDENTITY,
                    scale: Vec3::ONE,
                },
            )
            .expect("newly-created player entity should be valid");
        self.sync_player_camera()
            .expect("newly-created camera entity should be valid");

        rig
    }

    pub fn player_rig(&self) -> Option<PlayerRig> {
        let (player_entity, player) = self.player_component().ok()?;

        Some(PlayerRig {
            player_entity,
            camera_entity: player.camera_entity,
        })
    }

    pub fn player_mode(&self) -> Result<PlayerMode, EngineError> {
        Ok(self.player_component()?.1.mode)
    }

    pub fn set_player_mode(&mut self, mode: PlayerMode) -> Result<(), EngineError> {
        let (player_entity, mut player) = self.player_component()?;
        let previous_mode = player.mode;
        player.mode = mode;
        if mode == PlayerMode::ThirdPerson && previous_mode != PlayerMode::ThirdPerson {
            let position = self.scene.world_transform(player_entity)?.translation;
            reset_third_person_camera(&mut player, position);
        } else if previous_mode == PlayerMode::ThirdPerson && mode != PlayerMode::ThirdPerson {
            player.third_person_camera_initialized = false;
        }
        self.set_player_component(player_entity, player)?;
        self.sync_player_camera()
    }

    pub fn set_player_mode_code(&mut self, mode: u32) -> Result<(), EngineError> {
        let mode = PlayerMode::from_code(mode).ok_or(EngineError::InvalidPlayerMode(mode))?;

        self.set_player_mode(mode)
    }

    pub fn toggle_player_mode(&mut self) -> Result<PlayerMode, EngineError> {
        let next_mode = match self.player_mode()? {
            PlayerMode::FirstPerson => PlayerMode::ThirdPerson,
            PlayerMode::ThirdPerson => PlayerMode::DebugFly,
            PlayerMode::DebugFly => PlayerMode::FirstPerson,
        };
        self.set_player_mode(next_mode)?;

        Ok(next_mode)
    }

    pub fn set_player_movement_intent(
        &mut self,
        intent: PlayerMovementIntent,
    ) -> Result<(), EngineError> {
        let (player_entity, mut player) = self.player_component()?;
        player.intent = intent;
        self.set_player_component(player_entity, player)
    }

    pub fn set_player_position(&mut self, position: Vec3) -> Result<(), EngineError> {
        let (player_entity, mut player) = self.player_component()?;
        self.scene.set_local_transform(
            player_entity,
            LocalTransform {
                translation: position,
                rotation: Quat::from_yaw(player.yaw),
                scale: Vec3::ONE,
            },
        )?;
        if player.mode == PlayerMode::ThirdPerson {
            reset_third_person_camera(&mut player, position);
            self.set_player_component(player_entity, player)?;
        }
        self.sync_player_camera()
    }

    pub fn set_player_view(&mut self, yaw: f32, pitch: f32) -> Result<(), EngineError> {
        let (player_entity, mut player) = self.player_component()?;
        player.yaw = yaw;
        player.pitch = pitch.clamp(-player.config.max_pitch, player.config.max_pitch);
        self.set_player_component(player_entity, player)?;
        self.sync_player_camera()
    }

    pub fn set_debug_camera(
        &mut self,
        position: Vec3,
        yaw: f32,
        pitch: f32,
    ) -> Result<(), EngineError> {
        let (player_entity, mut player) = self.player_component()?;
        player.mode = PlayerMode::DebugFly;
        player.debug_position = position;
        player.debug_yaw = yaw;
        player.debug_pitch = pitch.clamp(-player.config.max_pitch, player.config.max_pitch);
        self.set_player_component(player_entity, player)?;
        self.sync_player_camera()
    }

    pub fn player_position(&self) -> Result<Vec3, EngineError> {
        let (player_entity, _) = self.player_component()?;

        Ok(self.scene.world_transform(player_entity)?.translation)
    }

    pub fn player_eye_transform(&self) -> Result<EyeTransform, EngineError> {
        let (player_entity, player) = self.player_component()?;

        self.player_eye_transform_for(player_entity, player)
    }

    pub fn render_snapshot(&self) -> Result<RenderSnapshot, EngineError> {
        let (player_entity, player) = self.player_component()?;
        let eye = self.player_eye_transform_for(player_entity, player)?;

        Ok(RenderSnapshot::from_player_view_at_time(
            eye.position,
            eye.yaw,
            eye.pitch,
            self.elapsed_seconds,
        ))
    }

    pub fn render_mesh_items(
        &self,
    ) -> Result<Vec<crate::render_packet::RenderMeshItemPacket>, EngineError> {
        let mut items = Vec::new();

        for entity_id in self.scene.entity_ids() {
            let entity = self.scene.entity(entity_id)?;
            let Some(renderer) = entity.mesh_renderer() else {
                continue;
            };
            if !renderer.visible {
                continue;
            }

            items.push(crate::render_packet::RenderMeshItemPacket {
                entity: entity_id,
                mesh: renderer.mesh,
                material: renderer.material,
                world_matrix: entity.world_transform().to_matrix(),
            });
        }

        Ok(items)
    }

    pub fn preview_player_position(&self, delta_seconds: f32) -> Result<Vec3, EngineError> {
        validate_delta_seconds(delta_seconds)?;
        let (player_entity, player) = self.player_component()?;

        match player.mode {
            PlayerMode::FirstPerson | PlayerMode::ThirdPerson => self
                .preview_grounded_player_position_at_yaw(
                    player_entity,
                    player,
                    player.yaw - player.intent.look_delta_x * player.config.look_sensitivity,
                    delta_seconds,
                ),
            PlayerMode::DebugFly => Ok(player.debug_position.add(debug_fly_movement(
                player,
                player.debug_yaw - player.intent.look_delta_x * player.config.look_sensitivity,
                (player.debug_pitch - player.intent.look_delta_y * player.config.look_sensitivity)
                    .clamp(-player.config.max_pitch, player.config.max_pitch),
                delta_seconds,
            ))),
        }
    }

    pub fn preview_third_person_camera_goal(
        &self,
        delta_seconds: f32,
        terrain_height: Option<f32>,
    ) -> Result<Option<Vec3>, EngineError> {
        validate_delta_seconds(delta_seconds)?;
        let (player_entity, mut player) = self.player_component()?;
        if player.mode != PlayerMode::ThirdPerson {
            return Ok(None);
        }

        player.yaw -= player.intent.look_delta_x * player.config.look_sensitivity;
        player.pitch = (player.pitch - player.intent.look_delta_y * player.config.look_sensitivity)
            .clamp(-player.config.max_pitch, player.config.max_pitch);
        let next_position = self.preview_grounded_player_position_at_yaw(
            player_entity,
            player,
            player.yaw,
            delta_seconds,
        )?;
        let grounded_position = Vec3::new(
            next_position.x,
            terrain_height.unwrap_or(next_position.y),
            next_position.z,
        );

        Ok(Some(third_person_camera_goal(
            grounded_position,
            player,
            grounded_position.y,
        )))
    }

    pub fn update_player(
        &mut self,
        delta_seconds: f32,
        terrain_height: Option<f32>,
        third_person_camera_ground_height: Option<f32>,
    ) -> Result<EyeTransform, EngineError> {
        validate_delta_seconds(delta_seconds)?;
        let (player_entity, mut player) = self.player_component()?;

        match player.mode {
            PlayerMode::FirstPerson | PlayerMode::ThirdPerson => {
                player.yaw -= player.intent.look_delta_x * player.config.look_sensitivity;
                player.pitch = (player.pitch
                    - player.intent.look_delta_y * player.config.look_sensitivity)
                    .clamp(-player.config.max_pitch, player.config.max_pitch);
                let next_position = self.preview_grounded_player_position_at_yaw(
                    player_entity,
                    player,
                    player.yaw,
                    delta_seconds,
                )?;
                let grounded_position = Vec3::new(
                    next_position.x,
                    terrain_height.unwrap_or(next_position.y),
                    next_position.z,
                );
                if player.mode == PlayerMode::ThirdPerson {
                    update_third_person_camera(
                        &mut player,
                        grounded_position,
                        third_person_camera_ground_height
                            .or(terrain_height)
                            .unwrap_or(grounded_position.y),
                    );
                }
                self.scene.set_local_transform(
                    player_entity,
                    LocalTransform {
                        translation: grounded_position,
                        rotation: Quat::from_yaw(player.yaw),
                        scale: Vec3::ONE,
                    },
                )?;
            }
            PlayerMode::DebugFly => {
                player.debug_yaw -= player.intent.look_delta_x * player.config.look_sensitivity;
                player.debug_pitch = (player.debug_pitch
                    - player.intent.look_delta_y * player.config.look_sensitivity)
                    .clamp(-player.config.max_pitch, player.config.max_pitch);
                player.debug_position = player.debug_position.add(debug_fly_movement(
                    player,
                    player.debug_yaw,
                    player.debug_pitch,
                    delta_seconds,
                ));
            }
        }

        self.set_player_component(player_entity, player)?;
        self.sync_player_camera()?;
        self.player_eye_transform()
    }

    pub fn update(&mut self, input: EngineUpdateInput) -> Result<EngineUpdateSummary, EngineError> {
        validate_delta_seconds(input.delta_seconds)?;

        self.tick = self.tick.wrapping_add(1);
        self.elapsed_seconds += input.delta_seconds as f64;
        self.scene.update_world_transforms();

        Ok(EngineUpdateSummary {
            tick: self.tick,
            delta_seconds: input.delta_seconds,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.scene.entity_count(),
        })
    }

    fn player_component(&self) -> Result<(EntityId, PlayerComponent), EngineError> {
        let player_entity = self.scene.player_id().ok_or(EngineError::MissingPlayer)?;
        let player = self
            .scene
            .entity(player_entity)?
            .player()
            .copied()
            .ok_or(EngineError::MissingPlayer)?;

        Ok((player_entity, player))
    }

    fn set_player_component(
        &mut self,
        player_entity: EntityId,
        player: PlayerComponent,
    ) -> Result<(), EngineError> {
        let mut entity = self.scene.entity_mut(player_entity)?;
        let component = entity.player_mut().ok_or(EngineError::MissingPlayer)?;
        *component = player;
        if let Some(renderer) = entity.mesh_renderer_mut() {
            renderer.visible = player.mode == PlayerMode::DebugFly;
        }
        Ok(())
    }

    fn preview_grounded_player_position_at_yaw(
        &self,
        player_entity: EntityId,
        player: PlayerComponent,
        yaw: f32,
        delta_seconds: f32,
    ) -> Result<Vec3, EngineError> {
        let current_position = self.scene.local_transform(player_entity)?.translation;
        let movement = planar_movement(player, yaw, delta_seconds);

        Ok(current_position.add(movement))
    }

    fn sync_player_camera(&mut self) -> Result<(), EngineError> {
        self.scene.update_world_transforms();
        let (player_entity, player) = self.player_component()?;
        let eye = self.player_eye_transform_for(player_entity, player)?;
        self.scene.set_local_transform(
            player.camera_entity,
            LocalTransform {
                translation: eye.position,
                rotation: Quat::from_yaw_pitch(eye.yaw, eye.pitch),
                scale: Vec3::ONE,
            },
        )?;
        self.scene.update_world_transforms();

        Ok(())
    }

    fn player_eye_transform_for(
        &self,
        player_entity: EntityId,
        player: PlayerComponent,
    ) -> Result<EyeTransform, EngineError> {
        if player.mode == PlayerMode::DebugFly {
            return Ok(EyeTransform {
                position: player.debug_position,
                yaw: player.debug_yaw,
                pitch: player.debug_pitch,
            });
        }

        let player_position = self.scene.world_transform(player_entity)?.translation;

        if player.mode == PlayerMode::ThirdPerson {
            let head_target = third_person_camera_target(player_position, player);
            let camera_position = if player.third_person_camera_initialized {
                player.third_person_camera_position
            } else {
                third_person_camera_goal(player_position, player, player_position.y)
            };
            let (yaw, pitch) = yaw_pitch_to_target(camera_position, head_target);
            return Ok(EyeTransform {
                position: camera_position,
                yaw,
                pitch,
            });
        }

        Ok(EyeTransform {
            position: player_position.add(Vec3::UP.scale(player.config.eye_height)),
            yaw: player.yaw,
            pitch: player.pitch,
        })
    }
}

fn planar_movement(player: PlayerComponent, yaw: f32, delta_seconds: f32) -> Vec3 {
    yaw_pitch_forward(yaw, 0.0)
        .scale(player.intent.forward)
        .add(yaw_right(yaw).scale(player.intent.right))
        .normalize()
        .scale(player.config.move_speed * speed_multiplier(player.intent) * delta_seconds)
}

fn reset_third_person_camera(player: &mut PlayerComponent, player_position: Vec3) {
    player.third_person_camera_position =
        third_person_camera_goal(player_position, *player, player_position.y);
    player.third_person_camera_initialized = true;
}

fn update_third_person_camera(
    player: &mut PlayerComponent,
    player_position: Vec3,
    ground_height: f32,
) {
    let goal = third_person_camera_goal(player_position, *player, ground_height);
    let next_position = if player.third_person_camera_initialized {
        lerp_vec3(
            player.third_person_camera_position,
            goal,
            player.config.third_person_camera_lerp,
        )
    } else {
        goal
    };
    player.third_person_camera_position = clamp_camera_above_ground(
        next_position,
        ground_height,
        player.config.third_person_camera_min_ground_clearance,
    );
    player.third_person_camera_initialized = true;
}

fn third_person_camera_goal(
    player_position: Vec3,
    player: PlayerComponent,
    ground_height: f32,
) -> Vec3 {
    let horizontal_forward = yaw_pitch_forward(player.yaw, 0.0);
    let pitch_height = -player.pitch.sin() * player.config.third_person_camera_distance;
    let desired = player_position
        .add(Vec3::UP.scale(player.config.third_person_camera_height + pitch_height))
        .add(horizontal_forward.scale(-player.config.third_person_camera_distance));
    let min_y = ground_height + player.config.third_person_camera_min_ground_clearance;

    Vec3::new(desired.x, desired.y.max(min_y), desired.z)
}

fn clamp_camera_above_ground(position: Vec3, ground_height: f32, min_clearance: f32) -> Vec3 {
    Vec3::new(
        position.x,
        position.y.max(ground_height + min_clearance),
        position.z,
    )
}

fn third_person_camera_target(player_position: Vec3, player: PlayerComponent) -> Vec3 {
    player_position.add(Vec3::UP.scale(player.config.third_person_camera_target_height))
}

fn lerp_vec3(from: Vec3, to: Vec3, amount: f32) -> Vec3 {
    let t = amount.clamp(0.0, 1.0);
    from.scale(1.0 - t).add(to.scale(t))
}

fn yaw_pitch_to_target(camera_position: Vec3, target: Vec3) -> (f32, f32) {
    let direction = target.add(camera_position.scale(-1.0));
    let horizontal_length = (direction.x * direction.x + direction.z * direction.z).sqrt();
    if horizontal_length <= f32::EPSILON && direction.y.abs() <= f32::EPSILON {
        return (0.0, 0.0);
    }

    (
        direction.x.atan2(direction.z),
        direction.y.atan2(horizontal_length),
    )
}

fn debug_fly_movement(player: PlayerComponent, yaw: f32, pitch: f32, delta_seconds: f32) -> Vec3 {
    yaw_pitch_forward(yaw, pitch)
        .scale(player.intent.forward)
        .add(yaw_right(yaw).scale(player.intent.right))
        .add(Vec3::UP.scale(player.intent.up))
        .normalize()
        .scale(player.config.debug_fly_speed * speed_multiplier(player.intent) * delta_seconds)
}

fn validate_delta_seconds(delta_seconds: f32) -> Result<(), EngineError> {
    if !delta_seconds.is_finite() || delta_seconds < 0.0 {
        return Err(EngineError::InvalidDeltaSeconds(delta_seconds));
    }

    Ok(())
}
