use std::collections::BTreeSet;

use crate::*;

fn density_store_contains(
    seed: u32,
    preset: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> bool {
    let key = density_chunk_store_key(seed, preset, coord, cell_size);

    density_chunk_store()
        .lock()
        .expect("density chunk store lock poisoned")
        .get(key)
        .is_some()
}

#[test]
fn exported_version_is_stable() {
    let _lock = test_lock();
    assert_eq!(ofg_terrain_core_version(), 1);
    assert_eq!(ofg_terrain_core_preset_count(), 4);
    assert_eq!(ofg_density_chunk_sample_count(), 33 * 33 * 33);
    assert!(ofg_density_chunk_store_max_entries() >= 8);
}

#[test]
fn facade_exposes_stable_buffer_capacities_and_pointers() {
    let _lock = test_lock();

    assert_eq!(
        ofg_stream_vertical_offset_buffer_capacity() as usize,
        STREAM_VERTICAL_OFFSET_BUFFER_CAPACITY
    );
    assert_eq!(
        ofg_stream_job_buffer_capacity() as usize,
        STREAM_JOB_BUFFER_CAPACITY
    );
    assert_eq!(
        ofg_stream_coord_buffer_capacity() as usize,
        STREAM_COORD_BUFFER_CAPACITY
    );
    assert_eq!(
        ofg_terrain_mesh_packet_coord_buffer_capacity() as usize,
        MESH_PACKET_COORD_BUFFER_CAPACITY
    );
    assert_eq!(
        ofg_worker_pool_max_workers() as usize,
        TERRAIN_WORKER_POOL_MAX_WORKERS
    );

    assert!(!ofg_stream_vertical_offset_buffer_ptr().is_null());
    assert!(!ofg_stream_job_kind_buffer_ptr().is_null());
    assert!(!ofg_stream_job_lod_buffer_ptr().is_null());
    assert!(!ofg_stream_job_generation_buffer_ptr().is_null());
    assert!(!ofg_stream_job_x_buffer_ptr().is_null());
    assert!(!ofg_stream_job_y_buffer_ptr().is_null());
    assert!(!ofg_stream_job_z_buffer_ptr().is_null());
    assert!(!ofg_stream_coord_x_buffer_ptr().is_null());
    assert!(!ofg_stream_coord_y_buffer_ptr().is_null());
    assert!(!ofg_stream_coord_z_buffer_ptr().is_null());
    assert!(!ofg_terrain_mesh_packet_lod_buffer_ptr().is_null());
    assert!(!ofg_terrain_mesh_packet_x_buffer_ptr().is_null());
    assert!(!ofg_terrain_mesh_packet_y_buffer_ptr().is_null());
    assert!(!ofg_terrain_mesh_packet_z_buffer_ptr().is_null());
    assert!(!ofg_density_chunk_buffer_ptr().is_null());
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
fn macro_base_elevation_matches_surface_band_for_every_preset() {
    let _lock = test_lock();

    for preset in 0..ofg_terrain_core_preset_count() {
        let macro_elevation = ofg_macro_base_elevation_at(0x0F6, preset, 42.25, -17.5);
        let refined_height = ofg_height_at(0x0F6, preset, 42.25, -17.5);

        assert!(macro_elevation.is_finite());
        assert!(macro_elevation > SURFACE_SEARCH_MIN_Y);
        assert!(macro_elevation < SURFACE_SEARCH_MAX_Y);
        assert!((macro_elevation - refined_height).abs() < 64.0);
    }
}

#[test]
fn height_and_density_samples_are_finite_for_every_preset() {
    let _lock = test_lock();
    let points = [
        (0.0, 0.0),
        (12.5, -20.25),
        (-47.75, 31.5),
        (96.125, -64.875),
    ];
    let mut preset_heights = Vec::new();

    for preset in 0..ofg_terrain_core_preset_count() {
        for (x, z) in points {
            let first_height = ofg_height_at(0x0F6, preset, x, z);
            let second_height = ofg_height_at(0x0F6, preset, x, z);
            let density_at_surface = ofg_density_at(0x0F6, preset, x, first_height, z);
            let density_below = ofg_density_at(0x0F6, preset, x, first_height - 4.0, z);
            let density_above = ofg_density_at(0x0F6, preset, x, first_height + 4.0, z);

            assert!(first_height.is_finite());
            assert_eq!(first_height.to_bits(), second_height.to_bits());
            assert!(density_at_surface.is_finite());
            assert!(density_at_surface.abs() < 0.05);
            assert!(density_below < density_above);
        }

        preset_heights.push(ofg_height_at(0x0F6, preset, 64.0, -96.0));
    }

    preset_heights.sort_by(|a, b| a.total_cmp(b));
    preset_heights.dedup_by(|a, b| (*a - *b).abs() < 0.001);
    assert!(preset_heights.len() > 1);
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
fn fills_density_chunks_deterministically_with_finite_samples() {
    let _lock = test_lock();
    ofg_fill_density_chunk(0x0F6, 3, -1, 0, 2, 1.0);
    let first = unsafe {
        std::slice::from_raw_parts(
            ofg_density_chunk_buffer_ptr(),
            ofg_density_chunk_sample_count() as usize,
        )
    }
    .to_vec();

    ofg_fill_density_chunk(0x0F6, 3, -1, 0, 2, 1.0);
    let second = unsafe {
        std::slice::from_raw_parts(
            ofg_density_chunk_buffer_ptr(),
            ofg_density_chunk_sample_count() as usize,
        )
    };

    assert_eq!(first.len(), TERRAIN_CHUNK_SAMPLE_COUNT);
    assert_eq!(first[0].to_bits(), second[0].to_bits());
    assert_eq!(
        first[first.len() - 1].to_bits(),
        second[second.len() - 1].to_bits()
    );

    let mut finite_samples = 0;
    for index in (0..first.len()).step_by(1024) {
        assert!(first[index].is_finite());
        finite_samples += 1;
    }
    assert!(finite_samples > 20);
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

    for vertex in vertices.chunks_exact(FLOATS_PER_VERTEX) {
        let material_weight_sum: f32 = vertex
            [MATERIAL_WEIGHTS_VERTEX_OFFSET..MATERIAL_WEIGHTS_VERTEX_OFFSET + 4]
            .iter()
            .sum();

        assert!((material_weight_sum - 1.0).abs() <= 0.00001);
    }
}

#[test]
fn build_chunk_mesh_facade_clears_buffers_on_invalid_cell_size() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();
    assert!(ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0) > 0);
    assert!(ofg_mesh_vertex_buffer_len() > 0);
    assert!(ofg_mesh_index_buffer_len() > 0);

    assert_eq!(ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 0.0), 0);

    assert_eq!(ofg_mesh_vertex_buffer_len(), 0);
    assert_eq!(ofg_mesh_index_buffer_len(), 0);
}

