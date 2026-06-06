use crate::{
    build_frame_packet_from_engine_snapshot, build_frame_uniform_values, build_material_packet,
    build_object_uniform_values, import_gltf_model_from_slice, model_primitive_vertex_floats,
    BrowserGameInput, BrowserGameState, BrowserTerrainStream, MaterialPacketError,
    ModelAnimationChannel, ModelAnimationClip, ModelAnimationInterpolation, ModelAnimationOutputs,
    ModelAnimationTarget, ModelAssetError, ModelNode, ModelNodeTransform, RenderPacketError,
    RenderUniformError, RendererState, RendererStateError, ResourceHandle, RgbaTextureArrayAsset,
    TerrainTextureArrays, TerrainTextureError, ENGINE_RENDER_SNAPSHOT_FLOATS, FRAME_PACKET_FLOATS,
    MATERIAL_PACKET_FLOATS, MODEL_VERTEX_FLOATS, REQUIRED_TEXTURE_ARRAY_LAYERS,
    SAMPLE_STATIC_BOX_MATERIAL_LABEL, SAMPLE_STATIC_BOX_MESH_LABEL,
    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID, TERRAIN_MATERIAL_ID, TERRAIN_MATERIAL_PACKET,
    TERRAIN_MATERIAL_TEXTURE_ARRAY_ID, TERRAIN_NORMAL_TEXTURE_ARRAY_ID, TERRAIN_VERTEX_FLOATS,
    TEXTURE_FORMAT_RGBA8_UNORM, WORLD_MATRIX_FLOATS,
};
use engine_core::{
    PlayerMode, TerrainComponent, Vec3, DEBUG_PLAYER_MARKER_MATERIAL_LABEL,
    DEBUG_PLAYER_MARKER_MESH_LABEL,
};
use terrain_core::DEFAULT_TERRAIN_PRESET;

const STATIC_BOX_GLB: &[u8] = include_bytes!("../../../assets/models/test-fixtures/static-box.glb");
const BOX_ANIMATED_GLB: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/box-animated.glb");
const ANIMATED_CUBE_GLTF: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/animated-cube.gltf");
const SIMPLE_SKIN_GLTF: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/simple-skin.gltf");

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

    let model_mesh = renderer
        .register_mesh(MODEL_VERTEX_FLOATS * 3, 3, MODEL_VERTEX_FLOATS)
        .unwrap();
    assert_eq!(renderer.resource_counts().meshes, 1);
    assert!(renderer.unregister_mesh(model_mesh).is_ok());

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
    let snapshot = sample_engine_render_snapshot();
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
fn engine_render_snapshot_packet_builders_validate_shape_and_camera() {
    let mut snapshot = sample_engine_render_snapshot();

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
fn gltf_importer_loads_static_box_glb_mesh_primitives() {
    let model = import_gltf_model_from_slice(STATIC_BOX_GLB).unwrap();

    assert_eq!(model.primitive_count(), 1);
    assert!(model.vertex_count() >= 8);
    assert_eq!(model.index_count() % 3, 0);
    assert!(!model.nodes.is_empty());
    assert!(!model.materials.is_empty());

    let primitive = &model.primitives[0];
    assert!(model
        .nodes
        .iter()
        .any(|node| node.mesh == Some(primitive.mesh_index)));
    assert!(primitive
        .vertices
        .iter()
        .all(|vertex| vertex.position.iter().all(|value| value.is_finite())));
    assert!(model.materials.iter().all(|material| material
        .base_color_factor
        .iter()
        .all(|value| value.is_finite())));
}

#[test]
fn gltf_importer_packs_static_model_vertices_for_renderer_upload() {
    let model = import_gltf_model_from_slice(STATIC_BOX_GLB).unwrap();
    let primitive = &model.primitives[0];

    let vertices = model_primitive_vertex_floats(primitive);

    assert_eq!(
        vertices.len(),
        primitive.vertices.len() * MODEL_VERTEX_FLOATS as usize
    );
    assert_eq!(&vertices[0..3], &primitive.vertices[0].position);
    assert_eq!(&vertices[3..6], &primitive.vertices[0].normal);
    assert_eq!(&vertices[6..8], &primitive.vertices[0].texcoord0);
    assert_eq!(&vertices[8..12], &primitive.vertices[0].color0);
}

#[test]
fn gltf_importer_loads_and_samples_node_animation_clip() {
    let model = import_gltf_model_from_slice(BOX_ANIMATED_GLB).unwrap();

    assert_eq!(model.animation_count(), 1);
    let clip = &model.animations[0];
    assert!(clip.duration_seconds > 0.0);
    assert!(!clip.channels.is_empty());
    assert!(clip.channels.iter().any(|channel| {
        matches!(
            channel.target,
            ModelAnimationTarget::Translation | ModelAnimationTarget::Rotation
        )
    }));

    let start = clip.sample_node_transforms(&model.nodes, 0.0).unwrap();
    let middle = clip
        .sample_node_transforms(&model.nodes, clip.duration_seconds * 0.5)
        .unwrap();

    assert_eq!(start.len(), model.nodes.len());
    assert!(start.iter().zip(middle.iter()).any(|(a, b)| a != b));
}

#[test]
fn animation_sampling_interpolates_translation_and_wraps_time() {
    let clip = ModelAnimationClip {
        name: Some("move".to_string()),
        duration_seconds: 2.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0, 2.0],
            outputs: ModelAnimationOutputs::Translations(vec![[0.0, 0.0, 0.0], [2.0, 4.0, 0.0]]),
        }],
    };
    let nodes = vec![test_model_node(ModelNodeTransform::default())];

    let transforms = clip.sample_node_transforms(&nodes, 3.0).unwrap();

    assert_close(transforms[0].translation[0], 1.0);
    assert_close(transforms[0].translation[1], 2.0);
    assert_close(transforms[0].translation[2], 0.0);
}

