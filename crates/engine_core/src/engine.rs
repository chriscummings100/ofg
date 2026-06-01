use crate::math::{Quat, Vec3};
use crate::player::{
    speed_multiplier, yaw_pitch_forward, yaw_right, EyeTransform, PlayerConfig,
    PlayerControllerState, PlayerMode, PlayerMovementIntent, PlayerRig,
};
use crate::world::{LocalTransform, World, WorldError};
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
    World(WorldError),
}

impl From<WorldError> for EngineError {
    fn from(error: WorldError) -> Self {
        Self::World(error)
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
    world: World,
    tick: u64,
    elapsed_seconds: f64,
    player_controller: Option<PlayerControllerState>,
}

impl Engine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn world(&self) -> &World {
        &self.world
    }

    pub fn world_mut(&mut self) -> &mut World {
        &mut self.world
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
            entity_count: self.world.entity_count(),
        }
    }

    pub fn create_player(&mut self, position: Vec3) -> PlayerRig {
        let player_entity = self.world.create_entity();
        let camera_entity = self.world.create_entity();
        let rig = PlayerRig {
            player_entity,
            camera_entity,
        };
        self.player_controller = Some(PlayerControllerState {
            rig,
            mode: PlayerMode::FirstPerson,
            yaw: 0.0,
            pitch: 0.0,
            debug_position: Vec3::new(position.x, position.y + 12.0, position.z),
            debug_yaw: 0.0,
            debug_pitch: -0.35,
            intent: PlayerMovementIntent::default(),
            config: PlayerConfig::default(),
        });
        self.world
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
        self.player_controller.map(|controller| controller.rig)
    }

    pub fn player_mode(&self) -> Result<PlayerMode, EngineError> {
        Ok(self.player_controller()?.mode)
    }

    pub fn set_player_mode(&mut self, mode: PlayerMode) -> Result<(), EngineError> {
        let mut controller = self.player_controller()?;
        controller.mode = mode;
        self.player_controller = Some(controller);
        self.sync_player_camera()
    }

    pub fn set_player_mode_code(&mut self, mode: u32) -> Result<(), EngineError> {
        let mode = PlayerMode::from_code(mode).ok_or(EngineError::InvalidPlayerMode(mode))?;

        self.set_player_mode(mode)
    }

    pub fn toggle_player_mode(&mut self) -> Result<PlayerMode, EngineError> {
        let next_mode = match self.player_mode()? {
            PlayerMode::FirstPerson => PlayerMode::DebugFly,
            PlayerMode::DebugFly => PlayerMode::FirstPerson,
        };
        self.set_player_mode(next_mode)?;

        Ok(next_mode)
    }

    pub fn set_player_movement_intent(
        &mut self,
        intent: PlayerMovementIntent,
    ) -> Result<(), EngineError> {
        let mut controller = self.player_controller()?;
        controller.intent = intent;
        self.player_controller = Some(controller);

        Ok(())
    }

    pub fn set_player_position(&mut self, position: Vec3) -> Result<(), EngineError> {
        let controller = self.player_controller()?;
        self.world.set_local_transform(
            controller.rig.player_entity,
            LocalTransform {
                translation: position,
                rotation: Quat::from_yaw(controller.yaw),
                scale: Vec3::ONE,
            },
        )?;
        self.sync_player_camera()
    }

    pub fn set_debug_camera(
        &mut self,
        position: Vec3,
        yaw: f32,
        pitch: f32,
    ) -> Result<(), EngineError> {
        let mut controller = self.player_controller()?;
        controller.mode = PlayerMode::DebugFly;
        controller.debug_position = position;
        controller.debug_yaw = yaw;
        controller.debug_pitch =
            pitch.clamp(-controller.config.max_pitch, controller.config.max_pitch);
        self.player_controller = Some(controller);
        self.sync_player_camera()
    }

    pub fn player_position(&self) -> Result<Vec3, EngineError> {
        let controller = self.player_controller()?;

        Ok(self
            .world
            .world_transform(controller.rig.player_entity)?
            .translation)
    }

    pub fn player_eye_transform(&self) -> Result<EyeTransform, EngineError> {
        self.player_eye_transform_for(self.player_controller()?)
    }

    pub fn update_player(
        &mut self,
        delta_seconds: f32,
        terrain_height: Option<f32>,
    ) -> Result<EyeTransform, EngineError> {
        validate_delta_seconds(delta_seconds)?;
        let mut controller = self.player_controller()?;

        match controller.mode {
            PlayerMode::FirstPerson => {
                controller.yaw -=
                    controller.intent.look_delta_x * controller.config.look_sensitivity;
                controller.pitch = (controller.pitch
                    - controller.intent.look_delta_y * controller.config.look_sensitivity)
                    .clamp(-controller.config.max_pitch, controller.config.max_pitch);
                let current_position = self
                    .world
                    .local_transform(controller.rig.player_entity)?
                    .translation;
                let planar_move = yaw_pitch_forward(controller.yaw, 0.0)
                    .scale(controller.intent.forward)
                    .add(yaw_right(controller.yaw).scale(controller.intent.right))
                    .normalize();
                let movement = planar_move.scale(
                    controller.config.move_speed
                        * speed_multiplier(controller.intent)
                        * delta_seconds,
                );
                let next_position = current_position.add(movement);
                let grounded_position = Vec3::new(
                    next_position.x,
                    terrain_height.unwrap_or(next_position.y),
                    next_position.z,
                );
                self.world.set_local_transform(
                    controller.rig.player_entity,
                    LocalTransform {
                        translation: grounded_position,
                        rotation: Quat::from_yaw(controller.yaw),
                        scale: Vec3::ONE,
                    },
                )?;
            }
            PlayerMode::DebugFly => {
                controller.debug_yaw -=
                    controller.intent.look_delta_x * controller.config.look_sensitivity;
                controller.debug_pitch = (controller.debug_pitch
                    - controller.intent.look_delta_y * controller.config.look_sensitivity)
                    .clamp(-controller.config.max_pitch, controller.config.max_pitch);
                let movement = yaw_pitch_forward(controller.debug_yaw, controller.debug_pitch)
                    .scale(controller.intent.forward)
                    .add(yaw_right(controller.debug_yaw).scale(controller.intent.right))
                    .add(Vec3::UP.scale(controller.intent.up))
                    .normalize()
                    .scale(
                        controller.config.debug_fly_speed
                            * speed_multiplier(controller.intent)
                            * delta_seconds,
                    );
                controller.debug_position = controller.debug_position.add(movement);
            }
        }

        self.player_controller = Some(controller);
        self.sync_player_camera()?;
        self.player_eye_transform()
    }

    pub fn update(&mut self, input: EngineUpdateInput) -> Result<EngineUpdateSummary, EngineError> {
        validate_delta_seconds(input.delta_seconds)?;

        self.tick = self.tick.wrapping_add(1);
        self.elapsed_seconds += input.delta_seconds as f64;
        self.world.update_world_transforms();

        Ok(EngineUpdateSummary {
            tick: self.tick,
            delta_seconds: input.delta_seconds,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.world.entity_count(),
        })
    }

    fn player_controller(&self) -> Result<PlayerControllerState, EngineError> {
        self.player_controller.ok_or(EngineError::MissingPlayer)
    }

    fn sync_player_camera(&mut self) -> Result<(), EngineError> {
        self.world.update_world_transforms();
        let controller = self.player_controller()?;
        let eye = self.player_eye_transform_for(controller)?;
        self.world.set_local_transform(
            controller.rig.camera_entity,
            LocalTransform {
                translation: eye.position,
                rotation: Quat::from_yaw_pitch(eye.yaw, eye.pitch),
                scale: Vec3::ONE,
            },
        )?;
        self.world.update_world_transforms();

        Ok(())
    }

    fn player_eye_transform_for(
        &self,
        controller: PlayerControllerState,
    ) -> Result<EyeTransform, EngineError> {
        if controller.mode == PlayerMode::DebugFly {
            return Ok(EyeTransform {
                position: controller.debug_position,
                yaw: controller.debug_yaw,
                pitch: controller.debug_pitch,
            });
        }

        let player_position = self
            .world
            .world_transform(controller.rig.player_entity)?
            .translation;

        Ok(EyeTransform {
            position: player_position.add(Vec3::UP.scale(controller.config.eye_height)),
            yaw: controller.yaw,
            pitch: controller.pitch,
        })
    }
}

fn validate_delta_seconds(delta_seconds: f32) -> Result<(), EngineError> {
    if !delta_seconds.is_finite() || delta_seconds < 0.0 {
        return Err(EngineError::InvalidDeltaSeconds(delta_seconds));
    }

    Ok(())
}