#[test]
fn stores_and_loads_terrain_mesh_packets() {
    let _lock = test_lock();
    ofg_reset_terrain_mesh_packet_store();
    let _ = ofg_build_chunk_mesh(0x0F6, 1, 0, 0, 0, 1.0);
    let vertex_len = ofg_mesh_vertex_buffer_len() as usize;
    let index_len = ofg_mesh_index_buffer_len() as usize;
    let expected_vertices =
        unsafe { std::slice::from_raw_parts(ofg_mesh_vertex_buffer_ptr(), vertex_len) }.to_vec();
    let expected_indices =
        unsafe { std::slice::from_raw_parts(ofg_mesh_index_buffer_ptr(), index_len) }.to_vec();

    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(vertex_len as u32, index_len as u32),
        1
    );
    let input_vertices = unsafe {
        std::slice::from_raw_parts_mut(
            ofg_terrain_mesh_packet_input_vertex_buffer_ptr(),
            vertex_len,
        )
    };
    let input_indices = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_input_index_buffer_ptr(), index_len)
    };
    input_vertices.copy_from_slice(&expected_vertices);
    input_indices.copy_from_slice(&expected_indices);

    assert_eq!(ofg_store_terrain_mesh_packet_buffer(0, 0, 0, 0), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_contains(0, 0, 0, 0), 1);
    assert_eq!(ofg_write_terrain_mesh_packet_coords(), 1);

    let lods = unsafe { std::slice::from_raw_parts(ofg_terrain_mesh_packet_lod_buffer_ptr(), 1) };
    let xs = unsafe { std::slice::from_raw_parts(ofg_terrain_mesh_packet_x_buffer_ptr(), 1) };
    let ys = unsafe { std::slice::from_raw_parts(ofg_terrain_mesh_packet_y_buffer_ptr(), 1) };
    let zs = unsafe { std::slice::from_raw_parts(ofg_terrain_mesh_packet_z_buffer_ptr(), 1) };
    assert_eq!((lods[0], xs[0], ys[0], zs[0]), (0, 0, 0, 0));

    assert_eq!(ofg_load_terrain_mesh_packet_buffer(0, 0, 0, 0), 1);
    let loaded_vertices = unsafe {
        std::slice::from_raw_parts(
            ofg_mesh_vertex_buffer_ptr(),
            ofg_mesh_vertex_buffer_len() as usize,
        )
    };
    let loaded_indices = unsafe {
        std::slice::from_raw_parts(
            ofg_mesh_index_buffer_ptr(),
            ofg_mesh_index_buffer_len() as usize,
        )
    };
    assert_eq!(loaded_vertices, expected_vertices);
    assert_eq!(loaded_indices, expected_indices);

    assert_eq!(ofg_remove_terrain_mesh_packet(0, 0, 0, 0), 1);
    assert_eq!(ofg_remove_terrain_mesh_packet(0, 0, 0, 0), 0);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 0);
}

#[test]
fn terrain_mesh_packet_store_replaces_existing_chunk_and_versions_changes() {
    let _lock = test_lock();
    ofg_reset_terrain_mesh_packet_store();
    let initial_version = ofg_terrain_mesh_packet_store_version();
    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(FLOATS_PER_VERTEX as u32, 3),
        1
    );
    let vertices = unsafe {
        std::slice::from_raw_parts_mut(
            ofg_terrain_mesh_packet_input_vertex_buffer_ptr(),
            FLOATS_PER_VERTEX,
        )
    };
    let indices = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_input_index_buffer_ptr(), 3)
    };
    vertices[0] = 1.0;
    indices.copy_from_slice(&[0, 0, 0]);

    assert_eq!(ofg_store_terrain_mesh_packet_buffer(1, 2, 3, 0), 1);
    let inserted_version = ofg_terrain_mesh_packet_store_version();
    assert!(inserted_version > initial_version);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 1);

    vertices[0] = 2.0;
    assert_eq!(ofg_store_terrain_mesh_packet_buffer(1, 2, 3, 0), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 1);
    assert!(ofg_terrain_mesh_packet_store_version() > inserted_version);
    assert_eq!(ofg_load_terrain_mesh_packet_buffer(1, 2, 3, 0), 1);
    assert_eq!(
        unsafe { *ofg_mesh_vertex_buffer_ptr() }.to_bits(),
        2.0f32.to_bits()
    );
}

