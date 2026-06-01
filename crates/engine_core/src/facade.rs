use std::sync::{Mutex, OnceLock};

use crate::engine::{Engine, EngineUpdateInput};
use crate::math::Vec3;
use crate::player::{PlayerMode, PlayerMovementIntent};
use crate::ENGINE_CORE_VERSION;

fn with_facade_engine<R>(callback: impl FnOnce(&mut Engine) -> R) -> R {
    static FACADE_ENGINE: OnceLock<Mutex<Engine>> = OnceLock::new();
    let mutex = FACADE_ENGINE.get_or_init(|| Mutex::new(Engine::new()));
    let mut engine = match mutex.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    };

    callback(&mut engine)
}

#[no_mangle]
pub extern "C" fn ofg_engine_core_version() -> u32 {
    ENGINE_CORE_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_engine_create() {
    with_facade_engine(|engine| *engine = Engine::new());
}

#[no_mangle]
pub extern "C" fn ofg_engine_create_entity() -> u64 {
    with_facade_engine(|engine| engine.world_mut().create_entity().to_raw())
}

#[no_mangle]
pub extern "C" fn ofg_engine_create_player(x: f32, y: f32, z: f32) -> u64 {
    with_facade_engine(|engine| {
        engine
            .create_player(Vec3::new(x, y, z))
            .player_entity
            .to_raw()
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_has_player() -> u32 {
    with_facade_engine(|engine| u32::from(engine.player_rig().is_some()))
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_camera_entity() -> u64 {
    with_facade_engine(|engine| {
        engine
            .player_rig()
            .map(|rig| rig.camera_entity.to_raw())
            .unwrap_or(u64::MAX)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_mode() -> u32 {
    with_facade_engine(|engine| {
        engine
            .player_mode()
            .map(PlayerMode::code)
            .unwrap_or(u32::MAX)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_set_player_mode(mode: u32) -> u32 {
    with_facade_engine(|engine| engine.set_player_mode_code(mode).map(|_| 1).unwrap_or(0))
}

#[no_mangle]
pub extern "C" fn ofg_engine_toggle_player_mode() -> u32 {
    with_facade_engine(|engine| {
        engine
            .toggle_player_mode()
            .map(PlayerMode::code)
            .unwrap_or(u32::MAX)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_set_player_intent(
    forward: f32,
    right: f32,
    up: f32,
    fast: u32,
    look_delta_x: f32,
    look_delta_y: f32,
) -> u32 {
    with_facade_engine(|engine| {
        engine
            .set_player_movement_intent(PlayerMovementIntent {
                forward,
                right,
                up,
                fast: fast != 0,
                look_delta_x,
                look_delta_y,
            })
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_set_player_position(x: f32, y: f32, z: f32) -> u32 {
    with_facade_engine(|engine| {
        engine
            .set_player_position(Vec3::new(x, y, z))
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_set_player_view(yaw: f32, pitch: f32) -> u32 {
    with_facade_engine(|engine| engine.set_player_view(yaw, pitch).map(|_| 1).unwrap_or(0))
}

#[no_mangle]
pub extern "C" fn ofg_engine_set_debug_camera(x: f32, y: f32, z: f32, yaw: f32, pitch: f32) -> u32 {
    with_facade_engine(|engine| {
        engine
            .set_debug_camera(Vec3::new(x, y, z), yaw, pitch)
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_update_player(
    delta_seconds: f32,
    terrain_height: f32,
    has_terrain: u32,
) -> u32 {
    with_facade_engine(|engine| {
        engine
            .update_player(
                delta_seconds,
                if has_terrain == 0 {
                    None
                } else {
                    Some(terrain_height)
                },
            )
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_preview_player_x(delta_seconds: f32) -> f32 {
    with_facade_engine(|engine| {
        engine
            .preview_player_position(delta_seconds)
            .map(|position| position.x)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_preview_player_y(delta_seconds: f32) -> f32 {
    with_facade_engine(|engine| {
        engine
            .preview_player_position(delta_seconds)
            .map(|position| position.y)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_preview_player_z(delta_seconds: f32) -> f32 {
    with_facade_engine(|engine| {
        engine
            .preview_player_position(delta_seconds)
            .map(|position| position.z)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_update(delta_seconds: f32) -> u32 {
    with_facade_engine(|engine| {
        engine
            .update(EngineUpdateInput { delta_seconds })
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_tick() -> u64 {
    with_facade_engine(|engine| engine.debug_snapshot().tick)
}

#[no_mangle]
pub extern "C" fn ofg_engine_elapsed_seconds() -> f64 {
    with_facade_engine(|engine| engine.debug_snapshot().elapsed_seconds)
}

#[no_mangle]
pub extern "C" fn ofg_engine_entity_count() -> u32 {
    with_facade_engine(|engine| engine.debug_snapshot().entity_count.min(u32::MAX as usize) as u32)
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_eye_x() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_eye_transform()
            .map(|eye| eye.position.x)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_eye_y() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_eye_transform()
            .map(|eye| eye.position.y)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_eye_z() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_eye_transform()
            .map(|eye| eye.position.z)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_eye_yaw() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_eye_transform()
            .map(|eye| eye.yaw)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_eye_pitch() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_eye_transform()
            .map(|eye| eye.pitch)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_x() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_position()
            .map(|position| position.x)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_y() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_position()
            .map(|position| position.y)
            .unwrap_or(f32::NAN)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_player_z() -> f32 {
    with_facade_engine(|engine| {
        engine
            .player_position()
            .map(|position| position.z)
            .unwrap_or(f32::NAN)
    })
}
