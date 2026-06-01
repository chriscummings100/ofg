use crate::*;

static TEST_MUTEX: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn test_lock() -> std::sync::MutexGuard<'static, ()> {
    TEST_MUTEX.lock().expect("terrain core test lock poisoned")
}

#[test]
fn exported_version_is_stable() {
    let _lock = test_lock();
    assert_eq!(ofg_terrain_core_version(), 1);
    assert_eq!(ofg_terrain_core_preset_count(), 4);
}

#[test]
fn height_sampling_is_deterministic() {
    let _lock = test_lock();
    let a = height_at(0x0F6, 1, 12.5, -20.25);
    let b = height_at(0x0F6, 1, 12.5, -20.25);

    assert_eq!(a.to_bits(), b.to_bits());
}

#[test]
fn presets_produce_different_surfaces() {
    let _lock = test_lock();
    let rolling = height_at(0x0F6, 1, 44.0, -36.0);
    let mountains = height_at(0x0F6, 2, 44.0, -36.0);
    let highland = height_at(0x0F6, 3, 44.0, -36.0);

    assert!((rolling - mountains).abs() > 0.1);
    assert!((rolling - highland).abs() > 0.1);
}

#[test]
fn density_crosses_zero_near_surface() {
    let _lock = test_lock();
    let height = height_at(0x0F6, 1, -18.0, 27.0);
    let below = ofg_density_at(0x0F6, 1, -18.0, height - 0.5, 27.0);
    let above = ofg_density_at(0x0F6, 1, -18.0, height + 0.5, 27.0);

    assert!(below <= 0.0);
    assert!(above > 0.0);
}

#[test]
fn fills_density_chunk_buffer_in_terrain_chunk_order() {
    let _lock = test_lock();
    ofg_fill_density_chunk(0x0F6, 1, -1, 0, 2, 1.0);
    let buffer = unsafe {
        std::slice::from_raw_parts(
            ofg_density_chunk_buffer_ptr(),
            ofg_density_chunk_sample_count() as usize,
        )
    };
    let origin_x = -32.0;
    let origin_y = 0.0;
    let origin_z = 64.0;

    assert_eq!(buffer.len(), TERRAIN_CHUNK_SAMPLE_COUNT);
    assert_eq!(
        buffer[terrain_chunk_sample_index(0, 0, 0)].to_bits(),
        (ofg_density_at(0x0F6, 1, origin_x, origin_y, origin_z) as f32).to_bits()
    );
    assert_eq!(
        buffer[terrain_chunk_sample_index(1, 0, 0)].to_bits(),
        (ofg_density_at(0x0F6, 1, origin_x + 1.0, origin_y, origin_z) as f32).to_bits()
    );
    assert_eq!(
        buffer[terrain_chunk_sample_index(0, 1, 0)].to_bits(),
        (ofg_density_at(0x0F6, 1, origin_x, origin_y + 1.0, origin_z) as f32).to_bits()
    );
    assert_eq!(
        buffer[terrain_chunk_sample_index(0, 0, 1)].to_bits(),
        (ofg_density_at(0x0F6, 1, origin_x, origin_y, origin_z + 1.0) as f32).to_bits()
    );
}

#[test]
fn builds_renderable_chunk_mesh_buffers() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();
    let index_count = ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0);
    let vertex_len = ofg_mesh_vertex_buffer_len() as usize;
    let index_len = ofg_mesh_index_buffer_len() as usize;
    let vertices = unsafe { std::slice::from_raw_parts(ofg_mesh_vertex_buffer_ptr(), vertex_len) };
    let indices = unsafe { std::slice::from_raw_parts(ofg_mesh_index_buffer_ptr(), index_len) };

    assert!(index_count > 0);
    assert!(vertex_len > 0);
    assert!(vertices.iter().all(|value| value.is_finite()));
    assert_eq!(index_count as usize, index_len);
    assert_eq!(vertex_len % FLOATS_PER_VERTEX, 0);
    assert_eq!(index_len % 3, 0);

    let vertex_count = vertex_len / FLOATS_PER_VERTEX;
    for index in indices {
        assert!((*index as usize) < vertex_count);
    }
}

