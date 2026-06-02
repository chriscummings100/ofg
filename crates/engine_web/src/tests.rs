use crate::{
    build_frame_packet_from_engine_snapshot, build_frame_uniform_values, build_material_packet,
    build_object_uniform_values, build_player_marker_world_matrix, MaterialPacketError,
    RenderPacketError, RenderUniformError, RendererState, RendererStateError, ResourceHandle,
    ENGINE_RENDER_SNAPSHOT_FLOATS, FRAME_PACKET_FLOATS, MATERIAL_PACKET_FLOATS,
    REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
    WORLD_MATRIX_FLOATS,
};

#[test]
fn config_rejects_canvas_and_texture_limits_that_webgpu_terrain_cannot_use() {
    let mut renderer = RendererState::new();

    assert_eq!(
        renderer.configure(0, 720, REQUIRED_TEXTURE_ARRAY_LAYERS),
        Err(RendererStateError::InvalidCanvasSize)
    );
    assert_eq!(
        renderer.configure(1280, 720, REQUIRED_TEXTURE_ARRAY_LAYERS - 1),
        Err(RendererStateError::InsufficientTextureArrayLayers)
    );
    assert_eq!(
        renderer.configure(1280, 720, REQUIRED_TEXTURE_ARRAY_LAYERS),
        Ok(())
    );
    assert!(renderer.is_configured());
    assert_eq!(renderer.config().unwrap().canvas_width(), 1280);
}

#[test]
fn mesh_handles_are_generational_and_stale_handles_are_rejected() {
    let mut renderer = configured_renderer();

    let first = renderer
        .register_mesh(TERRAIN_VERTEX_FLOATS * 3, 3, TERRAIN_VERTEX_FLOATS)
        .unwrap();
    assert_eq!(first.slot(), 0);
    assert_eq!(first.generation(), 0);
    assert_eq!(renderer.resource_counts().meshes, 1);

    assert!(renderer.unregister_mesh(first).is_ok());
    assert_eq!(renderer.resource_counts().meshes, 0);
    assert_eq!(
        renderer.unregister_mesh(first),
        Err(RendererStateError::StaleHandle)
    );

    let second = renderer
        .register_mesh(TERRAIN_VERTEX_FLOATS * 6, 6, TERRAIN_VERTEX_FLOATS)
        .unwrap();
    assert_eq!(second.slot(), first.slot());
    assert_eq!(second.generation(), first.generation() + 1);
}

#[test]
fn mesh_registration_validates_the_renderer_vertex_contract() {
    let mut renderer = configured_renderer();

    assert_eq!(
        renderer.register_mesh(18, 3, 18),
        Err(RendererStateError::InvalidMesh)
    );
    assert_eq!(
        renderer.register_mesh(TERRAIN_VERTEX_FLOATS * 3 - 1, 3, TERRAIN_VERTEX_FLOATS),
        Err(RendererStateError::InvalidMesh)
    );
    assert_eq!(
        renderer.register_mesh(TERRAIN_VERTEX_FLOATS * 3, 4, TERRAIN_VERTEX_FLOATS),
        Err(RendererStateError::InvalidMesh)
    );
}

#[test]
fn texture_registration_uses_configured_array_layer_limits() {
    let mut renderer = configured_renderer();

    let texture = renderer
        .register_texture(
            64,
            64,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
            TEXTURE_FORMAT_RGBA8_UNORM,
        )
        .unwrap();
    assert_eq!(renderer.resource_counts().textures, 1);
    assert!(renderer.unregister_texture(texture).is_ok());
    assert_eq!(renderer.resource_counts().textures, 0);

    assert_eq!(
        renderer.register_texture(
            64,
            64,
            REQUIRED_TEXTURE_ARRAY_LAYERS + 1,
            TEXTURE_FORMAT_RGBA8_UNORM
        ),
        Err(RendererStateError::InvalidTexture)
    );
    assert_eq!(
        renderer.register_texture(64, 64, 1, 999),
        Err(RendererStateError::UnsupportedTextureFormat)
    );
}