#[test]
fn terrain_mesh_packet_store_retains_requested_chunks() {
    let _lock = test_lock();
    ofg_reset_terrain_mesh_packet_store();
    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(FLOATS_PER_VERTEX as u32, 3),
        1
    );
    let indices = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_input_index_buffer_ptr(), 3)
    };
    indices.copy_from_slice(&[0, 0, 0]);

    assert_eq!(ofg_store_terrain_mesh_packet_buffer(0, 0, 0, 0), 1);
    assert_eq!(ofg_store_terrain_mesh_packet_buffer(1, 0, 0, 0), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 2);

    let lods = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_lod_buffer_ptr() as *mut u32, 1)
    };
    let xs = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_x_buffer_ptr() as *mut i32, 1)
    };
    let ys = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_y_buffer_ptr() as *mut i32, 1)
    };
    let zs = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_z_buffer_ptr() as *mut i32, 1)
    };
    lods[0] = 0;
    xs[0] = 1;
    ys[0] = 0;
    zs[0] = 0;

    assert_eq!(ofg_retain_terrain_mesh_packets(1), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_contains(0, 0, 0, 0), 0);
    assert_eq!(ofg_terrain_mesh_packet_store_contains(1, 0, 0, 0), 1);

    assert_eq!(ofg_retain_terrain_mesh_packets(0), 1);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 0);
}

#[test]
fn terrain_mesh_packet_store_rejects_invalid_meshes() {
    let _lock = test_lock();
    ofg_reset_terrain_mesh_packet_store();

    assert_eq!(ofg_prepare_terrain_mesh_packet_input(0, 3), 0);
    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(FLOATS_PER_VERTEX as u32, 0),
        0
    );
    assert_eq!(ofg_prepare_terrain_mesh_packet_input(1, 3), 0);
    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(FLOATS_PER_VERTEX as u32, 2),
        0
    );
    assert_eq!(
        ofg_prepare_terrain_mesh_packet_input(FLOATS_PER_VERTEX as u32, 3),
        1
    );

    let indices = unsafe {
        std::slice::from_raw_parts_mut(ofg_terrain_mesh_packet_input_index_buffer_ptr(), 3)
    };
    indices.copy_from_slice(&[0, 1, 0]);

    assert_eq!(ofg_store_terrain_mesh_packet_buffer(0, 0, 0, 0), 0);
    assert_eq!(ofg_terrain_mesh_packet_store_entry_count(), 0);
    assert_eq!(ofg_store_terrain_mesh_packet_buffer(0, 0, 0, u32::MAX), 0);
    assert_eq!(ofg_load_terrain_mesh_packet_buffer(0, 0, 0, 0), 0);
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
fn prunes_stored_density_chunks_to_window() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();
    ofg_fill_density_chunk(0x0F6, 1, 0, 0, 0, 1.0);
    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, 0, 0, 0, 1.0), 1);
    ofg_fill_density_chunk(0x0F6, 1, 4, 0, 0, 1.0);
    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, 4, 0, 0, 1.0), 1);

    assert_eq!(ofg_density_chunk_store_entry_count(), 2);
    assert_eq!(
        ofg_retain_density_chunk_store_window(0x0F6, 1, 0, 0, 0, 1, 1, 1, 1.0),
        1
    );
    assert!(density_store_contains(0x0F6, 1, coord(0, 0, 0), 1.0));
    assert!(!density_store_contains(0x0F6, 1, coord(4, 0, 0), 1.0));
}

#[test]
fn density_store_facade_rejects_invalid_cell_sizes_and_counts_evictions() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();

    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, 0, 0, 0, 0.0), 0);
    assert_eq!(
        ofg_retain_density_chunk_store_window(0x0F6, 1, 0, 0, 0, 1, 1, 1, -1.0),
        0
    );
    assert_eq!(
        ofg_prepare_density_chunk_window(0x0F6, 1, 0, 0, 0, 1, 1, 1, 0.0),
        0
    );
    assert_eq!(ofg_density_chunk_store_entry_count(), 0);

    ofg_fill_density_chunk(0x0F6, 1, -2, -2, -2, 1.0);
    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, -2, -2, -2, 1.0), 1);
    ofg_fill_density_chunk(0x0F6, 1, 2, 2, 2, 1.0);
    assert_eq!(ofg_store_density_chunk_buffer(0x0F6, 1, 2, 2, 2, 1.0), 1);
    assert_eq!(ofg_density_chunk_store_entry_count(), 2);

    assert_eq!(
        ofg_retain_density_chunk_store_window(0x0F6, 1, 1, 1, 1, -3, -3, -3, 1.0),
        1
    );
    assert!(density_store_contains(0x0F6, 1, coord(-2, -2, -2), 1.0));
    assert!(!density_store_contains(0x0F6, 1, coord(2, 2, 2), 1.0));
    assert_eq!(ofg_density_chunk_store_eviction_count(), 1.0);
}

#[test]
fn density_store_facade_evicts_oldest_chunks_when_capacity_is_exceeded() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();
    ofg_fill_density_chunk(0x0F6, 1, 0, 0, 0, 1.0);

    for x in 0..=ofg_density_chunk_store_max_entries() {
        assert_eq!(
            ofg_store_density_chunk_buffer(0x0F6, 1, x as i32, 0, 0, 1.0),
            1
        );
    }

    assert_eq!(
        ofg_density_chunk_store_entry_count(),
        ofg_density_chunk_store_max_entries()
    );
    assert_eq!(ofg_density_chunk_store_eviction_count(), 1.0);
    assert!(!density_store_contains(0x0F6, 1, coord(0, 0, 0), 1.0));
}

