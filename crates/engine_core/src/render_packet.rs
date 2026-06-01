use crate::math::Vec3;
use crate::player::{yaw_pitch_forward, PlayerMode};

pub const RENDER_SNAPSHOT_FLOAT_COUNT: usize = 24;

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
pub struct RenderDebugMarkerPacket {
    pub visible: bool,
    pub position: Vec3,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RenderSnapshot {
    pub camera: RenderCameraPacket,
    pub main_light: RenderLightPacket,
    pub player_marker: RenderDebugMarkerPacket,
}

impl RenderSnapshot {
    pub fn from_player_view(
        eye: Vec3,
        yaw: f32,
        pitch: f32,
        player_position: Vec3,
        player_mode: PlayerMode,
    ) -> Self {
        Self {
            camera: RenderCameraPacket {
                eye,
                target: eye.add(yaw_pitch_forward(yaw, pitch)),
                yaw,
                pitch,
                fov_y_radians: 70.0_f32.to_radians(),
                near_plane: 0.05,
                far_plane: 500.0,
            },
            main_light: RenderLightPacket {
                direction: Vec3::new(0.89, 0.25, 0.38).normalize(),
                color: Vec3::new(1.0, 0.96, 0.88),
                intensity: 1.0,
                ambient: 0.34,
            },
            player_marker: RenderDebugMarkerPacket {
                visible: player_mode == PlayerMode::DebugFly,
                position: player_position,
            },
        }
    }

    pub fn write_f32s(self, out: &mut [f32; RENDER_SNAPSHOT_FLOAT_COUNT]) {
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
        out[19] = f32::from(self.player_marker.visible);
        out[20] = self.player_marker.position.x;
        out[21] = self.player_marker.position.y;
        out[22] = self.player_marker.position.z;
        out[23] = 0.0;
    }
}