#[test]
fn frames_count_only_draws_with_live_mesh_and_object_handles() {
    let mut renderer = configured_renderer();
    let mesh = renderer
        .register_mesh(TERRAIN_VERTEX_FLOATS * 3, 3, TERRAIN_VERTEX_FLOATS)
        .unwrap();
    let object = renderer.register_object().unwrap();

    assert_eq!(renderer.begin_frame(1920, 1080), Ok(()));
    assert_eq!(renderer.frame_index(), 1);
    assert_eq!(renderer.frame_draw_count(), 0);
    assert_eq!(renderer.note_draw(mesh, object), Ok(()));
    assert_eq!(renderer.frame_draw_count(), 1);

    assert_eq!(
        renderer.note_draw(ResourceHandle::new(99, 0), object),
        Err(RendererStateError::StaleHandle)
    );
    assert_eq!(renderer.frame_draw_count(), 1);
}

#[test]
fn frame_begin_requires_renderer_configuration() {
    let mut renderer = RendererState::new();

    assert_eq!(
        renderer.begin_frame(1280, 720),
        Err(RendererStateError::NotConfigured)
    );
    assert_eq!(
        renderer.register_mesh(TERRAIN_VERTEX_FLOATS * 3, 3, TERRAIN_VERTEX_FLOATS),
        Err(RendererStateError::NotConfigured)
    );
    assert_eq!(
        renderer.register_object(),
        Err(RendererStateError::NotConfigured)
    );
}

#[test]
fn frame_uniforms_are_packed_from_rust_render_packets() {
    let mut frame = [0.0; FRAME_PACKET_FLOATS];
    for (index, value) in frame.iter_mut().enumerate() {
        *value = index as f32 + 0.25;
    }

    let uniforms = build_frame_uniform_values(&frame).unwrap();

    assert_eq!(&uniforms[0..16], &frame[0..16]);
    assert_eq!(&uniforms[16..32], &frame[16..32]);
    assert_eq!(&uniforms[32..35], &frame[32..35]);
    assert_eq!(uniforms[35], 1.0);
    assert_eq!(&uniforms[36..39], &frame[35..38]);
    assert_eq!(uniforms[39], frame[41]);
    assert_eq!(&uniforms[40..43], &frame[38..41]);
    assert_eq!(uniforms[43], frame[42]);
    assert_eq!(
        build_frame_uniform_values(&frame[0..FRAME_PACKET_FLOATS - 1]),
        Err(RenderUniformError::InvalidFramePacket)
    );
}

#[test]
fn engine_render_snapshot_builds_frame_packet_in_rust() {
    let snapshot = sample_engine_render_snapshot(true);
    let frame = build_frame_packet_from_engine_snapshot(&snapshot, 16.0 / 9.0).unwrap();

    assert_close(frame[0], 0.80333316);
    assert_close(frame[5], 1.428148);
    assert_close(frame[32], 1.0);
    assert_close(frame[33], 2.0);
    assert_close(frame[34], 3.0);
    assert_close(frame[35], 0.89);
    assert_close(frame[38], 1.0);
    assert_close(frame[41], 1.25);
    assert_close(frame[42], 0.4);
    assert!(frame[16..32].iter().all(|value| value.is_finite()));
}

#[test]
fn engine_render_snapshot_builds_player_marker_world_matrix_in_rust() {
    let visible = sample_engine_render_snapshot(true);
    let hidden = sample_engine_render_snapshot(false);

    let world = build_player_marker_world_matrix(&visible).unwrap().unwrap();
    assert_close(world[0], 1.0);
    assert_close(world[5], 1.0);
    assert_close(world[10], 1.0);
    assert_close(world[12], 4.0);
    assert_close(world[13], 5.0);
    assert_close(world[14], 6.0);
    assert_eq!(build_player_marker_world_matrix(&hidden).unwrap(), None);
}

