use crate::*;
use std::sync::{Mutex, OnceLock};

#[test]
fn entity_ids_reject_stale_generations_after_reuse() {
    let mut world = World::new();
    let first = world.create_entity();

    world.destroy_entity(first).unwrap();
    let second = world.create_entity();

    assert_eq!(first.index(), second.index());
    assert_ne!(first.generation(), second.generation());
    assert!(!world.is_alive(first));
    assert!(world.is_alive(second));
    assert_eq!(
        world.local_transform(first),
        Err(WorldError::InvalidEntity(first))
    );
}

#[test]
fn destroying_an_entity_destroys_descendants() {
    let mut world = World::new();
    let parent = world.create_entity();
    let child = world.create_entity();
    let grandchild = world.create_entity();

    world.set_parent(child, Some(parent)).unwrap();
    world.set_parent(grandchild, Some(child)).unwrap();
    world.destroy_entity(parent).unwrap();

    assert_eq!(world.entity_count(), 0);
    assert!(!world.is_alive(parent));
    assert!(!world.is_alive(child));
    assert!(!world.is_alive(grandchild));
}

#[test]
fn reparenting_updates_parent_child_relationships() {
    let mut world = World::new();
    let first_parent = world.create_entity();
    let second_parent = world.create_entity();
    let child = world.create_entity();

    world.set_parent(child, Some(first_parent)).unwrap();
    world.set_parent(child, Some(second_parent)).unwrap();

    assert_eq!(world.parent(child).unwrap(), Some(second_parent));
    assert_eq!(world.children(first_parent).unwrap(), &[]);
    assert_eq!(world.children(second_parent).unwrap(), &[child]);
}

#[test]
fn parent_cycles_are_rejected() {
    let mut world = World::new();
    let parent = world.create_entity();
    let child = world.create_entity();

    world.set_parent(child, Some(parent)).unwrap();

    assert_eq!(
        world.set_parent(parent, Some(child)),
        Err(WorldError::EntityHierarchyCycle {
            child: parent,
            parent: child
        })
    );
    assert_eq!(
        world.set_parent(parent, Some(parent)),
        Err(WorldError::CannotParentEntityToItself(parent))
    );
}

