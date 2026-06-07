use crate::*;

pub(crate) static mut DENSITY_CHUNK_BUFFER: [f32; TERRAIN_CHUNK_SAMPLE_COUNT] =
    [0.0; TERRAIN_CHUNK_SAMPLE_COUNT];
pub(crate) static mut MESH_VERTEX_BUFFER: Vec<f32> = Vec::new();
pub(crate) static mut MESH_INDEX_BUFFER: Vec<u32> = Vec::new();
pub(crate) static mut MESH_PACKET_INPUT_VERTEX_BUFFER: Vec<f32> = Vec::new();
pub(crate) static mut MESH_PACKET_INPUT_INDEX_BUFFER: Vec<u32> = Vec::new();
pub(crate) const STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY: usize = 64;
pub(crate) const STREAM_JOB_BUFFER_CAPACITY: usize = 1024;
pub(crate) const STREAM_COORD_BUFFER_CAPACITY: usize = 16384;
pub(crate) const MESH_PACKET_COORD_BUFFER_CAPACITY: usize = STREAM_COORD_BUFFER_CAPACITY;
pub(crate) static mut STREAM_VERTICAL_OFFSET_BUFFER: [i32; STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY] =
    [0; STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_KIND_BUFFER: [u32; STREAM_JOB_BUFFER_CAPACITY] =
    [0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_LOD_BUFFER: [u32; STREAM_JOB_BUFFER_CAPACITY] =
    [0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_GENERATION_BUFFER: [f64; STREAM_JOB_BUFFER_CAPACITY] =
    [0.0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_X_BUFFER: [i32; STREAM_JOB_BUFFER_CAPACITY] =
    [0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_Y_BUFFER: [i32; STREAM_JOB_BUFFER_CAPACITY] =
    [0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_JOB_Z_BUFFER: [i32; STREAM_JOB_BUFFER_CAPACITY] =
    [0; STREAM_JOB_BUFFER_CAPACITY];
pub(crate) static mut STREAM_COORD_X_BUFFER: [i32; STREAM_COORD_BUFFER_CAPACITY] =
    [0; STREAM_COORD_BUFFER_CAPACITY];
pub(crate) static mut STREAM_COORD_Y_BUFFER: [i32; STREAM_COORD_BUFFER_CAPACITY] =
    [0; STREAM_COORD_BUFFER_CAPACITY];
pub(crate) static mut STREAM_COORD_Z_BUFFER: [i32; STREAM_COORD_BUFFER_CAPACITY] =
    [0; STREAM_COORD_BUFFER_CAPACITY];
pub(crate) static mut MESH_PACKET_LOD_BUFFER: [u32; MESH_PACKET_COORD_BUFFER_CAPACITY] =
    [0; MESH_PACKET_COORD_BUFFER_CAPACITY];
pub(crate) static mut MESH_PACKET_X_BUFFER: [i32; MESH_PACKET_COORD_BUFFER_CAPACITY] =
    [0; MESH_PACKET_COORD_BUFFER_CAPACITY];
pub(crate) static mut MESH_PACKET_Y_BUFFER: [i32; MESH_PACKET_COORD_BUFFER_CAPACITY] =
    [0; MESH_PACKET_COORD_BUFFER_CAPACITY];
pub(crate) static mut MESH_PACKET_Z_BUFFER: [i32; MESH_PACKET_COORD_BUFFER_CAPACITY] =
    [0; MESH_PACKET_COORD_BUFFER_CAPACITY];
pub(crate) static mut WORKER_POOL_TASK_REQUEST_ID: u32 = 0;
pub(crate) static mut WORKER_POOL_TASK_WORKER_INDEX: u32 = 0;
pub(crate) static mut WORKER_POOL_TASK_RUNTIME_GENERATION: f64 = 0.0;

#[no_mangle]
pub extern "C" fn ofg_terrain_core_version() -> u32 {
    TERRAIN_CORE_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_terrain_core_preset_count() -> u32 {
    TERRAIN_PRESETS.len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_max_entries() -> u32 {
    DENSITY_CHUNK_STORE_MAX_ENTRIES as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_vertical_offset_buffer_capacity() -> u32 {
    STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_vertical_offset_buffer_ptr() -> *mut i32 {
    unsafe { core::ptr::addr_of_mut!(STREAM_VERTICAL_OFFSET_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_buffer_capacity() -> u32 {
    STREAM_JOB_BUFFER_CAPACITY as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_coord_buffer_capacity() -> u32 {
    STREAM_COORD_BUFFER_CAPACITY as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_kind_buffer_ptr() -> *const u32 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_KIND_BUFFER).cast::<u32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_lod_buffer_ptr() -> *const u32 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_LOD_BUFFER).cast::<u32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_generation_buffer_ptr() -> *const f64 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_GENERATION_BUFFER).cast::<f64>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_x_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_X_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_y_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_Y_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_job_z_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_JOB_Z_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_coord_x_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_COORD_X_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_coord_y_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_COORD_Y_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_stream_coord_z_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(STREAM_COORD_Z_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_coord_buffer_capacity() -> u32 {
    MESH_PACKET_COORD_BUFFER_CAPACITY as u32
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_lod_buffer_ptr() -> *const u32 {
    unsafe { core::ptr::addr_of!(MESH_PACKET_LOD_BUFFER).cast::<u32>() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_x_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(MESH_PACKET_X_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_y_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(MESH_PACKET_Y_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_z_buffer_ptr() -> *const i32 {
    unsafe { core::ptr::addr_of!(MESH_PACKET_Z_BUFFER).cast::<i32>() }
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_max_workers() -> u32 {
    TERRAIN_WORKER_POOL_MAX_WORKERS as u32
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_configure(worker_count: u32) -> u32 {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .configure(worker_count as usize)
        .is_ok() as u32
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_reset() {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .reset();
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_worker_count() -> u32 {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .worker_count() as u32
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_in_flight_count() -> u32 {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .in_flight_count() as u32
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_runtime_generation() -> f64 {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .runtime_generation() as f64
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_task_request_id() -> u32 {
    unsafe { WORKER_POOL_TASK_REQUEST_ID }
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_task_worker_index() -> u32 {
    unsafe { WORKER_POOL_TASK_WORKER_INDEX }
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_task_runtime_generation() -> f64 {
    unsafe { WORKER_POOL_TASK_RUNTIME_GENERATION }
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_begin_task(
    kind: u32,
    lod: u32,
    stream_generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let Some(kind) = TerrainWorkerTaskKind::from_code(kind) else {
        return 0;
    };
    let Some(stream_generation) = generation_from_f64(stream_generation) else {
        return 0;
    };
    let task = terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .begin_task(
            kind,
            lod,
            stream_generation,
            TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
        );
    let Ok(task) = task else {
        return 0;
    };

    unsafe {
        WORKER_POOL_TASK_REQUEST_ID = task.request_id;
        WORKER_POOL_TASK_WORKER_INDEX = task.worker_index as u32;
        WORKER_POOL_TASK_RUNTIME_GENERATION = terrain_worker_pool()
            .lock()
            .expect("terrain worker pool lock poisoned")
            .runtime_generation() as f64;
    }

    1
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_finish_task(
    request_id: u32,
    kind: u32,
    lod: u32,
    stream_generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let Some(kind) = TerrainWorkerTaskKind::from_code(kind) else {
        return 2;
    };
    let Some(stream_generation) = generation_from_f64(stream_generation) else {
        return 2;
    };

    match terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .finish_task(
            request_id,
            kind,
            lod,
            stream_generation,
            TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
        ) {
        TerrainWorkerTaskFinish::Stale => 0,
        TerrainWorkerTaskFinish::Matched => 1,
        TerrainWorkerTaskFinish::Mismatched => 2,
    }
}

#[no_mangle]
pub extern "C" fn ofg_worker_pool_fail_task(request_id: u32) -> u32 {
    terrain_worker_pool()
        .lock()
        .expect("terrain worker pool lock poisoned")
        .fail_task(request_id) as u32
}

#[no_mangle]
pub extern "C" fn ofg_prepare_terrain_mesh_packet_input(vertex_len: u32, index_len: u32) -> u32 {
    let vertex_len = vertex_len as usize;
    let index_len = index_len as usize;

    if vertex_len == 0
        || index_len == 0
        || vertex_len % FLOATS_PER_VERTEX != 0
        || index_len % 3 != 0
    {
        return 0;
    }

    unsafe {
        MESH_PACKET_INPUT_VERTEX_BUFFER.resize(vertex_len, 0.0);
        MESH_PACKET_INPUT_INDEX_BUFFER.resize(index_len, 0);
    }

    1
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_input_vertex_buffer_ptr() -> *mut f32 {
    unsafe { MESH_PACKET_INPUT_VERTEX_BUFFER.as_mut_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_input_vertex_buffer_len() -> u32 {
    unsafe { MESH_PACKET_INPUT_VERTEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_input_index_buffer_ptr() -> *mut u32 {
    unsafe { MESH_PACKET_INPUT_INDEX_BUFFER.as_mut_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_input_index_buffer_len() -> u32 {
    unsafe { MESH_PACKET_INPUT_INDEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_store_terrain_mesh_packet_buffer(
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    lod: u32,
) -> u32 {
    let Ok(lod) = u8::try_from(lod) else {
        return 0;
    };
    let key = terrain_mesh_packet_key(
        TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        },
        lod,
    );
    let vertices = unsafe { MESH_PACKET_INPUT_VERTEX_BUFFER.clone() };
    let indices = unsafe { MESH_PACKET_INPUT_INDEX_BUFFER.clone() };

    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .insert(key, vertices, indices)
        .is_ok() as u32
}

#[no_mangle]
pub extern "C" fn ofg_reset_terrain_mesh_packet_store() {
    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .reset();
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_store_entry_count() -> u32 {
    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_store_version() -> f64 {
    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .version() as f64
}

#[no_mangle]
pub extern "C" fn ofg_terrain_mesh_packet_store_contains(
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    lod: u32,
) -> u32 {
    let Ok(lod) = u8::try_from(lod) else {
        return 0;
    };
    let key = terrain_mesh_packet_key(
        TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        },
        lod,
    );

    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .contains(key) as u32
}

#[no_mangle]
pub extern "C" fn ofg_remove_terrain_mesh_packet(
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    lod: u32,
) -> u32 {
    let Ok(lod) = u8::try_from(lod) else {
        return 0;
    };
    let key = terrain_mesh_packet_key(
        TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        },
        lod,
    );

    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .remove(key) as u32
}

#[no_mangle]
pub extern "C" fn ofg_retain_terrain_mesh_packets(count: u32) -> u32 {
    let count = count as usize;
    if count > MESH_PACKET_COORD_BUFFER_CAPACITY {
        return 0;
    }

    let mut keys = Vec::with_capacity(count);
    for index in 0..count {
        let lod = unsafe {
            *core::ptr::addr_of!(MESH_PACKET_LOD_BUFFER)
                .cast::<u32>()
                .add(index)
        };
        let Ok(lod) = u8::try_from(lod) else {
            return 0;
        };
        let chunk_x = unsafe {
            *core::ptr::addr_of!(MESH_PACKET_X_BUFFER)
                .cast::<i32>()
                .add(index)
        };
        let chunk_y = unsafe {
            *core::ptr::addr_of!(MESH_PACKET_Y_BUFFER)
                .cast::<i32>()
                .add(index)
        };
        let chunk_z = unsafe {
            *core::ptr::addr_of!(MESH_PACKET_Z_BUFFER)
                .cast::<i32>()
                .add(index)
        };
        keys.push(terrain_mesh_packet_key(
            TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
            lod,
        ));
    }

    terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .retain_keys(&keys);
    1
}

#[no_mangle]
pub extern "C" fn ofg_write_terrain_mesh_packet_coords() -> u32 {
    let keys = terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .keys();
    let count = keys.len().min(MESH_PACKET_COORD_BUFFER_CAPACITY);

    for (index, key) in keys.iter().take(count).enumerate() {
        unsafe {
            *core::ptr::addr_of_mut!(MESH_PACKET_LOD_BUFFER)
                .cast::<u32>()
                .add(index) = u32::from(key.lod);
            *core::ptr::addr_of_mut!(MESH_PACKET_X_BUFFER)
                .cast::<i32>()
                .add(index) = key.chunk_x;
            *core::ptr::addr_of_mut!(MESH_PACKET_Y_BUFFER)
                .cast::<i32>()
                .add(index) = key.chunk_y;
            *core::ptr::addr_of_mut!(MESH_PACKET_Z_BUFFER)
                .cast::<i32>()
                .add(index) = key.chunk_z;
        }
    }

    count as u32
}

#[no_mangle]
pub extern "C" fn ofg_load_terrain_mesh_packet_buffer(
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    lod: u32,
) -> u32 {
    let Ok(lod) = u8::try_from(lod) else {
        return 0;
    };
    let key = terrain_mesh_packet_key(
        TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        },
        lod,
    );
    let Some((vertices, indices)) = terrain_mesh_packet_store()
        .lock()
        .expect("terrain mesh packet store lock poisoned")
        .get(key)
        .map(|(vertices, indices)| (vertices.to_vec(), indices.to_vec()))
    else {
        return 0;
    };

    unsafe {
        MESH_VERTEX_BUFFER = vertices;
        MESH_INDEX_BUFFER = indices;
    }

    1
}

#[no_mangle]
pub extern "C" fn ofg_stream_configure(
    horizontal_radius: i32,
    vertical_offset_count: u32,
    max_in_flight_jobs: u32,
) -> u32 {
    let vertical_offset_count = vertical_offset_count as usize;
    let max_in_flight_jobs = max_in_flight_jobs as usize;
    if vertical_offset_count > STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY
        || max_in_flight_jobs == 0
        || max_in_flight_jobs > STREAM_JOB_BUFFER_CAPACITY
    {
        return 0;
    }

    let vertical_chunk_offsets = unsafe {
        core::slice::from_raw_parts(
            core::ptr::addr_of!(STREAM_VERTICAL_OFFSET_BUFFER).cast::<i32>(),
            vertical_offset_count,
        )
    }
    .to_vec();
    let config = TerrainStreamConfig::single_lod0(
        horizontal_radius,
        vertical_chunk_offsets,
        max_in_flight_jobs,
    );
    let Ok(scheduler) = TerrainStreamScheduler::new(config) else {
        return 0;
    };

    *terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned") = scheduler;
    1
}

#[no_mangle]
pub extern "C" fn ofg_stream_generation() -> f64 {
    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .generation() as f64
}

#[no_mangle]
pub extern "C" fn ofg_stream_sync_center(chunk_x: i32, chunk_y: i32, chunk_z: i32) {
    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .sync_center(TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        });
}

#[no_mangle]
pub extern "C" fn ofg_stream_reset(chunk_x: i32, chunk_y: i32, chunk_z: i32) {
    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .reset(TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        });
}

#[no_mangle]
pub extern "C" fn ofg_stream_invalidate_all() {
    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .invalidate_all();
}

#[no_mangle]
pub extern "C" fn ofg_stream_tick() -> u32 {
    let jobs = terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .tick();
    write_stream_jobs(&jobs)
}

#[no_mangle]
pub extern "C" fn ofg_stream_complete_density(
    generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let Some(generation) = generation_from_f64(generation) else {
        return 0;
    };

    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .complete_density(
            generation,
            TerrainNodeKey::lod0(TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            }),
        ) as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_fail_density(
    generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let Some(generation) = generation_from_f64(generation) else {
        return 0;
    };

    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .fail_density(
            generation,
            TerrainNodeKey::lod0(TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            }),
        ) as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_complete_lod0(
    generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    empty: u32,
) -> u32 {
    let Some(generation) = generation_from_f64(generation) else {
        return 0;
    };

    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .complete_lod0(
            generation,
            TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
            empty != 0,
        ) as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_fail_lod0(
    generation: f64,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let Some(generation) = generation_from_f64(generation) else {
        return 0;
    };

    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .fail_lod0(
            generation,
            TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
        ) as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_write_desired_density_coords() -> u32 {
    let coords = terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .desired_density_coords();

    write_stream_coords(&coords)
}

#[no_mangle]
pub extern "C" fn ofg_stream_write_desired_lod0_coords() -> u32 {
    let coords = terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .desired_lod0_coords();

    write_stream_coords(&coords)
}

#[no_mangle]
pub extern "C" fn ofg_stream_write_lod0_dependency_coords(
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
) -> u32 {
    let coords = terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .lod0_density_dependencies(TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        });

    write_stream_coords(&coords)
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_desired_density_count() -> u32 {
    terrain_stream_status().desired_density_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_desired_lod0_count() -> u32 {
    terrain_stream_status().desired_lod0_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_density_ready_count() -> u32 {
    terrain_stream_status().density_ready_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_lod0_ready_count() -> u32 {
    terrain_stream_status().lod0_ready_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_lod0_empty_count() -> u32 {
    terrain_stream_status().lod0_empty_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_in_flight_density_count() -> u32 {
    terrain_stream_status().in_flight_density_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_in_flight_lod_count() -> u32 {
    terrain_stream_status().in_flight_lod_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_missing_density_count() -> u32 {
    terrain_stream_status().missing_density_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_missing_lod0_count() -> u32 {
    terrain_stream_status().missing_lod0_count as u32
}

#[no_mangle]
pub extern "C" fn ofg_stream_status_max_in_flight_jobs() -> u32 {
    terrain_stream_status().max_in_flight_jobs as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_entry_count() -> u32 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .entries
        .len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_reuse_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .reuses as f64
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_generation_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .generations as f64
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_store_eviction_count() -> f64 {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .evictions as f64
}

#[no_mangle]
pub extern "C" fn ofg_reset_density_chunk_store() {
    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .reset();
}

#[no_mangle]
pub extern "C" fn ofg_store_density_chunk_buffer(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    if cell_size <= 0.0 {
        return 0;
    }

    let coord = TerrainChunkCoord {
        x: chunk_x,
        y: chunk_y,
        z: chunk_z,
    };
    let preset_id = terrain_preset_index(preset);
    let key = density_chunk_store_key(seed, preset_id, coord, cell_size);
    let densities = unsafe {
        core::slice::from_raw_parts(
            core::ptr::addr_of!(DENSITY_CHUNK_BUFFER).cast::<f32>(),
            TERRAIN_CHUNK_SAMPLE_COUNT,
        )
    }
    .to_vec();

    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .insert(key, densities);

    1
}

#[no_mangle]
pub extern "C" fn ofg_retain_density_chunk_store_window(
    seed: u32,
    preset: u32,
    min_chunk_x: i32,
    min_chunk_y: i32,
    min_chunk_z: i32,
    max_chunk_x: i32,
    max_chunk_y: i32,
    max_chunk_z: i32,
    cell_size: f64,
) -> u32 {
    if cell_size <= 0.0 {
        return 0;
    }

    let min_x = min_chunk_x.min(max_chunk_x);
    let max_x = min_chunk_x.max(max_chunk_x);
    let min_y = min_chunk_y.min(max_chunk_y);
    let max_y = min_chunk_y.max(max_chunk_y);
    let min_z = min_chunk_z.min(max_chunk_z);
    let max_z = min_chunk_z.max(max_chunk_z);
    let preset_id = terrain_preset_index(preset);
    let mut store = density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned");

    store.retain_window(
        seed, preset_id, cell_size, min_x, min_y, min_z, max_x, max_y, max_z,
    );

    store.entries.len() as u32
}

#[no_mangle]
pub extern "C" fn ofg_prepare_density_chunk_window(
    seed: u32,
    preset: u32,
    min_chunk_x: i32,
    min_chunk_y: i32,
    min_chunk_z: i32,
    max_chunk_x: i32,
    max_chunk_y: i32,
    max_chunk_z: i32,
    cell_size: f64,
) -> u32 {
    if cell_size <= 0.0 {
        return 0;
    }

    let min_x = min_chunk_x.min(max_chunk_x);
    let max_x = min_chunk_x.max(max_chunk_x);
    let min_y = min_chunk_y.min(max_chunk_y);
    let max_y = min_chunk_y.max(max_chunk_y);
    let min_z = min_chunk_z.min(max_chunk_z);
    let max_z = min_chunk_z.max(max_chunk_z);
    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);

    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .retain_window(
            seed, preset_id, cell_size, min_x, min_y, min_z, max_x, max_y, max_z,
        );

    let mut prepared = 0;
    for z in min_z..=max_z {
        for y in min_y..=max_y {
            for x in min_x..=max_x {
                ensure_density_chunk_stored(
                    &noise,
                    preset,
                    preset_id,
                    seed,
                    TerrainChunkCoord { x, y, z },
                    cell_size,
                );
                prepared += 1;
            }
        }
    }

    prepared
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_sample_count() -> u32 {
    TERRAIN_CHUNK_SAMPLE_COUNT as u32
}

#[no_mangle]
pub extern "C" fn ofg_density_chunk_buffer_ptr() -> *const f32 {
    unsafe { core::ptr::addr_of!(DENSITY_CHUNK_BUFFER).cast::<f32>() }
}

#[no_mangle]
pub extern "C" fn ofg_fill_density_chunk(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) {
    if cell_size <= 0.0 {
        return;
    }

    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * cell_size;
    let origin = Vec3 {
        x: chunk_x as f64 * chunk_size,
        y: chunk_y as f64 * chunk_size,
        z: chunk_z as f64 * chunk_size,
    };
    let buffer = unsafe { core::ptr::addr_of_mut!(DENSITY_CHUNK_BUFFER).cast::<f32>() };

    for z in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
        for x in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
            let column_x = origin.x + x as f64 * cell_size;
            let column_z = origin.z + z as f64 * cell_size;
            let macro_sample = sample_macro_terrain(
                &noise,
                preset,
                seed,
                Vec3 {
                    x: column_x,
                    y: 0.0,
                    z: column_z,
                },
            );

            for y in 0..TERRAIN_CHUNK_SAMPLES_PER_AXIS {
                let position = Vec3 {
                    x: column_x,
                    y: origin.y + y as f64 * cell_size,
                    z: column_z,
                };
                let density = density_at_position_with_macro(&noise, preset, position, macro_sample)
                    .density as f32;
                let index = terrain_chunk_sample_index(x, y, z);

                unsafe {
                    *buffer.add(index) = density;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn ofg_build_chunk_mesh(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    unsafe {
        MESH_VERTEX_BUFFER.clear();
        MESH_INDEX_BUFFER.clear();
    }

    if cell_size <= 0.0 {
        return 0;
    }

    let center_coord = TerrainChunkCoord {
        x: chunk_x,
        y: chunk_y,
        z: chunk_z,
    };
    let mesh = build_chunk_mesh(seed, preset, center_coord, cell_size);

    unsafe {
        MESH_VERTEX_BUFFER = mesh.vertices;
        MESH_INDEX_BUFFER = mesh.indices;
        MESH_INDEX_BUFFER.len() as u32
    }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_ptr() -> *const f32 {
    unsafe { MESH_VERTEX_BUFFER.as_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_len() -> u32 {
    unsafe { MESH_VERTEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_ptr() -> *const u32 {
    unsafe { MESH_INDEX_BUFFER.as_ptr() }
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_len() -> u32 {
    unsafe { MESH_INDEX_BUFFER.len() as u32 }
}

#[no_mangle]
pub extern "C" fn ofg_macro_base_elevation_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    sample_macro_terrain(&noise, preset, seed, Vec3 { x, y: 0.0, z }).base_elevation
}

#[no_mangle]
pub extern "C" fn ofg_density_at(seed: u32, preset: u32, x: f64, y: f64, z: f64) -> f64 {
    let noise = SimplexNoise3D::new(seed);
    let preset = terrain_preset(preset);
    density_at_position(&noise, preset, seed, Vec3 { x, y, z }).density
}

#[no_mangle]
pub extern "C" fn ofg_height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    height_at(seed, preset, x, z)
}

fn terrain_stream_status() -> TerrainStreamStatus {
    terrain_stream_scheduler()
        .lock()
        .expect("terrain stream scheduler lock poisoned")
        .status()
}

fn generation_from_f64(generation: f64) -> Option<u64> {
    if !generation.is_finite()
        || generation < 0.0
        || generation > u64::MAX as f64
        || generation.fract() != 0.0
    {
        return None;
    }

    Some(generation as u64)
}

fn write_stream_jobs(jobs: &[TerrainStreamJob]) -> u32 {
    let count = jobs.len().min(STREAM_JOB_BUFFER_CAPACITY);

    for (index, job) in jobs.iter().take(count).enumerate() {
        let (kind, lod, generation, coord) = match *job {
            TerrainStreamJob::Density { generation, key } => {
                (0, u32::from(key.lod), generation, key.coord)
            }
            TerrainStreamJob::Mesh { generation, key } => {
                (1, u32::from(key.lod), generation, key.coord)
            }
        };

        unsafe {
            *core::ptr::addr_of_mut!(STREAM_JOB_KIND_BUFFER)
                .cast::<u32>()
                .add(index) = kind;
            *core::ptr::addr_of_mut!(STREAM_JOB_LOD_BUFFER)
                .cast::<u32>()
                .add(index) = lod;
            *core::ptr::addr_of_mut!(STREAM_JOB_GENERATION_BUFFER)
                .cast::<f64>()
                .add(index) = generation as f64;
            *core::ptr::addr_of_mut!(STREAM_JOB_X_BUFFER)
                .cast::<i32>()
                .add(index) = coord.x;
            *core::ptr::addr_of_mut!(STREAM_JOB_Y_BUFFER)
                .cast::<i32>()
                .add(index) = coord.y;
            *core::ptr::addr_of_mut!(STREAM_JOB_Z_BUFFER)
                .cast::<i32>()
                .add(index) = coord.z;
        }
    }

    count as u32
}

fn write_stream_coords(coords: &[TerrainChunkCoord]) -> u32 {
    let count = coords.len().min(STREAM_COORD_BUFFER_CAPACITY);

    for (index, coord) in coords.iter().take(count).enumerate() {
        unsafe {
            *core::ptr::addr_of_mut!(STREAM_COORD_X_BUFFER)
                .cast::<i32>()
                .add(index) = coord.x;
            *core::ptr::addr_of_mut!(STREAM_COORD_Y_BUFFER)
                .cast::<i32>()
                .add(index) = coord.y;
            *core::ptr::addr_of_mut!(STREAM_COORD_Z_BUFFER)
                .cast::<i32>()
                .add(index) = coord.z;
        }
    }

    count as u32
}
