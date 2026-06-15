use crate::math::Vec3;
use crate::player::yaw_pitch_forward;
use crate::scene::EntityId;
use crate::scene_resources::{MaterialId, MeshId};
use crate::sky::{sky_state_at_elapsed_seconds, SkyRenderPacket};

pub const RENDER_SNAPSHOT_FLOAT_COUNT: usize = 31;
pub const RENDER_MESH_ITEM_WORLD_MATRIX_FLOAT_COUNT: usize = 16;
pub const DEFAULT_CAMERA_FOV_Y_RADIANS: f32 = 70.0 * std::f32::consts::PI / 180.0;
pub const DEFAULT_CAMERA_NEAR_PLANE_METERS: f32 = 0.05;
pub const DEFAULT_CAMERA_FAR_PLANE_METERS: f32 = 3_500.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderCameraPacket {
    pub eye: Vec3,
    pub target: Vec3,
    pub yaw: f32,
    pub pitch: f32,
    pub fov_y_radians: f32,
    pub near_plane: f32,
    pub far_plane: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderLightPacket {
    pub direction: Vec3,
    pub color: Vec3,
    pub intensity: f32,
    pub ambient: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderSnapshot {
    pub camera: RenderCameraPacket,
    pub main_light: RenderLightPacket,
    pub sky: SkyRenderPacket,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderMeshItemPacket {
    pub entity: EntityId,
    pub mesh: MeshId,
    pub material: MaterialId,
    pub world_matrix: [f32; RENDER_MESH_ITEM_WORLD_MATRIX_FLOAT_COUNT],
}

impl RenderSnapshot {
    pub fn from_player_view(eye: Vec3, yaw: f32, pitch: f32) -> Self {
        Self::from_player_view_at_time(eye, yaw, pitch, 0.0)
    }

    pub fn from_player_view_at_time(eye: Vec3, yaw: f32, pitch: f32, elapsed_seconds: f64) -> Self {
        let sky_state = sky_state_at_elapsed_seconds(elapsed_seconds);
        Self {
            camera: RenderCameraPacket {
                eye,
                target: eye.add(yaw_pitch_forward(yaw, pitch)),
                yaw,
                pitch,
                fov_y_radians: DEFAULT_CAMERA_FOV_Y_RADIANS,
                near_plane: DEFAULT_CAMERA_NEAR_PLANE_METERS,
                far_plane: DEFAULT_CAMERA_FAR_PLANE_METERS,
            },
            main_light: sky_state.main_light,
            sky: sky_state.sky,
        }
    }

    pub fn write_f32s(self, out: &mut [f32; RENDER_SNAPSHOT_FLOAT_COUNT]) {
        let mut sky = [0.0; crate::SKY_RENDER_PACKET_FLOAT_COUNT];
        self.sky.write_f32s(&mut sky);
        out[0] = self.camera.eye.x;
        out[1] = self.camera.eye.y;
        out[2] = self.camera.eye.z;
        out[3] = self.camera.target.x;
        out[4] = self.camera.target.y;
        out[5] = self.camera.target.z;
        out[6] = self.camera.yaw;
        out[7] = self.camera.pitch;
        out[8] = self.camera.fov_y_radians;
        out[9] = self.camera.near_plane;
        out[10] = self.camera.far_plane;
        out[11] = self.main_light.direction.x;
        out[12] = self.main_light.direction.y;
        out[13] = self.main_light.direction.z;
        out[14] = self.main_light.color.x;
        out[15] = self.main_light.color.y;
        out[16] = self.main_light.color.z;
        out[17] = self.main_light.intensity;
        out[18] = self.main_light.ambient;
        out[19..31].copy_from_slice(&sky);
    }
}