#[test]
fn world_transforms_follow_parent_transforms() {
    let mut world = World::new();
    let parent = world.create_entity();
    let child = world.create_entity();

    world
        .set_local_transform(
            parent,
            LocalTransform {
                translation: Vec3::new(10.0, 2.0, -4.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::new(2.0, 2.0, 2.0),
            },
        )
        .unwrap();
    world
        .set_local_transform(
            child,
            LocalTransform {
                translation: Vec3::new(1.0, 3.0, 5.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::new(0.5, 1.0, 3.0),
            },
        )
        .unwrap();
    world.set_parent(child, Some(parent)).unwrap();
    world.update_world_transforms();

    assert_eq!(
        world.world_transform(child).unwrap(),
        WorldTransform {
            translation: Vec3::new(12.0, 8.0, 6.0),
            rotation: Quat::IDENTITY,
            scale: Vec3::new(1.0, 2.0, 6.0),
        }
    );
}

#[test]
fn world_transforms_follow_parent_rotation() {
    let mut world = World::new();
    let parent = world.create_entity();
    let child = world.create_entity();

    world
        .set_local_transform(
            parent,
            LocalTransform {
                translation: Vec3::ZERO,
                rotation: Quat::from_yaw(std::f32::consts::FRAC_PI_2),
                scale: Vec3::ONE,
            },
        )
        .unwrap();
    world
        .set_local_transform(
            child,
            LocalTransform {
                translation: Vec3::new(1.0, 0.0, 0.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::ONE,
            },
        )
        .unwrap();
    world.set_parent(child, Some(parent)).unwrap();
    world.update_world_transforms();

    assert_vec3_near(
        world.world_transform(child).unwrap().translation,
        Vec3::new(0.0, 0.0, -1.0),
    );
}

#[test]
fn engine_updates_are_deterministic_for_identical_inputs() {
    let mut first = Engine::new();
    let mut second = Engine::new();

    first.world_mut().create_entity();
    second.world_mut().create_entity();

    let first_summary = first
        .update(EngineUpdateInput {
            delta_seconds: 1.0 / 60.0,
        })
        .unwrap();
    let second_summary = second
        .update(EngineUpdateInput {
            delta_seconds: 1.0 / 60.0,
        })
        .unwrap();

    assert_eq!(first_summary, second_summary);
    assert_eq!(first.tick(), 1);
    assert_eq!(first.elapsed_seconds(), (1.0_f32 / 60.0) as f64);
}

#[test]
fn engine_rejects_non_finite_or_negative_delta_time() {
    let mut engine = Engine::new();

    match engine.update(EngineUpdateInput {
        delta_seconds: f32::NAN,
    }) {
        Err(EngineError::InvalidDeltaSeconds(value)) => assert!(value.is_nan()),
        result => panic!("expected invalid NaN delta, got {result:?}"),
    }
    assert_eq!(
        engine.update(EngineUpdateInput {
            delta_seconds: -0.1,
        }),
        Err(EngineError::InvalidDeltaSeconds(-0.1))
    );
    assert_eq!(engine.tick(), 0);
}

#[test]
fn engine_creates_player_and_camera_rig() {
    let mut engine = Engine::new();
    let rig = engine.create_player(Vec3::new(1.0, 2.0, 3.0));

    assert_eq!(engine.player_rig(), Some(rig));
    assert_eq!(engine.world().entity_count(), 2);
    assert_eq!(engine.player_mode().unwrap(), PlayerMode::FirstPerson);
    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(1.0, 2.0, 3.0));

    let eye = engine.player_eye_transform().unwrap();
    assert_vec3_near(eye.position, Vec3::new(1.0, 3.65, 3.0));
    assert_vec3_near(
        engine
            .world()
            .world_transform(rig.camera_entity)
            .unwrap()
            .translation,
        eye.position,
    );
}

#[test]
fn first_person_player_moves_and_grounds_against_terrain_height() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            forward: 1.0,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    assert_vec3_near(
        engine.preview_player_position(1.0).unwrap(),
        Vec3::new(0.0, 0.0, 5.5),
    );
    let eye = engine.update_player(1.0, Some(4.0)).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(0.0, 4.0, 5.5));
    assert_vec3_near(eye.position, Vec3::new(0.0, 5.65, 5.5));
}

#[test]
fn first_person_player_preserves_height_without_terrain() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(0.0, 3.0, 0.0));
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            forward: 1.0,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    engine.update_player(1.0, None).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(0.0, 3.0, 5.5));
}

#[test]
fn first_person_player_uses_fast_right_movement() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            right: 1.0,
            fast: true,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    engine.update_player(1.0, None).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(16.5, 0.0, 0.0));
}

#[test]
fn player_look_deltas_update_yaw_and_clamp_pitch() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            look_delta_x: 100.0,
            look_delta_y: 100000.0,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    let eye = engine.update_player(0.0, None).unwrap();

    assert_close(eye.yaw, -0.25);
    assert_close(eye.pitch, -std::f32::consts::PI * 0.48);
}

#[test]
fn player_view_and_position_can_be_set_without_movement() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);

    engine
        .set_player_position(Vec3::new(5.0, 6.0, 7.0))
        .unwrap();
    engine.set_player_view(0.75, -0.5).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(5.0, 6.0, 7.0));
    let eye = engine.player_eye_transform().unwrap();
    assert_vec3_near(eye.position, Vec3::new(5.0, 7.65, 7.0));
    assert_close(eye.yaw, 0.75);
    assert_close(eye.pitch, -0.5);
}

#[test]
fn debug_fly_player_moves_without_grounding() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);
    engine.set_player_mode(PlayerMode::DebugFly).unwrap();
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            up: 1.0,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    let eye = engine.update_player(1.0, Some(100.0)).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::ZERO);
    assert_vec3_near(eye.position, Vec3::new(0.0, 23.0, 0.0));
}