#[test]
fn animation_sampling_slerps_rotation_channels() {
    let clip = ModelAnimationClip {
        name: Some("turn".to_string()),
        duration_seconds: 2.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Rotation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0, 2.0],
            outputs: ModelAnimationOutputs::Rotations(vec![
                [0.0, 0.0, 0.0, 1.0],
                [0.0, 1.0, 0.0, 0.0],
            ]),
        }],
    };
    let nodes = vec![test_model_node(ModelNodeTransform::default())];

    let transforms = clip.sample_node_transforms(&nodes, 1.0).unwrap();

    assert_close(transforms[0].rotation[0], 0.0);
    assert_close(
        transforms[0].rotation[1].abs(),
        std::f32::consts::FRAC_1_SQRT_2,
    );
    assert_close(transforms[0].rotation[2], 0.0);
    assert_close(
        transforms[0].rotation[3].abs(),
        std::f32::consts::FRAC_1_SQRT_2,
    );
}

#[test]
fn gltf_importer_rejects_file_relative_external_buffers() {
    assert_eq!(
        import_gltf_model_from_slice(ANIMATED_CUBE_GLTF),
        Err(ModelAssetError::UnsupportedExternalBuffer {
            buffer_index: 0,
            uri: "AnimatedCube.bin".to_string()
        })
    );
}

#[test]
fn gltf_importer_preserves_embedded_skin_counts_for_later_animation_work() {
    let model = import_gltf_model_from_slice(SIMPLE_SKIN_GLTF).unwrap();

    assert!(model.primitive_count() > 0);
    assert!(model.skin_count > 0);
    assert!(model.nodes.iter().any(|node| node.skin.is_some()));
}

