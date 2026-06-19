use crate::*;
use std::sync::{Mutex, OnceLock};

mod scene_tests;

#[test]
fn engine_updates_are_deterministic_for_identical_inputs() {
    let mut first = Engine::new();
    let mut second = Engine::new();

    first.scene_mut().create_entity();
    second.scene_mut().create_entity();

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
fn engine_reports_debug_snapshot_without_a_player() {
    let mut engine = Engine::new();
    let entity = engine.scene_mut().create_entity();

    engine
        .update(EngineUpdateInput {
            delta_seconds: 0.25,
        })
        .unwrap();
    let snapshot = engine.debug_snapshot();

    assert_eq!(snapshot.version, ENGINE_CORE_VERSION);
    assert_eq!(snapshot.tick, 1);
    assert_eq!(snapshot.elapsed_seconds, 0.25);
    assert_eq!(snapshot.entity_count, 1);
    assert!(engine.scene().is_alive(entity));
}

#[test]
fn engine_player_api_reports_missing_player_before_creation() {
    let mut engine = Engine::new();

    assert_eq!(engine.player_rig(), None);
    assert_eq!(engine.player_mode(), Err(EngineError::MissingPlayer));
    assert_eq!(
        engine.set_player_mode(PlayerMode::DebugFly),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(engine.toggle_player_mode(), Err(EngineError::MissingPlayer));
    assert_eq!(
        engine.set_player_movement_intent(PlayerMovementIntent::default()),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(
        engine.set_player_position(Vec3::ZERO),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(
        engine.set_player_view(0.0, 0.0),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(
        engine.set_debug_camera(Vec3::ZERO, 0.0, 0.0),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(engine.player_position(), Err(EngineError::MissingPlayer));
    assert_eq!(
        engine.player_eye_transform(),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(engine.render_snapshot(), Err(EngineError::MissingPlayer));
    assert_eq!(
        engine.preview_player_position(0.0),
        Err(EngineError::MissingPlayer)
    );
    assert_eq!(
        engine.update_player(0.0, None),
        Err(EngineError::MissingPlayer)
    );
    assert!(engine.render_mesh_items().unwrap().is_empty());
}

#[test]
fn engine_creates_player_and_camera_rig() {
    let mut engine = Engine::new();
    let rig = engine.create_player(Vec3::new(1.0, 2.0, 3.0));

    assert_eq!(engine.player_rig(), Some(rig));
    assert_eq!(engine.scene().entity_count(), 2);
    assert_eq!(engine.scene().player_id(), Some(rig.player_entity));
    assert_eq!(engine.scene().active_camera_id(), Some(rig.camera_entity));
    assert_eq!(
        engine
            .scene()
            .entity(rig.player_entity)
            .unwrap()
            .player()
            .unwrap()
            .camera_entity,
        rig.camera_entity
    );
    assert!(engine
        .scene()
        .entity(rig.camera_entity)
        .unwrap()
        .camera()
        .is_some());
    let player_renderer = engine
        .scene()
        .entity(rig.player_entity)
        .unwrap()
        .mesh_renderer()
        .copied()
        .unwrap();
    assert!(!player_renderer.visible);
    assert_eq!(
        engine
            .scene()
            .resources()
            .mesh(player_renderer.mesh)
            .unwrap()
            .label,
        DEBUG_PLAYER_MARKER_MESH_LABEL
    );
    assert_eq!(
        engine
            .scene()
            .resources()
            .material(player_renderer.material)
            .unwrap()
            .label,
        DEBUG_PLAYER_MARKER_MATERIAL_LABEL
    );
    assert_eq!(engine.player_mode().unwrap(), PlayerMode::FirstPerson);
    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(1.0, 2.0, 3.0));

    let eye = engine.player_eye_transform().unwrap();
    assert_vec3_near(eye.position, Vec3::new(1.0, 3.65, 3.0));
    assert_vec3_near(
        engine
            .scene()
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
fn third_person_player_moves_like_grounded_player_with_chase_camera() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(0.0, 2.0, 0.0));
    engine.set_player_mode(PlayerMode::ThirdPerson).unwrap();
    engine
        .set_player_movement_intent(PlayerMovementIntent {
            forward: 1.0,
            ..PlayerMovementIntent::default()
        })
        .unwrap();

    assert_vec3_near(
        engine.preview_player_position(1.0).unwrap(),
        Vec3::new(0.0, 2.0, 5.5),
    );
    let eye = engine.update_player(1.0, Some(4.0)).unwrap();

    assert_vec3_near(engine.player_position().unwrap(), Vec3::new(0.0, 4.0, 5.5));
    assert_vec3_near(eye.position, Vec3::new(0.0, 5.25, -3.25));
    assert_close(eye.yaw, 0.0);
    assert_close(eye.pitch, 0.4_f32.atan2(8.75));
}

#[test]
fn third_person_camera_resets_on_mode_entry_and_clamps_above_ground() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(0.0, 10.0, 0.0));
    engine
        .set_player_view(0.0, std::f32::consts::PI * 0.45)
        .unwrap();
    engine.set_player_mode(PlayerMode::ThirdPerson).unwrap();

    let first_eye = engine.player_eye_transform().unwrap();
    assert!(first_eye.position.y >= 11.0);

    engine.set_player_mode(PlayerMode::FirstPerson).unwrap();
    engine
        .set_player_position(Vec3::new(10.0, 20.0, 10.0))
        .unwrap();
    engine.set_player_mode(PlayerMode::ThirdPerson).unwrap();

    let reset_eye = engine.player_eye_transform().unwrap();
    assert!(reset_eye.position.y >= 21.0);
    assert!((reset_eye.position.x - 10.0).abs() <= 0.001);
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
fn player_camera_mode_cycles_first_third_and_debug_fly() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::ZERO);

    assert_eq!(
        engine.toggle_player_mode().unwrap(),
        PlayerMode::ThirdPerson
    );
    assert_eq!(engine.player_mode().unwrap(), PlayerMode::ThirdPerson);
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
    assert_eq!(PlayerMode::FirstPerson.code(), 0);
    assert_eq!(PlayerMode::DebugFly.code(), 1);
    assert_eq!(PlayerMode::ThirdPerson.code(), 2);
    assert_eq!(PlayerMode::from_code(2), Some(PlayerMode::ThirdPerson));
}

#[test]
fn render_snapshot_tracks_player_camera_and_light() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(1.0, 2.0, 3.0));
    engine.set_player_view(0.75, -0.25).unwrap();

    let snapshot = engine.render_snapshot().unwrap();

    assert_vec3_near(snapshot.camera.eye, Vec3::new(1.0, 3.65, 3.0));
    assert_close(snapshot.camera.yaw, 0.75);
    assert_close(snapshot.camera.pitch, -0.25);
    assert_close(
        snapshot.camera.fov_y_radians,
        crate::DEFAULT_CAMERA_FOV_Y_RADIANS,
    );
    assert_close(
        snapshot.camera.near_plane,
        crate::DEFAULT_CAMERA_NEAR_PLANE_METERS,
    );
    assert_close(
        snapshot.camera.far_plane,
        crate::DEFAULT_CAMERA_FAR_PLANE_METERS,
    );
    assert!(snapshot.main_light.direction.x > 0.45);
    assert!(snapshot.main_light.direction.y > 0.80);
    assert!(snapshot.main_light.direction.z > 0.18);
    assert_vec3_near(snapshot.main_light.color, Vec3::new(1.0, 0.96, 0.88));
    assert!(snapshot.main_light.intensity > 0.9);
    assert!(snapshot.main_light.ambient > 0.33);
    assert_close(snapshot.sky.sun_elevation, snapshot.main_light.direction.y);
    assert_eq!(snapshot.sky.turbidity, 2.25);
    assert!(snapshot.sky.cloud_coverage > 0.0);
    assert_eq!(snapshot.sky.star_intensity, 0.0);
}

#[test]
fn sky_cycle_derives_day_night_light_and_presentation_values() {
    let noon = sky_state_for_day_phase(0.25, 10.0);
    assert_vec3_near(noon.main_light.direction, Vec3::UP);
    assert_close(noon.main_light.intensity, 1.0);
    assert_eq!(noon.sky.star_intensity, 0.0);
    assert_eq!(noon.sky.night_blend, 0.0);
    assert_eq!(noon.sky.elapsed_seconds, 10.0);

    let midnight = sky_state_for_day_phase(0.75, 20.0);
    assert!(midnight.main_light.direction.y < -0.99);
    assert_eq!(midnight.main_light.intensity, 0.0);
    assert_eq!(midnight.sky.star_intensity, 1.0);
    assert_eq!(midnight.sky.night_blend, 1.0);
    assert!(midnight.sky.moon_intensity > 0.7);

    let wrapped = sky_state_for_day_phase(1.25, 30.0);
    assert_vec3_near(wrapped.main_light.direction, noon.main_light.direction);

    let sunset = sky_state_for_day_phase(0.49, 40.0);
    assert!(sunset.main_light.color.x > sunset.main_light.color.y);
    assert!(sunset.main_light.color.y > sunset.main_light.color.z);
    assert!(sunset.main_light.color.z < noon.main_light.color.z);

    let start = sky_state_at_elapsed_seconds(0.0);
    let after_old_fast_cycle = sky_state_at_elapsed_seconds(240.0);
    let next_day = sky_state_at_elapsed_seconds(86_400.0);
    assert!(after_old_fast_cycle.main_light.intensity > 0.9);
    assert_eq!(after_old_fast_cycle.sky.star_intensity, 0.0);
    assert_close(start.sky.day_phase, next_day.sky.day_phase);
    assert_vec3_near(start.main_light.direction, next_day.main_light.direction);
}

#[test]
fn sky_packet_defaults_and_write_order_are_stable() {
    let default_day = SkyRenderPacket::default_day();
    let state = sky_state_at_elapsed_seconds(f64::NAN);
    let engine = Engine::new();
    let engine_sky = engine.sky_render_state();
    let mut values = [0.0; SKY_RENDER_PACKET_FLOAT_COUNT];

    default_day.write_f32s(&mut values);

    assert_eq!(default_day.elapsed_seconds, 0.0);
    assert_close(default_day.day_phase, state.sky.day_phase);
    assert_close(default_day.day_phase, engine_sky.sky.day_phase);
    assert_close(default_day.cloud_coverage, 0.34);
    assert_close(default_day.cloud_speed, 0.018);
    assert_close(default_day.cloud_scale, 1.35);
    assert_close(default_day.cloud_softness, 0.18);
    assert_close(default_day.cloud_shadow, 0.42);
    assert_eq!(state.sky.elapsed_seconds, 0.0);
    assert_eq!(values.len(), SKY_RENDER_PACKET_FLOAT_COUNT);
    assert_close(values[0], default_day.elapsed_seconds);
    assert_close(values[1], default_day.day_phase);
    assert_close(values[2], default_day.sun_elevation);
    assert_close(values[3], default_day.star_intensity);
    assert_close(values[4], default_day.turbidity);
    assert_close(values[5], default_day.cloud_coverage);
    assert_close(values[6], default_day.cloud_speed);
    assert_close(values[7], default_day.cloud_scale);
    assert_close(values[8], default_day.cloud_softness);
    assert_close(values[9], default_day.cloud_shadow);
    assert_close(values[10], default_day.moon_intensity);
    assert_close(values[11], default_day.night_blend);
}

#[test]
fn render_mesh_items_track_player_marker_visibility() {
    let mut engine = Engine::new();
    let rig = engine.create_player(Vec3::new(1.0, 2.0, 3.0));

    assert!(engine.render_mesh_items().unwrap().is_empty());

    engine.set_player_mode(PlayerMode::ThirdPerson).unwrap();
    assert!(engine.render_mesh_items().unwrap().is_empty());

    engine.set_player_mode(PlayerMode::DebugFly).unwrap();
    let items = engine.render_mesh_items().unwrap();

    assert_eq!(items.len(), 1);
    assert_eq!(items[0].entity, rig.player_entity);
    assert_close(items[0].world_matrix[12], 1.0);
    assert_close(items[0].world_matrix[13], 2.0);
    assert_close(items[0].world_matrix[14], 3.0);
    assert_eq!(
        engine
            .scene()
            .resources()
            .mesh(items[0].mesh)
            .unwrap()
            .label,
        DEBUG_PLAYER_MARKER_MESH_LABEL
    );

    engine.set_player_mode(PlayerMode::FirstPerson).unwrap();

    assert!(engine.render_mesh_items().unwrap().is_empty());
}

#[test]
fn replacing_player_hides_previous_player_marker() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(1.0, 2.0, 3.0));
    engine.set_player_mode(PlayerMode::DebugFly).unwrap();

    let next_rig = engine.create_player(Vec3::new(4.0, 5.0, 6.0));

    assert_eq!(engine.player_rig(), Some(next_rig));
    assert!(engine.render_mesh_items().unwrap().is_empty());
}