#[test]
fn terrain_node_parent_maps_negative_coords_with_floor_division() {
    assert_eq!(
        terrain_node_parent(node(0, -1, -2, -3)),
        Some(node(1, -1, -1, -2))
    );
    assert_eq!(
        terrain_node_coord_for_lod(coord(-1, -2, -3), 1),
        coord(-1, -1, -2)
    );
    assert_eq!(terrain_node_cell_size(1.0, 3), 8.0);
    assert_eq!(terrain_node_key(node(2, -1, 0, 4)), "lod2:-1,0,4");

    let children = terrain_node_children(node(2, -1, 0, 1)).unwrap();
    assert_eq!(children[0], node(1, -2, 0, 2));
    assert_eq!(children[7], node(1, -1, 1, 3));
    assert_eq!(terrain_node_children(node(0, 0, 0, 0)), None);
}

#[test]
fn build_node_mesh_scales_cell_size_for_coarser_lod() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();

    let node_mesh = build_node_mesh(0x0F6, 1, node(1, 0, 0, 0), 1.0);
    let direct_mesh = build_chunk_mesh(0x0F6, 1, coord(0, 0, 0), 2.0);

    assert_eq!(node_mesh.vertices, direct_mesh.vertices);
    assert_eq!(node_mesh.indices, direct_mesh.indices);

    ofg_reset_density_chunk_store();
}

#[test]
fn same_lod_neighbor_meshes_reuse_identical_apron_vertices() {
    let _lock = test_lock();
    ofg_reset_density_chunk_store();

    assert_matching_neighbor_vertex_strip(
        build_node_mesh(0x0F6, 3, node(0, 0, 0, 0), 1.0),
        build_node_mesh(0x0F6, 3, node(0, 1, 0, 0), 1.0),
        Axis3::X,
        32.0,
        1.0,
    );
    assert_matching_neighbor_vertex_strip(
        build_node_mesh(0x0F6, 3, node(2, 0, 0, 0), 1.0),
        build_node_mesh(0x0F6, 3, node(2, 0, 0, 1), 1.0),
        Axis3::Z,
        128.0,
        4.0,
    );

    ofg_reset_density_chunk_store();
}

#[test]
fn stream_scheduler_builds_rootless_lod_bands_without_a_root_node() {
    let mut scheduler = test_stream_scheduler_with_bands(
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![0],
            },
        ],
        16,
    );

    scheduler.sync_center(coord(0, 0, 0));
    let desired = scheduler.desired_mesh_nodes();

    assert_eq!(desired.len(), 17);
    assert!(desired.contains(&node(0, 0, 0, 0)));
    assert!(terrain_node_children(node(1, 0, 0, 0))
        .unwrap()
        .iter()
        .all(|child| desired.contains(child)));
    assert!(desired.contains(&node(1, -1, 0, -1)));
    assert!(desired.contains(&node(1, 1, 0, 1)));
    assert!(!desired.contains(&node(2, 0, 0, 0)));
    assert!(scheduler
        .desired_density_nodes()
        .iter()
        .any(|key| key.lod == 1));
    assert_eq!(scheduler.status().desired_lod0_count, 8);
    assert_eq!(scheduler.status().desired_mesh_count, 17);
    assert_eq!(scheduler.status().lod_summaries.len(), 2);
}

#[test]
fn stream_scheduler_schedules_parent_mesh_before_child_mesh() {
    let mut scheduler = test_stream_scheduler_with_bands(
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
        ],
        8,
    );

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation,
        key: parent_key,
    }] = scheduler.tick()[..]
    else {
        panic!("expected parent build job before child builds");
    };
    assert_eq!(parent_key, node(1, 0, 0, 0));
    assert!(scheduler.complete_node(generation, parent_key, false));

    let child_build_jobs = scheduler.tick();
    assert_eq!(child_build_jobs.len(), 8);
    assert!(child_build_jobs.iter().all(|job| {
        matches!(
            job,
            TerrainStreamJob::BuildNode {
                key: TerrainNodeKey { lod: 0, .. },
                ..
            }
        )
    }));
}

#[test]
fn stream_scheduler_treats_empty_parent_mesh_as_generated_cover() {
    let mut scheduler = test_stream_scheduler_with_bands(
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
        ],
        8,
    );

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation,
        key: parent_key,
    }] = scheduler.tick()[..]
    else {
        panic!("expected parent build job");
    };
    assert!(scheduler.complete_node(generation, parent_key, true));
    assert_eq!(
        scheduler.node_stage(parent_key),
        TerrainChunkStage::MeshEmpty { lod: 1 }
    );

    let child_build_jobs = scheduler.tick();
    assert_eq!(child_build_jobs.len(), 8);
    assert!(child_build_jobs.iter().all(|job| {
        matches!(
            job,
            TerrainStreamJob::BuildNode {
                key: TerrainNodeKey { lod: 0, .. },
                ..
            }
        )
    }));
}

#[test]
fn stream_scheduler_builds_lod0_targets() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 8);

    scheduler.sync_center(coord(0, 0, 0));

    assert_eq!(scheduler.desired_lod0_coords(), vec![coord(0, 0, 0)]);
    assert_eq!(scheduler.desired_density_coords(), vec![coord(0, 0, 0)]);
    assert!(scheduler.desired_density_coords().contains(&coord(0, 0, 0)));
    assert_eq!(scheduler.status().desired_lod0_count, 1);
    assert_eq!(scheduler.status().missing_density_count, 0);
    assert_eq!(scheduler.status().missing_lod0_count, 1);
}