#[test]
fn player_camera_mode_toggles_between_first_person_and_debug_fly() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);

    assert_eq!(engine.toggle_player_mode().unwrap(), PlayerMode::DebugFly);
    assert_eq!(engine.player_mode().unwrap(), PlayerMode::DebugFly);
    assert_eq!(
        engine.toggle_player_mode().unwrap(),
        PlayerMode::FirstPerson
    );
    assert_eq!(
        engine.set_player_mode_code(99),
        Err(EngineError::InvalidPlayerMode(99))
    );
}

#[test]
fn render_snapshot_tracks_player_camera_light_and_debug_marker() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(1.0, 2.0, 3.0));
    engine.set_player_view(0.75, -0.25).unwrap();

    let first_person = engine.render_snapshot().unwrap();

    assert_vec3_near(first_person.camera.eye, Vec3::new(1.0, 3.65, 3.0));
    assert_close(first_person.camera.yaw, 0.75);
    assert_close(first_person.camera.pitch, -0.25);
    assert_close(first_person.camera.fov_y_radians, 70.0_f32.to_radians());
    assert_close(first_person.camera.near_plane, 0.05);
    assert_close(first_person.camera.far_plane, 500.0);
    assert_vec3_near(
        first_person.main_light.direction,
        Vec3::new(0.89, 0.25, 0.38).normalize(),
    );
    assert_vec3_near(first_person.main_light.color, Vec3::new(1.0, 0.96, 0.88));
    assert_close(first_person.main_light.intensity, 1.0);
    assert_close(first_person.main_light.ambient, 0.34);
    assert!(!first_person.player_marker.visible);
    assert_vec3_near(first_person.player_marker.position, Vec3::new(1.0, 2.0, 3.0));

    engine.set_player_mode(PlayerMode::DebugFly).unwrap();
    let debug_fly = engine.render_snapshot().unwrap();

    assert!(debug_fly.player_marker.visible);
    assert_vec3_near(debug_fly.player_marker.position, Vec3::new(1.0, 2.0, 3.0));
}

#[test]
fn render_snapshot_writes_stable_f32_packet_layout() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(1.0, 2.0, 3.0));
    engine.set_player_view(0.5, -0.25).unwrap();
    let mut values = [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];

    engine.render_snapshot().unwrap().write_f32s(&mut values);

    assert_eq!(values.len(), 24);
    assert_close(values[0], 1.0);
    assert_close(values[1], 3.65);
    assert_close(values[2], 3.0);
    assert_close(values[6], 0.5);
    assert_close(values[7], -0.25);
    assert_close(values[8], 70.0_f32.to_radians());
    assert_close(values[9], 0.05);
    assert_close(values[10], 500.0);
    assert_close(values[17], 1.0);
    assert_close(values[18], 0.34);
    assert_close(values[19], 0.0);
    assert_close(values[20], 1.0);
    assert_close(values[21], 2.0);
    assert_close(values[22], 3.0);
}

#[test]
fn wasm_facade_can_reset_engine_and_report_debug_state() {
    let _facade_guard = lock_wasm_facade_tests();

    ofg_engine_create();

    assert_eq!(ofg_engine_core_version(), ENGINE_CORE_VERSION);
    assert_eq!(ofg_engine_tick(), 0);
    assert_eq!(ofg_engine_entity_count(), 0);

    let entity = EntityId::from_raw(ofg_engine_create_entity());
    assert_eq!(entity.index(), 0);
    assert_eq!(entity.generation(), 0);
    assert_eq!(ofg_engine_update(0.25), 1);
    assert_eq!(ofg_engine_update(f32::INFINITY), 0);

    assert_eq!(ofg_engine_tick(), 1);
    assert_eq!(ofg_engine_entity_count(), 1);
    assert_eq!(ofg_engine_elapsed_seconds(), 0.25);

    ofg_engine_create();
    assert_eq!(ofg_engine_tick(), 0);
    assert_eq!(ofg_engine_entity_count(), 0);
}