#[test]
fn render_snapshot_can_be_built_directly_from_player_view() {
    let mut values = [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];
    let snapshot = RenderSnapshot::from_player_view(Vec3::new(2.0, 3.0, 4.0), 0.5, -0.25);

    snapshot.write_f32s(&mut values);

    assert_eq!(values.len(), RENDER_SNAPSHOT_FLOAT_COUNT);
    assert_close(values[0], 2.0);
    assert_close(values[1], 3.0);
    assert_close(values[2], 4.0);
    assert_close(values[6], 0.5);
    assert_close(values[7], -0.25);
    assert!(values[17] > 0.9);
    assert!(values[18] > 0.33);
    assert_close(values[19], 0.0);
    assert_close(values[21], values[12]);
    assert_close(values[23], 2.25);
    assert!(values[24] > 0.0);
}

#[test]
fn render_snapshot_writes_stable_f32_packet_layout() {
    let mut engine = Engine::new();
    engine.create_player(Vec3::new(1.0, 2.0, 3.0));
    engine.set_player_view(0.5, -0.25).unwrap();
    let mut values = [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];

    engine.render_snapshot().unwrap().write_f32s(&mut values);

    assert_eq!(values.len(), 31);
    assert_close(values[0], 1.0);
    assert_close(values[1], 3.65);
    assert_close(values[2], 3.0);
    assert_close(values[6], 0.5);
    assert_close(values[7], -0.25);
    assert_close(values[8], crate::DEFAULT_CAMERA_FOV_Y_RADIANS);
    assert_close(values[9], crate::DEFAULT_CAMERA_NEAR_PLANE_METERS);
    assert_close(values[10], crate::DEFAULT_CAMERA_FAR_PLANE_METERS);
    assert!(values[17] > 0.9);
    assert!(values[18] > 0.33);
    assert_close(values[19], 0.0);
    assert_close(values[21], values[12]);
    assert_close(values[23], 2.25);
    assert!(values[24] > 0.0);
}

