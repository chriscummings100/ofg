// Browser-free typed components used by the scene graph.
use crate::math::Vec3;
use crate::player::{PlayerConfig, PlayerMode, PlayerMovementIntent};
use crate::render_packet::{
    DEFAULT_CAMERA_FAR_PLANE_METERS, DEFAULT_CAMERA_FOV_Y_RADIANS, DEFAULT_CAMERA_NEAR_PLANE_METERS,
};
use crate::scene::EntityId;
use crate::scene_resources::{MaterialId, MeshId};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CameraComponent {
    pub fov_y_radians: f32,
    pub near_plane: f32,
    pub far_plane: f32,
}

impl Default for CameraComponent {
    fn default() -> Self {
        Self {
            fov_y_radians: DEFAULT_CAMERA_FOV_Y_RADIANS,
            near_plane: DEFAULT_CAMERA_NEAR_PLANE_METERS,
            far_plane: DEFAULT_CAMERA_FAR_PLANE_METERS,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerComponent {
    pub mode: PlayerMode,
    pub yaw: f32,
    pub pitch: f32,
    pub debug_position: Vec3,
    pub debug_yaw: f32,
    pub debug_pitch: f32,
    pub third_person_camera_position: Vec3,
    pub third_person_camera_initialized: bool,
    pub intent: PlayerMovementIntent,
    pub config: PlayerConfig,
    pub camera_entity: EntityId,
}

impl PlayerComponent {
    pub fn new(camera_entity: EntityId) -> Self {
        Self {
            mode: PlayerMode::FirstPerson,
            yaw: 0.0,
            pitch: 0.0,
            debug_position: Vec3::ZERO,
            debug_yaw: 0.0,
            debug_pitch: -0.35,
            third_person_camera_position: Vec3::ZERO,
            third_person_camera_initialized: false,
            intent: PlayerMovementIntent::default(),
            config: PlayerConfig::default(),
            camera_entity,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MeshRendererComponent {
    pub mesh: MeshId,
    pub material: MaterialId,
    pub visible: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TerrainComponent {
    pub seed: u32,
    pub preset: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Components {
    pub camera: Option<CameraComponent>,
    pub player: Option<PlayerComponent>,
    pub mesh_renderer: Option<MeshRendererComponent>,
    pub terrain: Option<TerrainComponent>,
}