#[test]
fn wasm_facade_exposes_player_state_and_controls() {
    let _facade_guard = lock_wasm_facade_tests();

    ofg_engine_create();
    let player = EntityId::from_raw(ofg_engine_create_player(0.0, 2.0, 0.0));
    let camera = EntityId::from_raw(ofg_engine_player_camera_entity());

    assert_eq!(player.index(), 0);
    assert_eq!(camera.index(), 1);
    assert_eq!(ofg_engine_has_player(), 1);
    assert_eq!(ofg_engine_entity_count(), 2);
    assert_eq!(ofg_engine_player_mode(), PlayerMode::FirstPerson.code());

    assert_eq!(ofg_engine_set_player_intent(1.0, 0.0, 0.0, 0, 0.0, 0.0), 1);
    assert_close(ofg_engine_preview_player_z(1.0), 5.5);
    assert_eq!(ofg_engine_update_player(1.0, 4.0, 1), 1);
    assert_close(ofg_engine_player_z(), 5.5);
    assert_close(ofg_engine_player_y(), 4.0);
    assert_close(ofg_engine_player_eye_y(), 5.65);

    assert_eq!(ofg_engine_set_player_position(2.0, 3.0, 4.0), 1);
    assert_eq!(ofg_engine_set_player_view(0.75, -0.25), 1);
    assert_close(ofg_engine_player_x(), 2.0);
    assert_close(ofg_engine_player_eye_yaw(), 0.75);
    assert_close(ofg_engine_player_eye_pitch(), -0.25);

    assert_eq!(ofg_engine_set_debug_camera(7.0, 8.0, 9.0, 0.5, -0.4), 1);
    assert_close(ofg_engine_player_eye_x(), 7.0);
    assert_close(ofg_engine_player_eye_y(), 8.0);
    assert_close(ofg_engine_player_eye_z(), 9.0);

    assert_eq!(ofg_engine_set_player_intent(0.0, 0.0, 1.0, 0, 0.0, 0.0), 1);
    assert_eq!(ofg_engine_update_player(1.0, 100.0, 1), 1);
    assert_close(ofg_engine_player_eye_y(), 19.0);
    assert_eq!(ofg_engine_set_player_mode(99), 0);
}

#[test]
fn wasm_facade_writes_render_snapshot_to_memory() {
    let _facade_guard = lock_wasm_facade_tests();

    ofg_engine_create();
    assert_eq!(ofg_engine_write_render_snapshot(), 0);

    ofg_engine_create_player(1.0, 2.0, 3.0);
    assert_eq!(ofg_engine_set_player_view(0.5, -0.25), 1);
    assert_eq!(ofg_engine_render_snapshot_f32_count(), 24);
    assert_ne!(ofg_engine_render_snapshot_f32_ptr(), 0);
    assert_eq!(ofg_engine_write_render_snapshot(), 1);

    let values = facade_render_snapshot_values();
    assert_close(values[0], 1.0);
    assert_close(values[1], 3.65);
    assert_close(values[2], 3.0);
    assert_close(values[6], 0.5);
    assert_close(values[7], -0.25);
    assert_close(values[19], 0.0);

    assert_eq!(ofg_engine_set_player_mode(PlayerMode::DebugFly.code()), 1);
    assert_eq!(ofg_engine_write_render_snapshot(), 1);
    let values = facade_render_snapshot_values();
    assert_close(values[19], 1.0);
}

fn assert_vec3_near(actual: Vec3, expected: Vec3) {
    let epsilon = 1.0e-5;
    assert!(
        (actual.x - expected.x).abs() <= epsilon
            && (actual.y - expected.y).abs() <= epsilon
            && (actual.z - expected.z).abs() <= epsilon,
        "expected {actual:?} to be within {epsilon} of {expected:?}"
    );
}

fn assert_close(actual: f32, expected: f32) {
    let epsilon = 1.0e-5;
    assert!(
        (actual - expected).abs() <= epsilon,
        "expected {actual} to be within {epsilon} of {expected}"
    );
}

fn lock_wasm_facade_tests() -> std::sync::MutexGuard<'static, ()> {
    static FACADE_TEST_LOCK: OnceLock<Mutex<()>> = OnceLock::new();

    FACADE_TEST_LOCK
        .get_or_init(|| Mutex::new(()))
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}