#[test]
fn prepares_density_window_for_mesh_reuse() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();

    let prepared = ofg_prepare_density_chunk_window(0x0F6, 1, 0, 0, 0, 1, 1, 1, 1.0);

    assert_eq!(prepared, 8);
    assert_eq!(ofg_density_chunk_store_entry_count(), 8);
    assert_eq!(ofg_density_chunk_store_generation_count(), 8.0);

    let generated_before_mesh = ofg_density_chunk_store_generation_count();
    let reuses_before_mesh = ofg_density_chunk_store_reuse_count();
    let index_count = ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0);

    assert!(index_count > 0);
    assert_eq!(
        ofg_density_chunk_store_generation_count(),
        generated_before_mesh
    );
    assert_eq!(
        ofg_density_chunk_store_reuse_count(),
        reuses_before_mesh + 8.0
    );
}

#[test]
fn stores_density_chunk_buffer_for_mesh_reuse() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();
    ofg_fill_density_chunk(0x0F6, 1, 0, 0, 0, 1.0);

    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, 0, 0, 0, 1.0), 1);
    assert_eq!(ofg_density_chunk_store_entry_count(), 1);

    let generated_before = ofg_density_chunk_store_generation_count();
    let _ = ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0);

    assert!(ofg_density_chunk_store_reuse_count() >= 1.0);
    assert!(ofg_density_chunk_store_generation_count() >= generated_before);
}

#[test]
fn stream_scheduler_builds_lod0_targets_and_density_aprons() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 8);

    scheduler.sync_center(coord(0, 0, 0));

    assert_eq!(scheduler.desired_lod0_coords(), vec![coord(0, 0, 0)]);
    assert_eq!(scheduler.desired_density_coords().len(), 8);
    assert!(scheduler.desired_density_coords().contains(&coord(0, 0, 0)));
    assert!(scheduler.desired_density_coords().contains(&coord(1, 1, 1)));
    assert_eq!(scheduler.status().desired_lod0_count, 1);
    assert_eq!(scheduler.status().missing_density_count, 8);
    assert_eq!(scheduler.status().missing_lod0_count, 0);
}

#[test]
fn stream_scheduler_submits_nearest_density_jobs_first_up_to_capacity() {
    let mut scheduler = test_stream_scheduler(1, vec![0], 2);

    scheduler.sync_center(coord(0, 0, 0));
    let jobs = scheduler.tick();

    assert_eq!(
        jobs,
        vec![
            TerrainStreamJob::Density {
                generation: 0,
                coord: coord(0, 0, 0)
            },
            TerrainStreamJob::Density {
                generation: 0,
                coord: coord(0, 1, 0)
            }
        ]
    );
    assert_eq!(scheduler.status().in_flight_density_count, 2);
    assert_eq!(scheduler.status().in_flight_lod_count, 0);
}

#[test]
fn stream_scheduler_waits_for_density_dependencies_before_lod0() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 8);

    scheduler.sync_center(coord(0, 0, 0));
    let jobs = scheduler.tick();
    assert_eq!(jobs.len(), 8);
    complete_density_jobs(&mut scheduler, &jobs[..7]);

    assert!(scheduler.tick().is_empty());
    complete_density_jobs(&mut scheduler, &jobs[7..]);

    assert_eq!(
        scheduler.tick(),
        vec![TerrainStreamJob::Lod {
            generation: 0,
            lod: 0,
            coord: coord(0, 0, 0)
        }]
    );
}

#[test]
fn stream_scheduler_records_ready_and_empty_lod0_chunks() {
    let mut ready_scheduler = test_stream_scheduler(0, vec![0], 8);
    ready_scheduler.sync_center(coord(0, 0, 0));
    let density_jobs = ready_scheduler.tick();
    complete_density_jobs(&mut ready_scheduler, &density_jobs);
    let [TerrainStreamJob::Lod {
        generation,
        coord: ready_coord,
        ..
    }] = ready_scheduler.tick()[..]
    else {
        panic!("expected one lod job");
    };

    assert!(ready_scheduler.complete_lod0(generation, ready_coord, false));
    assert_eq!(
        ready_scheduler.chunk_stage(ready_coord),
        TerrainChunkStage::LodReady { lod: 0 }
    );
    assert_eq!(ready_scheduler.status().lod0_ready_count, 1);

    let mut empty_scheduler = test_stream_scheduler(0, vec![0], 8);
    empty_scheduler.sync_center(coord(0, 0, 0));
    let density_jobs = empty_scheduler.tick();
    complete_density_jobs(&mut empty_scheduler, &density_jobs);
    let [TerrainStreamJob::Lod {
        generation,
        coord: empty_coord,
        ..
    }] = empty_scheduler.tick()[..]
    else {
        panic!("expected one lod job");
    };

    assert!(empty_scheduler.complete_lod0(generation, empty_coord, true));
    assert_eq!(
        empty_scheduler.chunk_stage(empty_coord),
        TerrainChunkStage::LodEmpty { lod: 0 }
    );
    assert_eq!(empty_scheduler.status().lod0_empty_count, 1);
}