#[test]
fn stream_scheduler_submits_nearest_build_jobs_first_up_to_capacity() {
    let mut scheduler = test_stream_scheduler(1, vec![0], 2);

    scheduler.sync_center(coord(0, 0, 0));
    let jobs = scheduler.tick();

    assert_eq!(jobs.len(), 2);
    assert_eq!(
        jobs[0],
        TerrainStreamJob::BuildNode {
            generation: 0,
            key: node(0, 0, 0, 0)
        }
    );
    assert!(jobs
        .iter()
        .all(|job| matches!(job, TerrainStreamJob::BuildNode { generation: 0, .. })));
    assert_eq!(scheduler.status().in_flight_density_count, 0);
    assert_eq!(scheduler.status().in_flight_lod_count, 2);
}

#[test]
fn stream_scheduler_updates_max_in_flight_capacity() {
    let mut scheduler = test_stream_scheduler(1, vec![0], 1);

    assert_eq!(
        scheduler.set_max_in_flight_jobs(0),
        Err(TerrainStreamError::ZeroMaxInFlightJobs)
    );
    scheduler.set_max_in_flight_jobs(3).unwrap();
    scheduler.sync_center(coord(0, 0, 0));

    assert_eq!(scheduler.tick().len(), 3);
    assert_eq!(scheduler.status().max_in_flight_jobs, 3);
}

#[test]
fn stream_scheduler_does_not_submit_children_until_parent_is_generated() {
    let mut scheduler = test_stream_scheduler_with_bands(
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
        ],
        16,
    );

    scheduler.sync_center(coord(0, 0, 0));
    let jobs = scheduler.tick();
    assert_eq!(jobs.len(), 1);
    assert!(matches!(
        jobs[0],
        TerrainStreamJob::BuildNode {
            key: TerrainNodeKey { lod: 1, .. },
            ..
        }
    ));

    assert!(scheduler.tick().is_empty());
    let TerrainStreamJob::BuildNode { generation, key } = jobs[0];
    assert!(scheduler.complete_node(generation, key, false));

    let child_jobs = scheduler.tick();
    assert_eq!(child_jobs.len(), 8);
    assert!(child_jobs.iter().all(|job| matches!(
        job,
        TerrainStreamJob::BuildNode {
            key: TerrainNodeKey { lod: 0, .. },
            ..
        }
    )));
}

#[test]
fn stream_scheduler_records_ready_and_empty_lod0_chunks() {
    let mut ready_scheduler = test_stream_scheduler(0, vec![0], 8);
    ready_scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation,
        key: ready_key,
    }] = ready_scheduler.tick()[..]
    else {
        panic!("expected one build job");
    };

    assert!(ready_scheduler.complete_node(generation, ready_key, false));
    assert_eq!(
        ready_scheduler.node_stage(ready_key),
        TerrainChunkStage::MeshReady { lod: 0 }
    );
    assert_eq!(ready_scheduler.status().lod0_ready_count, 1);

    let mut empty_scheduler = test_stream_scheduler(0, vec![0], 8);
    empty_scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation,
        key: empty_key,
    }] = empty_scheduler.tick()[..]
    else {
        panic!("expected one build job");
    };

    assert!(empty_scheduler.complete_node(generation, empty_key, true));
    assert_eq!(
        empty_scheduler.node_stage(empty_key),
        TerrainChunkStage::MeshEmpty { lod: 0 }
    );
    assert_eq!(empty_scheduler.status().lod0_empty_count, 1);
}

#[test]
fn stream_scheduler_reset_rejects_stale_build_results() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 1);

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation: old_generation,
        key: old_key,
    }] = scheduler.tick()[..]
    else {
        panic!("expected one build job");
    };

    scheduler.reset(coord(0, 0, 0));
    assert_eq!(scheduler.generation(), old_generation + 1);
    let [TerrainStreamJob::BuildNode {
        generation: new_generation,
        key: new_key,
    }] = scheduler.tick()[..]
    else {
        panic!("expected one replacement build job");
    };

    assert_eq!(old_key, new_key);
    assert!(!scheduler.complete_node(old_generation, old_key, false));
    assert_eq!(
        scheduler.node_stage(new_key),
        TerrainChunkStage::BuildInFlight {
            lod: 0,
            generation: new_generation
        }
    );
    assert!(scheduler.complete_node(new_generation, new_key, false));
    assert_eq!(
        scheduler.node_stage(new_key),
        TerrainChunkStage::MeshReady { lod: 0 }
    );
}

#[test]
fn stream_scheduler_prunes_chunks_outside_the_current_window() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 8);

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode {
        generation,
        key: mesh_key,
    }] = scheduler.tick()[..]
    else {
        panic!("expected one build job");
    };
    assert!(scheduler.complete_node(generation, mesh_key, false));

    scheduler.sync_center(coord(4, 0, 0));

    assert_eq!(
        scheduler.chunk_stage(coord(0, 0, 0)),
        TerrainChunkStage::NotPresent
    );
    assert_eq!(scheduler.desired_lod0_coords(), vec![coord(4, 0, 0)]);
    assert_eq!(scheduler.desired_density_coords(), vec![coord(4, 0, 0)]);
}

#[test]
fn stream_scheduler_failed_build_jobs_can_be_retried() {
    let mut scheduler = test_stream_scheduler(0, vec![0], 1);

    scheduler.sync_center(coord(0, 0, 0));
    let [TerrainStreamJob::BuildNode { generation, key }] = scheduler.tick()[..] else {
        panic!("expected one build job");
    };

    assert!(scheduler.fail_node(generation, key));
    assert_eq!(scheduler.node_stage(key), TerrainChunkStage::NotPresent);
    assert_eq!(
        scheduler.tick(),
        vec![TerrainStreamJob::BuildNode { generation, key }]
    );
}