#[test]
fn terrain_material_packet_is_owned_by_rust() {
    assert_eq!(TERRAIN_MATERIAL_ID, "material:terrain.seed");
    assert_close(TERRAIN_MATERIAL_PACKET[0], 1.0);
    assert_close(TERRAIN_MATERIAL_PACKET[4], 0.55);
    assert_close(TERRAIN_MATERIAL_PACKET[5], 0.58);
    assert_close(TERRAIN_MATERIAL_PACKET[6], 0.52);
    assert_close(TERRAIN_MATERIAL_PACKET[7], 0.04);
    assert_close(TERRAIN_MATERIAL_PACKET[8], 1.0);
    assert_close(TERRAIN_MATERIAL_PACKET[9], 0.08);
}

#[test]
fn terrain_texture_manifest_requests_are_owned_by_rust() {
    let requests = crate::terrain_texture_array_requests().unwrap();

    assert_eq!(requests.len(), 3);
    assert_eq!(requests[0].id, TERRAIN_ALBEDO_TEXTURE_ARRAY_ID);
    assert_eq!(requests[1].id, TERRAIN_NORMAL_TEXTURE_ARRAY_ID);
    assert_eq!(requests[2].id, TERRAIN_MATERIAL_TEXTURE_ARRAY_ID);
    for request in requests {
        assert_eq!(request.urls.len(), REQUIRED_TEXTURE_ARRAY_LAYERS as usize);
        assert!(request.urls.iter().all(|url| url.starts_with('/')));
        assert!(request.urls.iter().all(|url| url.ends_with(".jpg")));
        assert!(request.urls.iter().all(|url| workspace_path(url).exists()));
    }
}

#[test]
fn terrain_texture_manifest_rejects_wrong_layer_count() {
    let manifest = r#"{
      "materials": [{
        "maps": {
          "albedo": { "path": "assets/a.jpg" },
          "normal": { "path": "assets/n.jpg" },
          "roughness": { "path": "assets/r.jpg" }
        }
      }]
    }"#;

    assert_eq!(
        crate::terrain_texture_array_requests_from_manifest_json(manifest),
        Err(TerrainTextureError::InvalidLayerCount {
            actual: 1,
            expected: REQUIRED_TEXTURE_ARRAY_LAYERS
        })
    );
}