#[test]
fn stream_scheduler_reset_rejects_stale_density_results() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 1);

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::Density {
        generation: old_generation,
        coord: old_coord,
    }] = scheduler.tick()[..]
    else {
        panic!("expected one density job");
    };

    scheduler.reset(coord(0, 0, 0));
    assert_eq!(scheduler.generation(), old_generation + 1);
    let [TerrainStreamJob::Density {
        generation: new_generation,
        coord: new_coord,
    }] = scheduler.tick()[..]
    else {
        panic!("expected one replacement density job");
    };

    assert_eq!(old_coord, new_coord);
    assert!(!scheduler.complete_density(old_generation, old_coord));
    assert_eq!(
        scheduler.chunk_stage(new_coord),
        TerrainChunkStage::DensityInFlight {
            generation: new_generation
        }
    );
    assert!(scheduler.complete_density(new_generation, new_coord));
    assert_eq!(
        scheduler.chunk_stage(new_coord),
        TerrainChunkStage::DensityReady
    );
}

#[test]
fn stream_scheduler_prunes_chunks_outside_the_current_window() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 8);

    scheduler.sync_center(coord(0, 0, 0));
    let density_jobs = scheduler.tick();
    complete_density_jobs(&mut scheduler, &density_jobs);
    let [TerrainStreamJob::Lod {
        generation,
        coord: lod_coord,
        ..
    }] = scheduler.tick()[..]
    else {
        panic!("expected one lod job");
    };
    assert!(scheduler.complete_lod0(generation, lod_coord, false));

    scheduler.sync_center(coord(4, 0, 0));

    assert_eq!(
        scheduler.chunk_stage(coord(0, 0, 0)),
        TerrainChunkStage::NotPresent
    );
    assert_eq!(scheduler.desired_lod0_coords(), vec![coord(4, 0, 0)]);
    assert!(scheduler.desired_density_coords().contains(&coord(5, 1, 1)));
}

#[test]
fn stream_scheduler_failed_density_jobs_can_be_retried() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 1);

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::Density { generation, coord }] = scheduler.tick()[..] else {
        panic!("expected one density job");
    };

    assert!(scheduler.fail_density(generation, coord));
    assert_eq!(scheduler.chunk_stage(coord), TerrainChunkStage::NotPresent);
    assert_eq!(
        scheduler.tick(),
        vec![TerrainStreamJob::Density { generation, coord }]
    );
}

#[test]
fn stream_scheduler_validates_configuration() {
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            horizontal_radius: -1,
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::NegativeHorizontalRadius)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            vertical_chunk_offsets: Vec::new(),
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::EmptyVerticalOffsets)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            vertical_chunk_offsets: vec![0, 0],
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::DuplicateVerticalOffsets)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            max_in_flight_jobs: 0,
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::ZeroMaxInFlightJobs)
    );
}

fn test_stream_scheduler(
    horizontal_radius: i32,
    vertical_chunk_offsets: Vec<i32>,
    max_in_flight_jobs: usize,
) -> TerrainStreamScheduler {
    TerrainStreamScheduler::new(TerrainStreamConfig {
        horizontal_radius,
        vertical_chunk_offsets,
        max_in_flight_jobs,
    })
    .expect("test stream config should be valid")
}

fn coord(x: i32, y: i32, z: i32) -> TerrainChunkCoord {
    TerrainChunkCoord { x, y, z }
}

fn complete_density_jobs(scheduler: &mut TerrainStreamScheduler, jobs: &[TerrainStreamJob]) {
    for job in jobs {
        let TerrainStreamJob::Density { generation, coord } = *job else {
            panic!("expected density job");
        };
        assert!(scheduler.complete_density(generation, coord));
    }
}
