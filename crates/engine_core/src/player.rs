use crate::math::Vec3;
use crate::scene::EntityId;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlayerMode {
    FirstPerson,
    ThirdPerson,
    DebugFly,
}

impl PlayerMode {
    pub const fn code(self) -> u32 {
        match self {
            Self::FirstPerson => 0,
            Self::DebugFly => 1,
            Self::ThirdPerson => 2,
        }
    }

    pub const fn from_code(code: u32) -> Option<Self> {
        match code {
            0 => Some(Self::FirstPerson),
            1 => Some(Self::DebugFly),
            2 => Some(Self::ThirdPerson),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerMovementIntent {
    pub forward: f32,
    pub right: f32,
    pub up: f32,
    pub fast: bool,
    pub look_delta_x: f32,
    pub look_delta_y: f32,
}

impl Default for PlayerMovementIntent {
    fn default() -> Self {
        Self {
            forward: 0.0,
            right: 0.0,
            up: 0.0,
            fast: false,
            look_delta_x: 0.0,
            look_delta_y: 0.0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PlayerConfig {
    pub move_speed: f32,
    pub debug_fly_speed: f32,
    pub eye_height: f32,
    pub third_person_camera_distance: f32,
    pub third_person_camera_height: f32,
    pub look_sensitivity: f32,
    pub max_pitch: f32,
}

impl Default for PlayerConfig {
    fn default() -> Self {
        Self {
            move_speed: 5.5,
            debug_fly_speed: 11.0,
            eye_height: 1.65,
            third_person_camera_distance: 6.0,
            third_person_camera_height: 2.25,
            look_sensitivity: 0.0025,
            max_pitch: std::f32::consts::PI * 0.48,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PlayerRig {
    pub player_entity: EntityId,
    pub camera_entity: EntityId,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EyeTransform {
    pub position: Vec3,
    pub yaw: f32,
    pub pitch: f32,
}

pub(crate) fn speed_multiplier(intent: PlayerMovementIntent) -> f32 {
    if intent.fast {
        3.0
    } else {
        1.0
    }
}

pub(crate) fn yaw_pitch_forward(yaw: f32, pitch: f32) -> Vec3 {
    let cp = pitch.cos();
    Vec3::new(yaw.sin() * cp, pitch.sin(), yaw.cos() * cp).normalize()
}

pub(crate) fn yaw_right(yaw: f32) -> Vec3 {
    Vec3::new(yaw.cos(), 0.0, -yaw.sin()).normalize()
}