#[test]
fn stream_scheduler_validates_configuration() {
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands: Vec::new(),
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::EmptyLodBands)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands: vec![
                TerrainLodBand {
                    lod: 0,
                    horizontal_radius: 0,
                    vertical_chunk_offsets: vec![0],
                },
                TerrainLodBand {
                    lod: 0,
                    horizontal_radius: 1,
                    vertical_chunk_offsets: vec![0],
                },
            ],
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::DuplicateLodBands)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands: vec![TerrainLodBand {
                lod: 0,
                horizontal_radius: -1,
                vertical_chunk_offsets: vec![0],
            }],
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::NegativeHorizontalRadius)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands: vec![TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: Vec::new(),
            }],
            ..TerrainStreamConfig::default()
        })
        .err(),
        Some(TerrainStreamError::EmptyVerticalOffsets)
    );
    assert_eq!(
        TerrainStreamScheduler::new(TerrainStreamConfig {
            lod_bands: vec![TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0, 0],
            }],
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

#[test]
fn terrain_stream_config_helpers_and_errors_are_stable() {
    let default_config = TerrainStreamConfig::default();
    assert_eq!(
        default_config,
        TerrainStreamConfig::single_lod0(1, vec![-1, 0, 1], 1)
    );

    let messages = [
        (
            TerrainStreamError::EmptyLodBands,
            "empty terrain stream LOD bands",
        ),
        (
            TerrainStreamError::DuplicateLodBands,
            "duplicate terrain stream LOD bands",
        ),
        (
            TerrainStreamError::NegativeHorizontalRadius,
            "negative terrain stream horizontal radius",
        ),
        (
            TerrainStreamError::EmptyVerticalOffsets,
            "empty terrain stream vertical offsets",
        ),
        (
            TerrainStreamError::DuplicateVerticalOffsets,
            "duplicate terrain stream vertical offsets",
        ),
        (
            TerrainStreamError::ZeroMaxInFlightJobs,
            "zero terrain stream max in-flight jobs",
        ),
    ];

    for (error, message) in messages {
        assert_eq!(error.to_string(), message);
    }
}

#[test]
fn stream_scheduler_facade_ticks_and_completes_jobs_through_buffers() {
    let _lock = test_lock();
    let offsets =
        unsafe { std::slice::from_raw_parts_mut(ofg_stream_vertical_offset_buffer_ptr(), 1) };
    offsets[0] = 0;

    assert_eq!(ofg_stream_configure(0, 1, 8), 1);
    ofg_stream_sync_center(0, 0, 0);

    assert_eq!(ofg_stream_write_desired_density_coords(), 1);
    let desired_xs = unsafe { std::slice::from_raw_parts(ofg_stream_coord_x_buffer_ptr(), 1) };
    let desired_ys = unsafe { std::slice::from_raw_parts(ofg_stream_coord_y_buffer_ptr(), 1) };
    let desired_zs = unsafe { std::slice::from_raw_parts(ofg_stream_coord_z_buffer_ptr(), 1) };
    assert_eq!((desired_xs[0], desired_ys[0], desired_zs[0]), (0, 0, 0));

    assert_eq!(ofg_stream_write_lod0_dependency_coords(3, -2, 5), 8);
    let dependency_xs = unsafe { std::slice::from_raw_parts(ofg_stream_coord_x_buffer_ptr(), 8) };
    let dependency_ys = unsafe { std::slice::from_raw_parts(ofg_stream_coord_y_buffer_ptr(), 8) };
    let dependency_zs = unsafe { std::slice::from_raw_parts(ofg_stream_coord_z_buffer_ptr(), 8) };
    assert_eq!(
        (dependency_xs[0], dependency_ys[0], dependency_zs[0]),
        (3, -2, 5)
    );
    assert_eq!(
        (dependency_xs[7], dependency_ys[7], dependency_zs[7]),
        (4, -1, 6)
    );

    let build_job_count = ofg_stream_tick();
    assert_eq!(build_job_count, 1);
    assert_eq!(ofg_stream_status_in_flight_density_count(), 0);
    assert_eq!(ofg_stream_status_in_flight_lod_count(), 1);

    let job_kinds = unsafe {
        std::slice::from_raw_parts(ofg_stream_job_kind_buffer_ptr(), build_job_count as usize)
    };
    let job_generations = unsafe {
        std::slice::from_raw_parts(
            ofg_stream_job_generation_buffer_ptr(),
            build_job_count as usize,
        )
    };
    let job_xs = unsafe {
        std::slice::from_raw_parts(ofg_stream_job_x_buffer_ptr(), build_job_count as usize)
    };
    let job_ys = unsafe {
        std::slice::from_raw_parts(ofg_stream_job_y_buffer_ptr(), build_job_count as usize)
    };
    let job_zs = unsafe {
        std::slice::from_raw_parts(ofg_stream_job_z_buffer_ptr(), build_job_count as usize)
    };

    assert_eq!(job_kinds[0], 1);
    assert_eq!(
        ofg_stream_complete_lod0(job_generations[0], job_xs[0], job_ys[0], job_zs[0], 0),
        1
    );
    assert_eq!(ofg_stream_status_lod0_ready_count(), 1);
}

#[test]
fn stream_scheduler_facade_rejects_invalid_config_and_stale_results() {
    let _lock = test_lock();
    let offsets =
        unsafe { std::slice::from_raw_parts_mut(ofg_stream_vertical_offset_buffer_ptr(), 1) };
    offsets[0] = 0;

    assert_eq!(ofg_stream_configure(0, 1, 2), 1);
    assert_eq!(ofg_stream_status_max_in_flight_jobs(), 2);
    assert_eq!(ofg_stream_configure(0, 1, 0), 0);
    assert_eq!(ofg_stream_status_max_in_flight_jobs(), 2);
    assert_eq!(
        ofg_stream_configure(0, 1, ofg_stream_job_buffer_capacity() + 1),
        0
    );
    assert_eq!(
        ofg_stream_configure(0, ofg_stream_vertical_offset_buffer_capacity() + 1, 1),
        0
    );
    assert_eq!(ofg_stream_configure(-1, 1, 1), 0);

    ofg_stream_sync_center(0, 0, 0);
    assert_eq!(ofg_stream_status_desired_lod0_count(), 1);
    assert_eq!(ofg_stream_status_desired_density_count(), 1);
    assert_eq!(ofg_stream_status_missing_density_count(), 0);
    assert_eq!(ofg_stream_status_missing_lod0_count(), 1);

    let job_count = ofg_stream_tick();
    assert_eq!(job_count, 1);
    assert_eq!(ofg_stream_status_in_flight_density_count(), 0);
    assert_eq!(ofg_stream_status_in_flight_lod_count(), 1);
    let generations =
        unsafe { std::slice::from_raw_parts(ofg_stream_job_generation_buffer_ptr(), 1) };
    let xs = unsafe { std::slice::from_raw_parts(ofg_stream_job_x_buffer_ptr(), 1) };
    let ys = unsafe { std::slice::from_raw_parts(ofg_stream_job_y_buffer_ptr(), 1) };
    let zs = unsafe { std::slice::from_raw_parts(ofg_stream_job_z_buffer_ptr(), 1) };
    let stale_generation = generations[0];
    let stale_coord = (xs[0], ys[0], zs[0]);

    ofg_stream_reset(0, 0, 0);
    assert!(ofg_stream_generation() > stale_generation);
    assert_eq!(
        ofg_stream_complete_density(
            stale_generation,
            stale_coord.0,
            stale_coord.1,
            stale_coord.2
        ),
        0
    );
    assert_eq!(
        ofg_stream_fail_density(f64::NAN, stale_coord.0, stale_coord.1, stale_coord.2),
        0
    );
    assert_eq!(ofg_stream_complete_lod0(f64::INFINITY, 0, 0, 0, 0), 0);
    assert_eq!(ofg_stream_fail_lod0(f64::NEG_INFINITY, 0, 0, 0), 0);

    ofg_stream_invalidate_all();
    assert_eq!(ofg_stream_status_desired_lod0_count(), 0);
    assert_eq!(ofg_stream_status_desired_density_count(), 0);
    assert_eq!(ofg_stream_status_density_ready_count(), 0);
    assert_eq!(ofg_stream_status_lod0_ready_count(), 0);
    assert_eq!(ofg_stream_status_lod0_empty_count(), 0);
}

#[test]
fn worker_pool_assigns_slots_and_rejects_work_when_full() {
    let mut pool = TerrainWorkerPool::default();
    pool.configure(2)
        .expect("worker pool config should be valid");

    let first = pool
        .begin_task(TerrainWorkerTaskKind::Density, 0, 7, coord(0, 0, 0))
        .expect("first worker task should start");
    let second = pool
        .begin_task(TerrainWorkerTaskKind::Lod, 0, 7, coord(1, 0, 0))
        .expect("second worker task should start");

    assert_eq!(first.request_id, 1);
    assert_eq!(first.worker_index, 0);
    assert_eq!(second.request_id, 2);
    assert_eq!(second.worker_index, 1);
    assert_eq!(pool.in_flight_count(), 2);
    assert_eq!(
        pool.begin_task(TerrainWorkerTaskKind::Density, 0, 7, coord(2, 0, 0))
            .err(),
        Some(TerrainWorkerPoolError::NoIdleWorkers)
    );
}

#[test]
fn worker_pool_validates_completion_and_reset_generations() {
    let mut pool = TerrainWorkerPool::default();
    pool.configure(1)
        .expect("worker pool config should be valid");
    let task = pool
        .begin_task(TerrainWorkerTaskKind::Density, 0, 3, coord(0, 0, 0))
        .expect("worker task should start");

    assert_eq!(
        pool.finish_task(
            task.request_id,
            TerrainWorkerTaskKind::Density,
            0,
            99,
            coord(0, 0, 0)
        ),
        TerrainWorkerTaskFinish::Mismatched
    );
    assert_eq!(pool.in_flight_count(), 0);

    let task = pool
        .begin_task(TerrainWorkerTaskKind::Lod, 0, 4, coord(1, 0, 0))
        .expect("replacement worker task should start");
    pool.reset();
    assert_eq!(
        pool.finish_task(
            task.request_id,
            TerrainWorkerTaskKind::Lod,
            0,
            4,
            coord(1, 0, 0)
        ),
        TerrainWorkerTaskFinish::Stale
    );
    assert_eq!(pool.in_flight_count(), 0);
}

#[test]
fn worker_pool_facade_records_task_metadata() {
    let _lock = test_lock();

    assert_eq!(ofg_worker_pool_configure(2), 1);
    assert_eq!(ofg_worker_pool_worker_count(), 2);
    let runtime_generation = ofg_worker_pool_runtime_generation();

    assert_eq!(ofg_worker_pool_begin_task(0, 0, 5.0, 2, 0, -1), 1);
    assert_eq!(ofg_worker_pool_task_request_id(), 1);
    assert_eq!(ofg_worker_pool_task_worker_index(), 0);
    assert_eq!(
        ofg_worker_pool_task_runtime_generation(),
        runtime_generation
    );
    assert_eq!(ofg_worker_pool_in_flight_count(), 1);

    assert_eq!(ofg_worker_pool_begin_task(1, 0, 5.0, 3, 0, -1), 1);
    assert_eq!(ofg_worker_pool_task_request_id(), 2);
    assert_eq!(ofg_worker_pool_task_worker_index(), 1);
    assert_eq!(ofg_worker_pool_in_flight_count(), 2);
    assert_eq!(ofg_worker_pool_begin_task(0, 0, 5.0, 4, 0, -1), 0);

    assert_eq!(ofg_worker_pool_finish_task(1, 0, 0, 5.0, 2, 0, -1), 1);
    assert_eq!(ofg_worker_pool_finish_task(2, 1, 0, 6.0, 3, 0, -1), 2);
    assert_eq!(ofg_worker_pool_finish_task(2, 1, 0, 5.0, 3, 0, -1), 0);

    assert_eq!(ofg_worker_pool_begin_task(0, 0, 7.0, 4, 0, -1), 1);
    let stale_request_id = ofg_worker_pool_task_request_id();
    ofg_worker_pool_reset();
    assert_eq!(ofg_worker_pool_in_flight_count(), 0);
    assert_eq!(
        ofg_worker_pool_finish_task(stale_request_id, 0, 0, 7.0, 4, 0, -1),
        0
    );
}

#[test]
fn worker_pool_facade_rejects_invalid_inputs_and_failed_tasks() {
    let _lock = test_lock();

    assert_eq!(ofg_worker_pool_configure(0), 0);
    assert_eq!(
        ofg_worker_pool_configure(ofg_worker_pool_max_workers() + 1),
        0
    );
    assert_eq!(ofg_worker_pool_configure(1), 1);
    assert_eq!(ofg_worker_pool_worker_count(), 1);

    assert_eq!(ofg_worker_pool_begin_task(99, 0, 1.0, 0, 0, 0), 0);
    assert_eq!(ofg_worker_pool_begin_task(0, 0, f64::NAN, 0, 0, 0), 0);
    assert_eq!(ofg_worker_pool_begin_task(0, u32::MAX, 1.0, 0, 0, 0), 0);
    assert_eq!(ofg_worker_pool_in_flight_count(), 0);

    assert_eq!(ofg_worker_pool_begin_task(0, 0, 1.0, 0, 0, 0), 1);
    let request_id = ofg_worker_pool_task_request_id();
    assert_eq!(ofg_worker_pool_in_flight_count(), 1);
    assert_eq!(ofg_worker_pool_fail_task(request_id), 1);
    assert_eq!(ofg_worker_pool_fail_task(request_id), 0);
    assert_eq!(ofg_worker_pool_in_flight_count(), 0);

    assert_eq!(ofg_worker_pool_finish_task(0, 99, 0, 1.0, 0, 0, 0), 2);
    assert_eq!(ofg_worker_pool_finish_task(0, 0, 0, f64::NAN, 0, 0, 0), 2);
}

fn test_stream_scheduler(
    horizontal_radius: i32,
    vertical_chunk_offsets: Vec<i32>,
    max_in_flight_jobs: usize,
) -> TerrainStreamScheduler {
    TerrainStreamScheduler::new(TerrainStreamConfig::single_lod0(
        horizontal_radius,
        vertical_chunk_offsets,
        max_in_flight_jobs,
    ))
    .expect("test stream config should be valid")
}

fn test_stream_scheduler_with_bands(
    lod_bands: Vec<TerrainLodBand>,
    max_in_flight_jobs: usize,
) -> TerrainStreamScheduler {
    TerrainStreamScheduler::new(TerrainStreamConfig {
        lod_bands,
        max_in_flight_jobs,
    })
    .expect("test stream config should be valid")
}

fn coord(x: i32, y: i32, z: i32) -> TerrainChunkCoord {
    TerrainChunkCoord { x, y, z }
}

fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
    TerrainNodeKey {
        lod,
        coord: coord(x, y, z),
    }
}