#[test]
fn engine_render_snapshot_packet_builders_validate_shape_and_camera() {
    let mut snapshot = sample_engine_render_snapshot(true);

    assert_eq!(
        build_frame_packet_from_engine_snapshot(
            &snapshot[0..ENGINE_RENDER_SNAPSHOT_FLOATS - 1],
            1.0
        ),
        Err(RenderPacketError::InvalidEngineSnapshot)
    );
    assert_eq!(
        build_frame_packet_from_engine_snapshot(&snapshot, 0.0),
        Err(RenderPacketError::InvalidAspect)
    );

    snapshot[3] = snapshot[0];
    snapshot[4] = snapshot[1];
    snapshot[5] = snapshot[2];
    assert_eq!(
        build_frame_packet_from_engine_snapshot(&snapshot, 1.0),
        Err(RenderPacketError::InvalidCamera)
    );
}

#[test]
fn object_uniforms_are_packed_with_rust_owned_normal_matrix() {
    let world = [
        2.0, 0.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 8.0, 0.0, 3.0, 5.0, 7.0, 1.0,
    ];
    let material = [0.8, 0.7, 0.6, 1.0, 0.1, 0.2, 0.3, 0.4, 1.0, 0.08];

    let uniforms = build_object_uniform_values(&world, &material).unwrap();

    assert_eq!(&uniforms[0..WORLD_MATRIX_FLOATS], &world);
    assert_close(uniforms[16], 0.5);
    assert_close(uniforms[21], 0.25);
    assert_close(uniforms[26], 0.125);
    assert_close(uniforms[32], 0.8);
    assert_close(uniforms[36], 0.1);
    assert_close(uniforms[39], 0.4);
    assert_close(uniforms[40], 1.0);
    assert_close(uniforms[41], 0.08);
    assert_eq!(uniforms[42], 0.0);
    assert_eq!(uniforms[43], 0.0);
    assert_eq!(
        build_object_uniform_values(&world, &material[0..MATERIAL_PACKET_FLOATS - 1]),
        Err(RenderUniformError::InvalidObjectPacket)
    );
}

#[test]
fn object_uniforms_reject_singular_world_matrices() {
    let singular = [0.0; WORLD_MATRIX_FLOATS];
    let material = [1.0; MATERIAL_PACKET_FLOATS];

    assert_eq!(
        build_object_uniform_values(&singular, &material),
        Err(RenderUniformError::SingularWorldMatrix)
    );
}

#[test]
fn material_packets_are_built_in_rust() {
    let packet =
        build_material_packet([0.8, 0.7, 0.6, 1.0], [0.1, 0.2, 0.3], 0.4, 1.0, 0.08).unwrap();

    assert_close(packet[0], 0.8);
    assert_close(packet[3], 1.0);
    assert_close(packet[4], 0.1);
    assert_close(packet[7], 0.4);
    assert_close(packet[8], 1.0);
    assert_close(packet[9], 0.08);
}

#[test]
fn material_packets_reject_invalid_values() {
    assert_eq!(
        build_material_packet([f32::NAN, 1.0, 1.0, 1.0], [1.0, 1.0, 1.0], 0.18, 0.0, 1.0,),
        Err(MaterialPacketError::InvalidValue)
    );
    assert_eq!(
        build_material_packet([1.0; 4], [1.0; 3], 0.18, 0.0, 0.0),
        Err(MaterialPacketError::InvalidTextureScale)
    );
}

fn sample_engine_render_snapshot(marker_visible: bool) -> [f32; ENGINE_RENDER_SNAPSHOT_FLOATS] {
    [
        1.0,
        2.0,
        3.0,
        1.0,
        2.0,
        2.0,
        0.0,
        0.0,
        70.0_f32.to_radians(),
        0.05,
        500.0,
        0.89,
        0.25,
        0.38,
        1.0,
        0.96,
        0.88,
        1.25,
        0.4,
        if marker_visible { 1.0 } else { 0.0 },
        4.0,
        5.0,
        6.0,
        0.0,
    ]
}

fn configured_renderer() -> RendererState {
    let mut renderer = RendererState::new();
    renderer
        .configure(1280, 720, REQUIRED_TEXTURE_ARRAY_LAYERS)
        .unwrap();
    renderer
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