#[test]
fn terrain_texture_arrays_validate_browser_loaded_assets() {
    let textures = TerrainTextureArrays::from_assets(vec![
        fake_texture_asset(
            TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
        fake_texture_asset(
            TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
        fake_texture_asset(
            TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
    ])
    .unwrap();

    assert_eq!(textures.width, 2);
    assert_eq!(textures.height, 2);
    assert_eq!(textures.layers, REQUIRED_TEXTURE_ARRAY_LAYERS);
    assert_eq!(textures.format_code, TEXTURE_FORMAT_RGBA8_UNORM);

    let mismatch = TerrainTextureArrays::from_assets(vec![
        fake_texture_asset(
            TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
        fake_texture_asset(
            TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
            4,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
        fake_texture_asset(
            TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        ),
    ]);
    assert!(matches!(
        mismatch,
        Err(TerrainTextureError::TextureShapeMismatch { .. })
    ));
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

#[test]
fn browser_game_state_resets_with_a_rust_owned_grounded_player() {
    let mut state = BrowserGameState::new();

    state.reset_game(0x0F6, 1).unwrap();

    let position = state.player_position().unwrap();
    assert_close(position.x, 0.0);
    assert!(position.y.is_finite());
    assert_close(position.z, 0.0);
    assert_eq!(state.player_mode().unwrap(), PlayerMode::FirstPerson);
    assert_eq!(
        state.terrain_component(),
        Some(TerrainComponent {
            seed: 0x0F6,
            preset: 1
        })
    );
    assert!(state.render_mesh_items().unwrap().is_empty());
}

#[test]
fn browser_game_state_attaches_configured_static_model_scene_item() {
    let mut state = BrowserGameState::new();
    state
        .configure_static_model_scene(
            SAMPLE_STATIC_BOX_MESH_LABEL,
            SAMPLE_STATIC_BOX_MATERIAL_LABEL,
        )
        .unwrap();

    state.reset_game(0x0F6, 1).unwrap();

    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].mesh_label, SAMPLE_STATIC_BOX_MESH_LABEL);
    assert_eq!(items[0].material_label, SAMPLE_STATIC_BOX_MATERIAL_LABEL);
    assert_close(items[0].world_matrix[12], 3.0);
    assert!(items[0].world_matrix[13].is_finite());
    assert_close(items[0].world_matrix[14], 6.0);
}

#[test]
fn browser_game_state_applies_configured_model_animation_to_scene_item() {
    let mut state = BrowserGameState::new();
    state
        .configure_animated_static_model_scene(
            SAMPLE_STATIC_BOX_MESH_LABEL,
            SAMPLE_STATIC_BOX_MATERIAL_LABEL,
            ModelAnimationClip {
                name: Some("test-move".to_string()),
                duration_seconds: 2.0,
                channels: vec![ModelAnimationChannel {
                    target_node: 0,
                    target: ModelAnimationTarget::Translation,
                    interpolation: ModelAnimationInterpolation::Linear,
                    inputs: vec![0.0, 2.0],
                    outputs: ModelAnimationOutputs::Translations(vec![
                        [0.0, 0.0, 0.0],
                        [2.0, 0.0, 0.0],
                    ]),
                }],
            },
            0,
            vec![ModelNodeTransform::default()],
        )
        .unwrap();

    state.reset_game(0x0F6, 1).unwrap();
    state
        .tick(BrowserGameInput {
            delta_seconds: 1.0,
            forward: 0.0,
            right: 0.0,
            up: 0.0,
            fast: false,
            look_delta_x: 0.0,
            look_delta_y: 0.0,
        })
        .unwrap();

    let item = &state.render_mesh_items().unwrap()[0];
    assert_close(item.world_matrix[12], 5.0);
    let animation = state.model_animation_snapshot().unwrap();
    assert_eq!(animation.runtime, "rust");
    assert_eq!(animation.clip_name, Some("test-move".to_string()));
    assert_close(animation.time_seconds, 1.0);
    assert_close(animation.duration_seconds, 2.0);
}

#[test]
fn browser_game_state_replaces_configured_static_model_scene_item() {
    let mut state = BrowserGameState::new();

    state.reset_game(0x0F6, 1).unwrap();
    state
        .configure_static_model_scene("model.first.mesh", "model.first.material")
        .unwrap();
    state
        .configure_static_model_scene("model.second.mesh", "model.second.material")
        .unwrap();

    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].mesh_label, "model.second.mesh");
    assert_eq!(items[0].material_label, "model.second.material");
}

#[test]
fn browser_game_state_ticks_player_and_grounds_against_terrain() {
    let mut state = BrowserGameState::new();
    state.reset_game(0x0F6, 1).unwrap();
    let before = state.player_position().unwrap();

    state
        .tick(BrowserGameInput {
            delta_seconds: 1.0,
            forward: 1.0,
            right: 0.0,
            up: 0.0,
            fast: false,
            look_delta_x: 0.0,
            look_delta_y: 0.0,
        })
        .unwrap();

    let after = state.player_position().unwrap();
    assert!(after.z > before.z);
    assert!(after.y.is_finite());
}

#[test]
fn browser_game_state_public_controls_cover_player_and_debug_camera_api() {
    let mut state = BrowserGameState::new();

    assert_eq!(state.terrain_seed(), 0);
    assert_eq!(state.terrain_preset(), DEFAULT_TERRAIN_PRESET);

    assert_eq!(state.toggle_player_mode().unwrap(), PlayerMode::DebugFly);
    assert_eq!(state.player_mode().unwrap(), PlayerMode::DebugFly);

    let position = state.set_player_position_xz(12.0, -8.0).unwrap();
    assert_close(position.x, 12.0);
    assert!(position.y.is_finite());
    assert_close(position.z, -8.0);
    assert_eq!(state.player_position().unwrap(), position);

    state
        .set_debug_camera(Vec3::new(1.0, 2.0, 3.0), 0.4, -0.2)
        .unwrap();
    let snapshot = state.render_snapshot_values().unwrap();
    assert_close(snapshot[0], 1.0);
    assert_close(snapshot[1], 2.0);
    assert_close(snapshot[2], 3.0);
    assert_close(snapshot[6], 0.4);
    assert_close(snapshot[7], -0.2);

    state.set_player_mode(PlayerMode::FirstPerson).unwrap();

    assert_eq!(state.player_mode().unwrap(), PlayerMode::FirstPerson);
    assert_eq!(
        crate::player_mode_code(PlayerMode::DebugFly),
        PlayerMode::DebugFly.code()
    );
    assert_eq!(
        crate::player_mode_from_code(PlayerMode::FirstPerson.code()),
        Some(PlayerMode::FirstPerson)
    );
    assert_eq!(crate::player_mode_from_code(99), None);
}

#[test]
fn browser_game_state_debug_fly_moves_camera_without_moving_player_marker() {
    let mut state = BrowserGameState::new();
    state.reset_game(0x0F6, 1).unwrap();
    let player_position = state.player_position().unwrap();

    state.set_player_mode(PlayerMode::DebugFly).unwrap();
    state
        .tick(BrowserGameInput {
            delta_seconds: 1.0,
            forward: 0.0,
            right: 0.0,
            up: 1.0,
            fast: false,
            look_delta_x: 0.0,
            look_delta_y: 0.0,
        })
        .unwrap();

    assert_eq!(state.player_position().unwrap(), player_position);
    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].mesh_label, DEBUG_PLAYER_MARKER_MESH_LABEL);
    assert_eq!(items[0].material_label, DEBUG_PLAYER_MARKER_MATERIAL_LABEL);
    assert_close(items[0].world_matrix[12], player_position.x);
    assert_close(items[0].world_matrix[13], player_position.y);
    assert_close(items[0].world_matrix[14], player_position.z);
}