#[derive(Clone, Copy)]
enum Axis3 {
    X,
    Z,
}

fn assert_matching_neighbor_vertex_strip(
    low_mesh: MeshData,
    high_mesh: MeshData,
    axis: Axis3,
    boundary: f32,
    cell_size: f32,
) {
    let low_strip = vertex_positions_in_strip(&low_mesh, axis, boundary, boundary + cell_size);
    let high_strip = vertex_positions_in_strip(&high_mesh, axis, boundary, boundary + cell_size);

    assert!(!low_strip.is_empty());
    assert!(low_strip.is_subset(&high_strip));
}

fn vertex_positions_in_strip(
    mesh: &MeshData,
    axis: Axis3,
    min: f32,
    max: f32,
) -> BTreeSet<(i32, i32, i32)> {
    mesh.vertices
        .chunks_exact(FLOATS_PER_VERTEX)
        .filter_map(|vertex| {
            let axis_value = match axis {
                Axis3::X => vertex[0],
                Axis3::Z => vertex[2],
            };
            if axis_value < min || axis_value >= max {
                return None;
            }

            Some((
                quantized_position(vertex[0]),
                quantized_position(vertex[1]),
                quantized_position(vertex[2]),
            ))
        })
        .collect()
}

fn quantized_position(value: f32) -> i32 {
    (value * 10_000.0).round() as i32
}