#[test]
fn wasm_facade_can_reset_engine_and_report_debug_state() {
    let _facade_guard = lock_wasm_facade_tests();

    ofg_engine_create();

    assert_eq!(ofg_engine_core_version(), ENGINE_CORE_VERSION);
    assert_eq!(ofg_engine_tick(), 0);
    assert_eq!(ofg_engine_entity_count(), 0);

    let entity = EntityId::from_raw(ofg_engine_create_entity());
    assert_eq!(entity.index(), 1);
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

    assert_eq!(player.index(), 1);
    assert_eq!(camera.index(), 2);
    assert_eq!(ofg_engine_has_player(), 1);
    assert_eq!(ofg_engine_entity_count(), 2);
    assert_eq!(ofg_engine_player_mode(), PlayerMode::FirstPerson.code());
    assert_eq!(
        ofg_engine_toggle_player_mode(),
        PlayerMode::ThirdPerson.code()
    );
    assert_eq!(ofg_engine_toggle_player_mode(), PlayerMode::DebugFly.code());
    assert_eq!(
        ofg_engine_toggle_player_mode(),
        PlayerMode::FirstPerson.code()
    );

    assert_eq!(ofg_engine_set_player_intent(1.0, 0.0, 0.0, 0, 0.0, 0.0), 1);
    assert_close(ofg_engine_preview_player_x(1.0), 0.0);
    assert_close(ofg_engine_preview_player_y(1.0), 2.0);
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
    assert_eq!(
        ofg_engine_render_snapshot_f32_count(),
        RENDER_SNAPSHOT_FLOAT_COUNT as u32
    );
    assert_ne!(ofg_engine_render_snapshot_f32_ptr(), 0);
    assert_eq!(ofg_engine_write_render_snapshot(), 1);

    let values = facade_render_snapshot_values();
    assert_close(values[0], 1.0);
    assert_close(values[1], 3.65);
    assert_close(values[2], 3.0);
    assert_close(values[6], 0.5);
    assert_close(values[7], -0.25);
    assert!(values[17] > 0.9);
    assert!(values[18] > 0.33);
    assert_close(values[19], 0.0);
    assert_close(values[21], values[12]);
    assert_close(values[23], 2.25);
    assert!(values[24] > 0.0);
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