#[test]
fn browser_terrain_stream_generates_and_prunes_meshes_in_rust() {
    let mut stream = BrowserTerrainStream::new(0x0F6, 1).unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);

    let mut uploaded_mesh_count = 0;
    for _ in 0..20 {
        uploaded_mesh_count += stream.tick(origin).upserted_meshes.len();
    }

    assert!(uploaded_mesh_count > 0);
    assert!(stream.loaded_chunk_keys().contains(&"0,0,0".to_string()));
    assert!(stream.render_chunk_keys().contains(&"0,0,0".to_string()));
    assert!(stream.status().rendered_chunk_count > 0);

    let moved = Vec3::new(96.0, 0.0, 0.0);
    let update = stream.tick(moved);

    assert!(update.removed_coords.iter().any(|coord| coord.x == 0));
    assert!(stream.loaded_chunk_keys().contains(&"3,0,0".to_string()));
    assert!(!stream.render_chunk_keys().contains(&"0,0,0".to_string()));
}

fn sample_engine_render_snapshot() -> [f32; ENGINE_RENDER_SNAPSHOT_FLOATS] {
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
    ]
}

fn configured_renderer() -> RendererState {
    let mut renderer = RendererState::new();
    renderer
        .configure(1280, 720, REQUIRED_TEXTURE_ARRAY_LAYERS)
        .unwrap();
    renderer
}

fn fake_texture_asset(id: &str, width: u32, height: u32, layers: u32) -> RgbaTextureArrayAsset {
    RgbaTextureArrayAsset {
        id: id.to_string(),
        width,
        height,
        layers,
        data: vec![0; width as usize * height as usize * layers as usize * 4],
    }
}

fn workspace_path(path: &str) -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join(path.trim_start_matches('/'))
}

fn test_model_node(local_transform: ModelNodeTransform) -> ModelNode {
    ModelNode {
        name: None,
        parent: None,
        children: Vec::new(),
        mesh: None,
        skin: None,
        local_transform,
    }
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
