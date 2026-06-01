use std::sync::{Mutex, OnceLock};

use crate::engine::{Engine, EngineUpdateInput};
use crate::math::Vec3;
use crate::player::{PlayerMode, PlayerMovementIntent};
use crate::render_packet::RENDER_SNAPSHOT_FLOAT_COUNT;
use crate::ENGINE_CORE_VERSION;

static mut RENDER_SNAPSHOT_F32S: [f32; RENDER_SNAPSHOT_FLOAT_COUNT] =
    [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];

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

#[no_mangle]
pub extern "C" fn ofg_engine_render_snapshot_f32_count() -> u32 {
    RENDER_SNAPSHOT_FLOAT_COUNT as u32
}

#[no_mangle]
pub extern "C" fn ofg_engine_render_snapshot_f32_ptr() -> u32 {
    // SAFETY: this exposes the stable address of the facade-owned packet buffer.
    // Callers must request a fresh write before reading the buffer.
    unsafe { std::ptr::addr_of!(RENDER_SNAPSHOT_F32S) as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_engine_write_render_snapshot() -> u32 {
    with_facade_engine(|engine| {
        let Ok(snapshot) = engine.render_snapshot() else {
            return 0;
        };

        // SAFETY: taking the raw pointer is unsafe because the buffer is mutable
        // static state. The actual write below is serialized by the facade caller.
        let snapshot_ptr = unsafe { std::ptr::addr_of_mut!(RENDER_SNAPSHOT_F32S) };
        // SAFETY: the facade runs on the browser main thread today and facade tests
        // serialize access with a mutex. The buffer is overwritten atomically from
        // the caller's point of view before TypeScript reads the exported memory.
        unsafe {
            snapshot.write_f32s(&mut *snapshot_ptr);
        }

        1
    })
}

#[cfg(test)]
pub(crate) fn facade_render_snapshot_values() -> [f32; RENDER_SNAPSHOT_FLOAT_COUNT] {
    // SAFETY: facade unit tests serialize access to the global facade state.
    unsafe { RENDER_SNAPSHOT_F32S }
}
