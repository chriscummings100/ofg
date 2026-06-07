use std::collections::BTreeSet;

use crate::{
    blend_node_transforms, build_frame_packet_from_engine_snapshot, build_frame_uniform_values,
    build_material_packet, build_metallic_roughness_material_packet, build_object_uniform_values,
    build_specular_glossiness_material_packet, horizontal_movement_is_active,
    import_gltf_model_from_slice, model_primitive_vertex_floats, skin_joint_matrices,
    skin_primitive_vertices, skinned_model_render_assets, BrowserGameInput, BrowserGameState,
    BrowserGameStateError, BrowserTerrainBuildCompletion, BrowserTerrainStream,
    LocomotionAnimationController, MaterialPacketError, MeshResource, ModelAnimationChannel,
    ModelAnimationClip, ModelAnimationInterpolation, ModelAnimationOutputs, ModelAnimationTarget,
    ModelAsset, ModelAssetError, ModelMaterial, ModelNode, ModelNodeTransform, ModelPrimitive,
    ModelSkin, ModelVertex, PlayerCharacterLocomotionTuning, PlayerCharacterModel,
    RenderPacketError, RenderUniformError, RendererState, RendererStateError, ResourceHandle,
    RgbaTextureArrayAsset, TerrainTextureArrays, TerrainTextureError, TextureResource,
    ENGINE_RENDER_SNAPSHOT_FLOATS, FRAME_PACKET_FLOATS, MATERIAL_PACKET_FLOATS,
    MATERIAL_WORKFLOW_METALLIC_ROUGHNESS, MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS,
    MODEL_VERTEX_FLOATS, QUATERNIUS_IDLE_CLIP_NAME, QUATERNIUS_RUN_CLIP_NAME,
    QUATERNIUS_WALK_CLIP_NAME, REQUIRED_TEXTURE_ARRAY_LAYERS, SAMPLE_STATIC_BOX_MATERIAL_LABEL,
    SAMPLE_STATIC_BOX_MESH_LABEL, TERRAIN_ALBEDO_TEXTURE_ARRAY_ID, TERRAIN_MATERIAL_ID,
    TERRAIN_MATERIAL_PACKET, TERRAIN_MATERIAL_TEXTURE_ARRAY_ID, TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
    TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM, WORLD_MATRIX_FLOATS,
};
use engine_core::{EngineError, MaterialId, MeshId, PlayerMode, TerrainComponent, Vec3};
use terrain_core::{
    build_node_mesh, height_at, terrain_node_cell_size, terrain_node_parent, TerrainLodBand,
    TerrainNodeKey, DEFAULT_TERRAIN_PRESET, TERRAIN_CHUNK_CELLS_PER_AXIS,
};

const STATIC_BOX_GLB: &[u8] = include_bytes!("../../../assets/models/test-fixtures/static-box.glb");
const BOX_ANIMATED_GLB: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/box-animated.glb");
const RIGGED_SIMPLE_GLB: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/rigged-simple.glb");
const QUATERNIUS_UAL1_GLB: &[u8] =
    include_bytes!("../../../assets/models/player/quaternius-ual1-standard.glb");
const QUATERNIUS_SUPERHERO_MALE_GLB: &[u8] =
    include_bytes!("../../../assets/models/player/quaternius-superhero-male.glb");
const QUATERNIUS_SUPERHERO_FEMALE_GLB: &[u8] =
    include_bytes!("../../../assets/models/player/quaternius-superhero-female.glb");
const ANIMATED_CUBE_GLTF: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/animated-cube.gltf");
const SIMPLE_SKIN_GLTF: &[u8] =
    include_bytes!("../../../assets/models/test-fixtures/simple-skin.gltf");
const MIN_MULTI_KM_TERRAIN_SPAN_METERS: f64 = 4096.0;

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
fn renderer_resources_expose_upload_contract_metadata() {
    let mesh = MeshResource::new(MODEL_VERTEX_FLOATS * 4, 6, MODEL_VERTEX_FLOATS).unwrap();
    assert_eq!(mesh.vertex_float_count(), MODEL_VERTEX_FLOATS * 4);
    assert_eq!(mesh.index_count(), 6);
    assert_eq!(mesh.floats_per_vertex(), MODEL_VERTEX_FLOATS);

    let texture = TextureResource::new(
        32,
        16,
        2,
        TEXTURE_FORMAT_RGBA8_UNORM,
        REQUIRED_TEXTURE_ARRAY_LAYERS,
    )
    .unwrap();
    assert_eq!(texture.width(), 32);
    assert_eq!(texture.height(), 16);
    assert_eq!(texture.layers(), 2);
    assert_eq!(texture.format_code(), TEXTURE_FORMAT_RGBA8_UNORM);

    assert!(!RendererState::default().is_configured());
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
fn object_handles_are_unregisterable_and_generational() {
    let mut renderer = configured_renderer();
    let first = renderer.register_object().unwrap();

    assert_eq!(renderer.resource_counts().objects, 1);
    assert_eq!(renderer.unregister_object(first), Ok(()));
    assert_eq!(renderer.resource_counts().objects, 0);
    assert_eq!(
        renderer.unregister_object(first),
        Err(RendererStateError::StaleHandle)
    );

    let second = renderer.register_object().unwrap();
    assert_eq!(second.slot(), first.slot());
    assert_eq!(second.generation(), first.generation() + 1);
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
    assert_eq!(
        renderer.register_texture(64, 64, 1, TEXTURE_FORMAT_RGBA8_UNORM),
        Err(RendererStateError::NotConfigured)
    );
    assert_eq!(
        configured_renderer().resize(0, 720),
        Err(RendererStateError::InvalidCanvasSize)
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
    assert_eq!(&uniforms[44..56], &frame[43..55]);
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
    assert_close(frame[43], 12.0);
    assert_close(frame[45], 0.25);
    assert_close(frame[47], 2.25);
    assert_close(frame[53], 0.1);
    assert_close(frame[54], 0.0);
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
fn metallic_roughness_material_packets_preserve_gltf_factors() {
    let packet =
        build_metallic_roughness_material_packet([0.8, 0.7, 0.6, 0.5], 0.25, 0.75, 1.0).unwrap();

    assert_close(packet[0], 0.8);
    assert_close(packet[3], 0.5);
    assert_close(packet[4], 0.25);
    assert_close(packet[5], 0.75);
    assert_close(packet[8], MATERIAL_WORKFLOW_METALLIC_ROUGHNESS);
}

#[test]
fn specular_glossiness_material_packets_preserve_extension_factors() {
    let packet =
        build_specular_glossiness_material_packet([0.8, 0.7, 0.6, 0.5], [0.1, 0.2, 0.3], 0.65, 1.0)
            .unwrap();

    assert_close(packet[0], 0.8);
    assert_close(packet[3], 0.5);
    assert_close(packet[4], 0.1);
    assert_close(packet[6], 0.3);
    assert_close(packet[7], 0.65);
    assert_close(packet[8], MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS);
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
fn animation_blending_interpolates_node_trs() {
    let from = vec![ModelNodeTransform {
        translation: [0.0, 0.0, 0.0],
        rotation: [0.0, 0.0, 0.0, 1.0],
        scale: [1.0, 1.0, 1.0],
    }];
    let to = vec![ModelNodeTransform {
        translation: [2.0, 4.0, 0.0],
        rotation: [0.0, 1.0, 0.0, 0.0],
        scale: [3.0, 5.0, 1.0],
    }];

    let blended = blend_node_transforms(&from, &to, 0.5).unwrap();

    assert_close(blended[0].translation[0], 1.0);
    assert_close(blended[0].translation[1], 2.0);
    assert_close(
        blended[0].rotation[1].abs(),
        std::f32::consts::FRAC_1_SQRT_2,
    );
    assert_close(
        blended[0].rotation[3].abs(),
        std::f32::consts::FRAC_1_SQRT_2,
    );
    assert_close(blended[0].scale[0], 2.0);
    assert_close(blended[0].scale[1], 3.0);
}

#[test]
fn animation_sampling_covers_step_final_and_zero_duration_edges() {
    let base = vec![ModelNodeTransform::default()];
    let step_clip = ModelAnimationClip {
        name: Some("step".to_string()),
        duration_seconds: 4.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Step,
            inputs: vec![0.0, 1.0, 3.0],
            outputs: ModelAnimationOutputs::Translations(vec![
                [0.0, 0.0, 0.0],
                [5.0, 0.0, 0.0],
                [9.0, 0.0, 0.0],
            ]),
        }],
    };

    let stepped = step_clip.sample_transforms(&base, 0.5).unwrap();
    assert_close(stepped[0].translation[0], 0.0);
    let final_keyframe = step_clip.sample_transforms(&base, 3.5).unwrap();
    assert_close(final_keyframe[0].translation[0], 9.0);

    let zero_duration_clip = ModelAnimationClip {
        name: Some("scale".to_string()),
        duration_seconds: 0.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Scale,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0, 2.0],
            outputs: ModelAnimationOutputs::Scales(vec![[1.0, 1.0, 1.0], [3.0, 3.0, 3.0]]),
        }],
    };
    assert_close(zero_duration_clip.wrapped_time(3.0), 3.0);
    let scaled = zero_duration_clip.sample_transforms(&base, 3.0).unwrap();
    assert_eq!(scaled[0].scale, [3.0, 3.0, 3.0]);
}

#[test]
fn animation_sampling_rejects_invalid_inputs_and_normalizes_degenerate_rotations() {
    let base = vec![ModelNodeTransform::default()];
    let missing_target = ModelAnimationClip {
        name: Some("bad-target".to_string()),
        duration_seconds: 1.0,
        channels: vec![ModelAnimationChannel {
            target_node: 1,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0],
            outputs: ModelAnimationOutputs::Translations(vec![[1.0, 0.0, 0.0]]),
        }],
    };
    assert_eq!(
        missing_target.sample_transforms(&base, 0.0),
        Err(ModelAssetError::InvalidAnimationTargetNode { node_index: 1 })
    );

    let zero_rotation = ModelAnimationClip {
        name: Some("zero-rotation".to_string()),
        duration_seconds: 1.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Rotation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0],
            outputs: ModelAnimationOutputs::Rotations(vec![[0.0, 0.0, 0.0, 0.0]]),
        }],
    };
    let sampled = zero_rotation.sample_transforms(&base, 0.0).unwrap();
    assert_eq!(sampled[0].rotation, [0.0, 0.0, 0.0, 1.0]);

    assert_eq!(
        zero_rotation.sample_transforms(&base, f32::NAN),
        Err(ModelAssetError::InvalidAnimationTime)
    );

    let empty_inputs = ModelAnimationClip {
        name: Some("empty-inputs".to_string()),
        duration_seconds: 1.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: Vec::new(),
            outputs: ModelAnimationOutputs::Translations(Vec::new()),
        }],
    };
    assert_eq!(
        empty_inputs.sample_transforms(&base, 0.0),
        Err(ModelAssetError::InvalidAnimationKeyframes {
            animation_index: 0,
            channel_index: 0,
            input_count: 0,
            output_count: 0,
        })
    );

    let mismatched_target = ModelAnimationClip {
        name: Some("mismatched-target".to_string()),
        duration_seconds: 1.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0],
            outputs: ModelAnimationOutputs::Rotations(vec![[0.0, 0.0, 0.0, 1.0]]),
        }],
    };
    assert_eq!(
        mismatched_target.sample_transforms(&base, 0.0),
        Err(ModelAssetError::InvalidAnimationData {
            animation_index: 0,
            channel_index: 0,
            attribute: "target/output",
        })
    );
}

#[test]
fn animation_blending_rejects_shape_and_time_errors() {
    let transform = ModelNodeTransform::default();

    assert_eq!(
        blend_node_transforms(&[transform], &[], 0.5),
        Err(ModelAssetError::InvalidAnimationBlendTransformCount {
            from_count: 1,
            to_count: 0,
        })
    );
    assert_eq!(
        blend_node_transforms(&[transform], &[transform], f32::INFINITY),
        Err(ModelAssetError::InvalidAnimationTime)
    );

    let blended = blend_node_transforms(&[transform], &[transform], 0.5).unwrap();
    assert_eq!(blended[0].rotation, [0.0, 0.0, 0.0, 1.0]);
}

#[test]
fn gltf_animation_importer_reports_unsupported_and_invalid_channels() {
    assert!(matches!(
        import_gltf_model_from_slice(&animation_gltf(
            "CUBICSPLINE",
            "translation",
            &[0.0, 1.0],
            &[0.0, 0.0, 0.0, 1.0, 0.0, 0.0],
            "VEC3",
            2
        )),
        Err(ModelAssetError::UnsupportedAnimationInterpolation { .. })
    ));
    assert!(matches!(
        import_gltf_model_from_slice(&animation_gltf(
            "LINEAR",
            "weights",
            &[0.0, 1.0],
            &[0.0, 1.0],
            "SCALAR",
            2
        )),
        Err(ModelAssetError::UnsupportedAnimationTarget { .. })
    ));
    assert_eq!(
        import_gltf_model_from_slice(&animation_gltf(
            "LINEAR",
            "translation",
            &[0.0, 1.0],
            &[0.0, 0.0, 0.0],
            "VEC3",
            1
        )),
        Err(ModelAssetError::InvalidAnimationKeyframes {
            animation_index: 0,
            channel_index: 0,
            input_count: 2,
            output_count: 1,
        })
    );
    assert_eq!(
        import_gltf_model_from_slice(&animation_gltf(
            "LINEAR",
            "translation",
            &[0.0, f32::NAN],
            &[0.0, 0.0, 0.0, 1.0, 0.0, 0.0],
            "VEC3",
            2
        )),
        Err(ModelAssetError::InvalidAnimationData {
            animation_index: 0,
            channel_index: 0,
            attribute: "input time",
        })
    );
    assert_eq!(
        import_gltf_model_from_slice(&animation_gltf(
            "LINEAR",
            "translation",
            &[0.0, 1.0],
            &[0.0, 0.0, 0.0, f32::NAN, 0.0, 0.0],
            "VEC3",
            2
        )),
        Err(ModelAssetError::InvalidAnimationData {
            animation_index: 0,
            channel_index: 0,
            attribute: "output",
        })
    );
}

#[test]
fn locomotion_controller_crossfades_idle_to_walk_when_movement_starts() {
    let idle = single_translation_clip("idle", 0.0);
    let walk = single_translation_clip("walk", 10.0);
    let run = single_translation_clip("run", 30.0);
    let tuning = PlayerCharacterLocomotionTuning {
        walk_speed_meters_per_second: 1.0,
        run_speed_meters_per_second: 3.0,
        idle_playback_scale: 1.0,
        walk_playback_scale: 1.0,
        run_playback_scale: 1.0,
    };
    let base = vec![ModelNodeTransform::default()];
    let mut controller = LocomotionAnimationController::new(idle, walk, run, 0.2, tuning).unwrap();

    let idle_pose = controller.advance_pose(&base, 0.1, 0.0).unwrap();
    assert_close(idle_pose[0].translation[0], 0.0);

    let blended_pose = controller.advance_pose(&base, 0.1, 1.0).unwrap();
    let blending = controller.snapshot();
    assert_eq!(blending.active_clip_name, "idle");
    assert_eq!(blending.next_clip_name, Some("walk".to_string()));
    assert_close(blending.blend_weight, 0.5);
    assert_close(blending.walk_run_blend_weight, 0.0);
    assert_close(blended_pose[0].translation[0], 5.0);

    let walk_pose = controller.advance_pose(&base, 0.1, 1.0).unwrap();
    let walking = controller.snapshot();
    assert_eq!(walking.active_clip_name, "walk");
    assert_eq!(walking.next_clip_name, None);
    assert_close(walking.blend_weight, 0.0);
    assert_close(walking.locomotion_speed_meters_per_second, 1.0);
    assert_close(walk_pose[0].translation[0], 10.0);

    let run_pose = controller.advance_pose(&base, 0.1, 3.0).unwrap();
    let running = controller.snapshot();
    assert_eq!(running.active_clip_name, "run");
    assert_eq!(running.next_clip_name, None);
    assert_close(running.walk_run_blend_weight, 1.0);
    assert_close(run_pose[0].translation[0], 30.0);
}

#[test]
fn locomotion_movement_threshold_uses_horizontal_input_only() {
    assert!(!horizontal_movement_is_active([0.0, 0.0]));
    assert!(!horizontal_movement_is_active([0.005, 0.0]));
    assert!(horizontal_movement_is_active([0.02, 0.0]));
    assert!(horizontal_movement_is_active([0.0, -0.02]));
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
fn quaternius_player_asset_imports_skin_and_idle_walk_clips() {
    let animation_model = import_gltf_model_from_slice(QUATERNIUS_UAL1_GLB).unwrap();

    assert!(animation_model
        .animations
        .iter()
        .any(|clip| clip.name.as_deref() == Some(QUATERNIUS_IDLE_CLIP_NAME)));
    assert!(animation_model
        .animations
        .iter()
        .any(|clip| clip.name.as_deref() == Some(QUATERNIUS_WALK_CLIP_NAME)));
    assert!(animation_model
        .animations
        .iter()
        .any(|clip| clip.name.as_deref() == Some(QUATERNIUS_RUN_CLIP_NAME)));

    for body_bytes in [
        QUATERNIUS_SUPERHERO_MALE_GLB,
        QUATERNIUS_SUPERHERO_FEMALE_GLB,
    ] {
        let body_model = import_gltf_model_from_slice(body_bytes).unwrap();
        assert!(body_model.skin_count() > 0);

        let mut character =
            PlayerCharacterModel::from_body_and_animation_models(body_model, &animation_model)
                .unwrap();
        let initial_vertices = character.current_vertices().unwrap();
        let initial_part_vertices = character.current_part_vertices().unwrap();
        assert_eq!(initial_vertices.len() % MODEL_VERTEX_FLOATS as usize, 0);
        assert!(initial_vertices.len() > MODEL_VERTEX_FLOATS as usize * 100);
        assert_eq!(initial_part_vertices.len(), character.part_count());
        assert!(initial_part_vertices
            .iter()
            .all(|vertices| vertices.len() % MODEL_VERTEX_FLOATS as usize == 0));
        assert!(!character.indices().is_empty());
        assert_eq!(character.part_indices(0), character.indices());
        assert!(character
            .material_packet()
            .iter()
            .all(|value| value.is_finite()));
        assert_eq!(
            character.part_material_packet(0),
            character.material_packet()
        );
        assert!(character.part_material_index(0).is_some());
        assert_eq!(
            character.part_mesh_node_index(0),
            character.mesh_node_index()
        );
        assert!(character.skin_joint_count() >= 60);
        let tuning = character.locomotion_tuning();
        assert_eq!(tuning, PlayerCharacterLocomotionTuning::default());
        assert_eq!(character.set_locomotion_tuning(tuning), Ok(()));
        assert!(matches!(
            character.set_locomotion_tuning(PlayerCharacterLocomotionTuning {
                walk_speed_meters_per_second: f32::NAN,
                ..tuning
            }),
            Err(crate::PlayerCharacterModelError::InvalidLocomotionTuning(
                "walkSpeedMetersPerSecond",
                value
            )) if value.is_nan()
        ));
        assert_eq!(
            character.animation_snapshot().active_clip_name,
            QUATERNIUS_IDLE_CLIP_NAME
        );

        let moving_vertices = character.tick_vertices(0.1, 5.5).unwrap();
        let moving_part_vertices = character.tick_part_vertices(0.0, 5.5).unwrap();
        let moving = character.animation_snapshot();

        assert_eq!(
            moving.next_clip_name,
            Some(QUATERNIUS_WALK_CLIP_NAME.to_string())
        );
        assert!(moving.blend_weight > 0.0);
        assert_close(moving.walk_run_blend_weight, 0.0);
        assert_eq!(moving_vertices.len(), initial_vertices.len());
        assert_eq!(moving_part_vertices.len(), initial_part_vertices.len());
        assert_ne!(moving_vertices, initial_vertices);

        let running_vertices = character.tick_vertices(0.2, 16.5).unwrap();
        let running = character.animation_snapshot();
        assert_eq!(running.active_clip_name, QUATERNIUS_RUN_CLIP_NAME);
        assert_close(running.walk_run_blend_weight, 1.0);
        assert_eq!(running_vertices.len(), initial_vertices.len());
    }
}

#[test]
fn gltf_importer_preserves_embedded_skin_counts_for_later_animation_work() {
    let model = import_gltf_model_from_slice(SIMPLE_SKIN_GLTF).unwrap();

    assert!(model.primitive_count() > 0);
    assert!(model.skin_count() > 0);
    assert!(model.nodes.iter().any(|node| node.skin.is_some()));
}

#[test]
fn gltf_importer_preserves_rigged_simple_skin_vertices_and_inverse_binds() {
    let model = import_gltf_model_from_slice(RIGGED_SIMPLE_GLB).unwrap();

    assert!(model.skin_count() > 0);
    let skin = &model.skins[0];
    assert!(!skin.joints.is_empty());
    assert_eq!(skin.inverse_bind_matrices.len(), skin.joints.len());
    assert!(model.nodes.iter().any(|node| node.skin == Some(0)));
    let primitive = model
        .primitives
        .iter()
        .find(|primitive| {
            primitive.vertices.iter().any(|vertex| {
                vertex.weights0.iter().any(|weight| *weight > 0.0)
                    && vertex.joints0.iter().any(|joint| *joint > 0)
            })
        })
        .unwrap();
    let node_transforms = model
        .animations
        .first()
        .map(|clip| {
            clip.sample_node_transforms(&model.nodes, clip.duration_seconds * 0.5)
                .unwrap()
        })
        .unwrap_or_else(|| {
            model
                .nodes
                .iter()
                .map(|node| node.local_transform)
                .collect()
        });

    let joint_matrices = skin_joint_matrices(&model, 0, &node_transforms).unwrap();
    let skinned_vertices = skin_primitive_vertices(primitive, &joint_matrices).unwrap();

    assert_eq!(joint_matrices.len(), skin.joints.len());
    assert_eq!(skinned_vertices.len(), primitive.vertices.len());
    assert!(skinned_vertices
        .iter()
        .all(|vertex| vertex.position.iter().all(|value| value.is_finite())));
}

#[test]
fn skinned_model_render_assets_bakes_rigged_simple_for_model_pipeline() {
    let model = import_gltf_model_from_slice(RIGGED_SIMPLE_GLB).unwrap();
    let clip = model.animations.first().unwrap();
    let node_transforms: Vec<ModelNodeTransform> = model
        .nodes
        .iter()
        .map(|node| node.local_transform)
        .collect();

    let assets =
        skinned_model_render_assets(&model, clip, &node_transforms, clip.duration_seconds * 0.5)
            .unwrap();

    assert_eq!(assets.vertices.len() % MODEL_VERTEX_FLOATS as usize, 0);
    assert!(!assets.indices.is_empty());
    assert_eq!(assets.skin_joint_count, 2);
    assert!(model.nodes[assets.mesh_node_index].skin.is_some());
    assert!(assets.material_packet.iter().all(|value| value.is_finite()));
}

#[test]
fn cpu_skinning_preserves_vertices_at_inverse_bind_pose() {
    let mut model = test_skin_model(vec![test_model_node(ModelNodeTransform {
        translation: [2.0, 0.0, 0.0],
        rotation: [0.0, 0.0, 0.0, 1.0],
        scale: [1.0, 1.0, 1.0],
    })]);
    model.skins[0].inverse_bind_matrices[0] = translation_matrix(-2.0, 0.0, 0.0);
    let primitive = test_skinned_primitive(
        [3.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0, 0, 0, 0],
        [1.0, 0.0, 0.0, 0.0],
    );
    let node_transforms: Vec<ModelNodeTransform> = model
        .nodes
        .iter()
        .map(|node| node.local_transform)
        .collect();

    let joint_matrices = skin_joint_matrices(&model, 0, &node_transforms).unwrap();
    let skinned = skin_primitive_vertices(&primitive, &joint_matrices).unwrap();

    assert_close(skinned[0].position[0], 3.0);
    assert_close(skinned[0].position[1], 0.0);
    assert_close(skinned[0].position[2], 0.0);
}

#[test]
fn cpu_skinning_applies_one_joint_motion() {
    let model = test_skin_model(vec![test_model_node(ModelNodeTransform {
        translation: [2.0, 0.0, 0.0],
        rotation: [0.0, 0.0, 0.0, 1.0],
        scale: [1.0, 1.0, 1.0],
    })]);
    let primitive = test_skinned_primitive(
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0, 0, 0, 0],
        [1.0, 0.0, 0.0, 0.0],
    );
    let node_transforms: Vec<ModelNodeTransform> = model
        .nodes
        .iter()
        .map(|node| node.local_transform)
        .collect();

    let joint_matrices = skin_joint_matrices(&model, 0, &node_transforms).unwrap();
    let skinned = skin_primitive_vertices(&primitive, &joint_matrices).unwrap();

    assert_close(skinned[0].position[0], 3.0);
    assert_close(skinned[0].position[1], 0.0);
    assert_close(skinned[0].position[2], 0.0);
    assert_close(skinned[0].normal[0], 0.0);
    assert_close(skinned[0].normal[1], 1.0);
    assert_close(skinned[0].normal[2], 0.0);
}

#[test]
fn cpu_skinning_blends_weighted_two_joint_motion() {
    let model = test_skin_model(vec![
        test_model_node(ModelNodeTransform::default()),
        test_model_node(ModelNodeTransform {
            translation: [2.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0, 1.0],
            scale: [1.0, 1.0, 1.0],
        }),
    ]);
    let primitive = test_skinned_primitive(
        [0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0, 1, 0, 0],
        [0.25, 0.75, 0.0, 0.0],
    );
    let node_transforms: Vec<ModelNodeTransform> = model
        .nodes
        .iter()
        .map(|node| node.local_transform)
        .collect();

    let joint_matrices = skin_joint_matrices(&model, 0, &node_transforms).unwrap();
    let skinned = skin_primitive_vertices(&primitive, &joint_matrices).unwrap();

    assert_close(skinned[0].position[0], 1.5);
    assert_close(skinned[0].position[1], 0.0);
    assert_close(skinned[0].position[2], 0.0);
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
fn browser_game_state_attaches_scaled_static_model_scene_item() {
    let mut state = BrowserGameState::new();
    state
        .configure_scaled_static_model_scene("scaled.mesh", "scaled.material", 0.5, 3.25)
        .unwrap();

    state.reset_game(0x0F6, 1).unwrap();

    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].mesh_label, "scaled.mesh");
    assert_eq!(items[0].material_label, "scaled.material");
    assert_close(items[0].world_matrix[0], 0.5);
    assert_close(items[0].world_matrix[5], 0.5);
    assert_close(items[0].world_matrix[10], 0.5);
    assert_close(
        items[0].world_matrix[13],
        height_at(0x0F6, 1, 3.0, 6.0) as f32 + 3.25,
    );
}

#[test]
fn browser_game_state_attaches_player_character_scene_to_player() {
    let mut state = BrowserGameState::new();
    state
        .configure_player_character_scene("player.mesh", "player.material", 1.25, 0.5)
        .unwrap();

    state.reset_game(0x0F6, 1).unwrap();

    let player_position = state.player_position().unwrap();
    let first_snapshot = state.player_character_scene_snapshot().unwrap().unwrap();
    assert_eq!(first_snapshot.runtime, "rust");
    assert!(!first_snapshot.visible);
    assert!(first_snapshot.follows_player);
    assert!(!first_snapshot.debug_marker_visible);
    assert!(state.render_mesh_items().unwrap().is_empty());

    state.set_player_mode(PlayerMode::DebugFly).unwrap();
    let debug_items = state.render_mesh_items().unwrap();

    assert_eq!(debug_items.len(), 1);
    assert_eq!(debug_items[0].mesh_label, "player.mesh");
    assert_eq!(debug_items[0].material_label, "player.material");
    assert_close(debug_items[0].world_matrix[12], player_position.x);
    assert_close(debug_items[0].world_matrix[13], player_position.y + 0.5);
    assert_close(debug_items[0].world_matrix[14], player_position.z);
    let debug_snapshot = state.player_character_scene_snapshot().unwrap().unwrap();
    assert!(debug_snapshot.visible);
    assert!(debug_snapshot.follows_player);
    assert!(!debug_snapshot.debug_marker_visible);

    let moved = state.set_player_position_xz(12.0, -8.0).unwrap();
    let moved_item = &state.render_mesh_items().unwrap()[0];
    assert_close(moved_item.world_matrix[12], moved.x);
    assert_close(moved_item.world_matrix[13], moved.y + 0.5);
    assert_close(moved_item.world_matrix[14], moved.z);
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
fn browser_game_state_rejects_invalid_scaled_static_model_scene_item() {
    let mut state = BrowserGameState::new();

    assert_eq!(
        state.configure_scaled_animated_static_model_scene(
            "model.mesh",
            "model.material",
            0.0,
            ModelAnimationClip {
                name: Some("test".to_string()),
                duration_seconds: 1.0,
                channels: Vec::new(),
            },
            0,
            vec![ModelNodeTransform::default()],
        ),
        Err(BrowserGameStateError::InvalidModelSceneScale(0.0))
    );

    let invalid_height =
        state.configure_scaled_static_model_scene("model.mesh", "model.material", 1.0, f32::NAN);
    assert!(matches!(
        invalid_height,
        Err(BrowserGameStateError::InvalidModelSceneHeightOffset(value)) if value.is_nan()
    ));
    let invalid_character_height =
        state.configure_player_character_scene("player.mesh", "player.material", 1.0, f32::NAN);
    assert!(matches!(
        invalid_character_height,
        Err(BrowserGameStateError::InvalidModelSceneHeightOffset(value)) if value.is_nan()
    ));
}

#[test]
fn browser_game_state_replaces_configured_player_character_parts() {
    let mut state = BrowserGameState::new();
    state
        .configure_player_character_scene("old.mesh", "old.material", 1.0, 0.0)
        .unwrap();
    state.reset_game(0x0F6, 1).unwrap();
    state.set_player_mode(PlayerMode::DebugFly).unwrap();
    assert_eq!(state.render_mesh_items().unwrap()[0].mesh_label, "old.mesh");

    state
        .configure_player_character_scene_parts(
            vec![
                ("head.mesh".to_string(), "head.material".to_string()),
                ("body.mesh".to_string(), "body.material".to_string()),
            ],
            2.0,
            0.25,
        )
        .unwrap();

    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 2);
    assert_eq!(items[0].mesh_label, "head.mesh");
    assert_eq!(items[0].material_label, "head.material");
    assert_eq!(items[1].mesh_label, "body.mesh");
    assert_eq!(items[1].material_label, "body.material");
    let snapshot = state.player_character_scene_snapshot().unwrap().unwrap();
    assert!(snapshot.visible);
    assert!(snapshot.follows_player);
}

#[test]
fn browser_game_state_character_snapshot_is_absent_without_spawned_meshes() {
    let state = BrowserGameState::default();
    assert_eq!(state.terrain_preset(), DEFAULT_TERRAIN_PRESET);
    assert_eq!(state.player_character_scene_snapshot().unwrap(), None);

    let mut state = BrowserGameState::new();
    state
        .configure_player_character_scene_parts(Vec::new(), 1.0, 0.0)
        .unwrap();
    state.reset_game(0x0F6, 1).unwrap();

    assert_eq!(state.player_character_scene_snapshot().unwrap(), None);
}

#[test]
fn browser_game_state_errors_format_supported_failure_modes() {
    let cases = [
        (
            BrowserGameStateError::Engine(EngineError::MissingPlayer),
            "engine error",
        ),
        (
            BrowserGameStateError::ModelAnimation(ModelAssetError::InvalidAnimationTime),
            "model animation error",
        ),
        (
            BrowserGameStateError::InvalidTerrainHeight { x: 1.0, z: 2.0 },
            "terrain height was invalid",
        ),
        (
            BrowserGameStateError::InvalidModelSceneScale(0.0),
            "model scene scale was invalid",
        ),
        (
            BrowserGameStateError::InvalidModelSceneHeightOffset(f32::INFINITY),
            "model scene height offset was invalid",
        ),
        (
            BrowserGameStateError::MissingSceneMeshResource(MeshId::new(7, 2)),
            "scene mesh",
        ),
        (
            BrowserGameStateError::MissingSceneMaterialResource(MaterialId::new(8, 3)),
            "scene material",
        ),
    ];

    for (error, expected) in cases {
        assert!(
            error.to_string().contains(expected),
            "expected '{error}' to contain '{expected}'"
        );
    }
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

    assert_eq!(state.toggle_player_mode().unwrap(), PlayerMode::ThirdPerson);
    assert_eq!(state.player_mode().unwrap(), PlayerMode::ThirdPerson);
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
        crate::player_mode_from_code(PlayerMode::ThirdPerson.code()),
        Some(PlayerMode::ThirdPerson)
    );
    assert_eq!(
        crate::player_mode_from_code(PlayerMode::FirstPerson.code()),
        Some(PlayerMode::FirstPerson)
    );
    assert_eq!(crate::player_mode_from_code(99), None);
}

#[test]
fn browser_game_state_third_person_draws_character_while_grounding_player() {
    let mut state = BrowserGameState::new();
    state
        .configure_player_character_scene("player.mesh", "player.material", 1.0, 0.0)
        .unwrap();
    state.reset_game(0x0F6, 1).unwrap();
    state.set_player_mode(PlayerMode::ThirdPerson).unwrap();
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
    let items = state.render_mesh_items().unwrap();
    assert_eq!(items.len(), 1);
    assert_eq!(items[0].mesh_label, "player.mesh");
    assert_close(items[0].world_matrix[12], after.x);
    assert_close(items[0].world_matrix[13], after.y);
    assert_close(items[0].world_matrix[14], after.z);
    let snapshot = state.player_character_scene_snapshot().unwrap().unwrap();
    assert!(snapshot.visible);
    assert!(snapshot.follows_player);
    assert!(!snapshot.debug_marker_visible);
}

#[test]
fn browser_game_state_debug_fly_moves_camera_without_moving_player_character() {
    let mut state = BrowserGameState::new();
    state
        .configure_player_character_scene("player.mesh", "player.material", 1.0, 0.0)
        .unwrap();
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
    assert_eq!(items[0].mesh_label, "player.mesh");
    assert_eq!(items[0].material_label, "player.material");
    assert_close(items[0].world_matrix[12], player_position.x);
    assert_close(items[0].world_matrix[13], player_position.y);
    assert_close(items[0].world_matrix[14], player_position.z);
    let snapshot = state.player_character_scene_snapshot().unwrap().unwrap();
    assert!(snapshot.visible);
    assert!(snapshot.follows_player);
    assert!(!snapshot.debug_marker_visible);
}

#[test]
fn browser_terrain_stream_generates_and_prunes_meshes_in_rust() {
    let mut stream = BrowserTerrainStream::new_lod0(0x0F6, 1).unwrap();
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

    assert!(update.removed_nodes.is_empty());
    assert!(stream.loaded_chunk_keys().contains(&"3,0,0".to_string()));
    assert!(stream.render_chunk_keys().contains(&"0,0,0".to_string()));

    settle_terrain_stream(&mut stream, moved, 80);

    assert!(!stream.render_chunk_keys().contains(&"0,0,0".to_string()));
}

#[test]
fn browser_terrain_stream_default_bands_render_multiple_lods_after_settling() {
    let mut stream = BrowserTerrainStream::new(0x0F6, 1).unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);

    for _ in 0..1600 {
        stream.tick(origin);
        let status = stream.status();
        if !status.pending
            && status.rendered_chunk_count > 0
            && status.max_rendered_lod >= 3
            && status.visible_world_span_x_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS
            && status.visible_world_span_z_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS
        {
            break;
        }
    }

    let status = stream.status();
    let render_node_keys = stream.render_node_keys();
    let loaded_node_keys = stream.loaded_node_keys();

    assert!(status.rendered_chunk_count > 0);
    assert!(status.rendered_node_count > status.rendered_chunk_count);
    assert!(status.max_rendered_lod >= 3);
    assert!(status.visible_world_span_x_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS);
    assert!(status.visible_world_span_z_meters >= MIN_MULTI_KM_TERRAIN_SPAN_METERS);
    assert_eq!(status.pending, false);
    assert_eq!(status.missing_node_count, 0);
    assert_visible_stream_cover(&stream, origin);
    assert!(loaded_node_keys
        .iter()
        .any(|key| key.starts_with("lod4:") && key.contains(",-1,")));
    assert!(render_node_keys.iter().any(|key| key.starts_with("lod0:")));
    assert!(render_node_keys
        .iter()
        .any(|key| key.starts_with("lod3:") || key.starts_with("lod4:")));
}

#[test]
fn browser_terrain_stream_queues_worker_requests_without_sync_building() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![TerrainLodBand {
            lod: 0,
            horizontal_radius: 0,
            vertical_chunk_offsets: vec![0],
        }],
    )
    .unwrap();
    stream.configure_worker_runtime(2).unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);

    let update = stream.tick_for_workers(origin);
    assert!(update.upserted_meshes.is_empty());
    assert_eq!(stream.status().synchronous_build_count, 0);

    let requests = stream.take_worker_build_requests();
    assert_eq!(requests.len(), 1);
    let request = requests[0];
    assert_eq!(request.seed, 0x0F6);
    assert_eq!(request.preset, 1);
    assert_eq!(request.key.lod, 0);
    assert_eq!(stream.status().terrain_worker_count, 2);
    assert_eq!(stream.status().terrain_worker_in_flight_count, 1);
    assert_eq!(stream.status().terrain_worker_queued_request_count, 0);

    let mesh = build_node_mesh(request.seed, request.preset, request.key, 1.0);
    assert!(stream.complete_worker_build(BrowserTerrainBuildCompletion {
        request_id: request.request_id,
        generation: request.generation,
        key: request.key,
        vertices: mesh.vertices,
        indices: mesh.indices,
        failed: false,
    }));

    let update = stream.tick_for_workers(origin);
    assert_eq!(update.upserted_meshes.len(), 1);
    let status = stream.status();
    assert_eq!(status.terrain_worker_runtime, "browser-worker");
    assert_eq!(status.terrain_worker_completed_count, 1);
    assert_eq!(status.terrain_worker_in_flight_count, 0);
    assert_eq!(status.synchronous_build_count, 0);
    assert_eq!(status.pending, false);
}

#[test]
fn browser_terrain_stream_rejects_stale_worker_completions_and_retries() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![TerrainLodBand {
            lod: 0,
            horizontal_radius: 0,
            vertical_chunk_offsets: vec![0],
        }],
    )
    .unwrap();
    stream.configure_worker_runtime(1).unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);
    stream.tick_for_workers(origin);
    let request = stream.take_worker_build_requests()[0];
    let wrong_key = TerrainNodeKey {
        lod: request.key.lod,
        coord: terrain_core::TerrainChunkCoord {
            x: request.key.coord.x + 1,
            y: request.key.coord.y,
            z: request.key.coord.z,
        },
    };

    assert!(
        !stream.complete_worker_build(BrowserTerrainBuildCompletion {
            request_id: request.request_id,
            generation: request.generation,
            key: wrong_key,
            vertices: Vec::new(),
            indices: Vec::new(),
            failed: false,
        })
    );
    assert_eq!(stream.status().terrain_worker_stale_completion_count, 1);

    stream.tick_for_workers(origin);
    let retry = stream.take_worker_build_requests();
    assert_eq!(retry.len(), 1);
    assert_eq!(retry[0].key, request.key);
}

#[test]
fn browser_terrain_stream_rejects_worker_completions_after_reset() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![TerrainLodBand {
            lod: 0,
            horizontal_radius: 0,
            vertical_chunk_offsets: vec![0],
        }],
    )
    .unwrap();
    stream.configure_worker_runtime(1).unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);
    stream.tick_for_workers(origin);
    let request = stream.take_worker_build_requests()[0];

    stream.reset_around(Vec3::new(96.0, 0.0, 0.0));
    assert!(
        !stream.complete_worker_build(BrowserTerrainBuildCompletion {
            request_id: request.request_id,
            generation: request.generation,
            key: request.key,
            vertices: Vec::new(),
            indices: Vec::new(),
            failed: false,
        })
    );
    assert_eq!(stream.status().terrain_worker_stale_completion_count, 1);
}

#[test]
fn browser_terrain_stream_generates_unique_mesh_keys_across_lods() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![0, 1],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![0],
            },
        ],
    )
    .unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    stream.reset_around(origin);

    for _ in 0..240 {
        stream.tick(origin);
    }

    let render_node_keys = stream.render_node_keys();

    assert!(render_node_keys.contains(&"lod0:0,0,0".to_string()));
    assert!(render_node_keys.iter().any(|key| key.starts_with("lod1:")));
    assert!(stream.render_chunk_keys().contains(&"0,0,0".to_string()));

    let status = stream.status();
    assert!(status.rendered_node_count >= 2);
    assert!(status.max_rendered_lod >= 1);
    assert!(status
        .lod_summaries
        .iter()
        .any(|summary| summary.lod == 1 && summary.rendered_node_count > 0));
}

#[test]
fn browser_terrain_stream_keeps_current_position_covered_while_running() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![-1, 0, 1],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![-1, 0],
            },
            TerrainLodBand {
                lod: 2,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![-1, 0],
            },
        ],
    )
    .unwrap();
    let mut position = terrain_position(0x0F6, 1, 0.0, 0.0);
    stream.reset_around(position);
    settle_terrain_stream(&mut stream, position, 240);
    assert_visible_stream_cover(&stream, position);

    for step in 1..=48 {
        let x = step as f32 * 4.0;
        let z = step as f32 * 1.75;
        position = terrain_position(0x0F6, 1, x, z);
        stream.tick(position);
        assert_visible_stream_cover(&stream, position);

        for _ in 0..2 {
            stream.tick(position);
            assert_visible_stream_cover(&stream, position);
        }
    }

    settle_terrain_stream(&mut stream, position, 240);
    let status = stream.status();
    assert!(!status.pending);
    assert_eq!(status.missing_node_count, 0);
    assert_visible_stream_cover(&stream, position);
}

#[test]
fn browser_terrain_stream_swaps_parent_out_after_complete_child_group() {
    let mut stream = BrowserTerrainStream::new_with_lod_bands(
        0x0F6,
        1,
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![0, 1],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 0,
                vertical_chunk_offsets: vec![0],
            },
        ],
    )
    .unwrap();
    let origin = Vec3::new(0.0, 0.0, 0.0);
    let parent_key = "lod1:0,0,0".to_string();
    let child_key = "lod0:0,0,0".to_string();
    stream.reset_around(origin);

    let mut saw_parent_fallback = false;
    for _ in 0..120 {
        stream.tick(origin);
        let visible = stream.render_node_keys();
        if visible.contains(&parent_key) {
            assert!(!visible.iter().any(|key| key.starts_with("lod0:")));
            saw_parent_fallback = true;
            break;
        }
    }

    assert!(saw_parent_fallback);

    for _ in 0..240 {
        stream.tick(origin);
        let visible = stream.render_node_keys();
        if visible.contains(&child_key) && !visible.contains(&parent_key) {
            break;
        }
    }

    let visible = stream.render_node_keys();
    assert!(visible.contains(&child_key));
    assert!(!visible.contains(&parent_key));
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
        12.0,
        0.04,
        0.25,
        0.0,
        2.25,
        0.44,
        0.018,
        1.35,
        0.18,
        0.42,
        0.1,
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

fn single_translation_clip(name: &str, x: f32) -> ModelAnimationClip {
    ModelAnimationClip {
        name: Some(name.to_string()),
        duration_seconds: 1.0,
        channels: vec![ModelAnimationChannel {
            target_node: 0,
            target: ModelAnimationTarget::Translation,
            interpolation: ModelAnimationInterpolation::Linear,
            inputs: vec![0.0, 1.0],
            outputs: ModelAnimationOutputs::Translations(vec![[x, 0.0, 0.0], [x, 0.0, 0.0]]),
        }],
    }
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

fn test_skin_model(nodes: Vec<ModelNode>) -> ModelAsset {
    let joint_count = nodes.len();
    ModelAsset {
        nodes,
        primitives: Vec::new(),
        images: Vec::new(),
        textures: Vec::new(),
        samplers: Vec::new(),
        materials: Vec::<ModelMaterial>::new(),
        animations: Vec::new(),
        skins: vec![ModelSkin {
            name: Some("test-skin".to_string()),
            joints: (0..joint_count).collect(),
            inverse_bind_matrices: vec![identity_matrix(); joint_count],
        }],
    }
}

fn test_skinned_primitive(
    position: [f32; 3],
    normal: [f32; 3],
    joints0: [u16; 4],
    weights0: [f32; 4],
) -> ModelPrimitive {
    ModelPrimitive {
        mesh_index: 0,
        mesh_name: None,
        material: None,
        vertices: vec![ModelVertex {
            position,
            normal,
            texcoord0: [0.0, 0.0],
            color0: [1.0, 1.0, 1.0, 1.0],
            joints0,
            weights0,
        }],
        indices: vec![0],
    }
}

fn animation_gltf(
    interpolation: &str,
    target_path: &str,
    inputs: &[f32],
    outputs: &[f32],
    output_type: &str,
    output_count: usize,
) -> Vec<u8> {
    let mut bytes = Vec::with_capacity((inputs.len() + outputs.len()) * std::mem::size_of::<f32>());
    for value in inputs.iter().chain(outputs.iter()) {
        bytes.extend_from_slice(&value.to_le_bytes());
    }

    let input_byte_length = inputs.len() * std::mem::size_of::<f32>();
    let output_byte_offset = input_byte_length;
    let output_byte_length = outputs.len() * std::mem::size_of::<f32>();
    let encoded = base64::encode(&bytes);

    format!(
        r#"{{
  "asset": {{ "version": "2.0" }},
  "nodes": [{{}}],
  "buffers": [
    {{
      "uri": "data:application/octet-stream;base64,{encoded}",
      "byteLength": {byte_length}
    }}
  ],
  "bufferViews": [
    {{ "buffer": 0, "byteOffset": 0, "byteLength": {input_byte_length} }},
    {{ "buffer": 0, "byteOffset": {output_byte_offset}, "byteLength": {output_byte_length} }}
  ],
  "accessors": [
    {{
      "bufferView": 0,
      "componentType": 5126,
      "count": {input_count},
      "type": "SCALAR",
      "min": [0.0],
      "max": [1.0]
    }},
    {{
      "bufferView": 1,
      "componentType": 5126,
      "count": {output_count},
      "type": "{output_type}"
    }}
  ],
  "animations": [
    {{
      "channels": [
        {{ "sampler": 0, "target": {{ "node": 0, "path": "{target_path}" }} }}
      ],
      "samplers": [
        {{ "input": 0, "interpolation": "{interpolation}", "output": 1 }}
      ]
    }}
  ]
}}"#,
        byte_length = bytes.len(),
        input_count = inputs.len(),
    )
    .into_bytes()
}

fn identity_matrix() -> [f32; 16] {
    translation_matrix(0.0, 0.0, 0.0)
}

fn translation_matrix(x: f32, y: f32, z: f32) -> [f32; 16] {
    [
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, x, y, z, 1.0,
    ]
}

fn terrain_position(seed: u32, preset: u32, x: f32, z: f32) -> Vec3 {
    Vec3::new(x, height_at(seed, preset, x as f64, z as f64) as f32, z)
}

fn settle_terrain_stream(stream: &mut BrowserTerrainStream, position: Vec3, max_ticks: usize) {
    for _ in 0..max_ticks {
        stream.tick(position);
        if !stream.status().pending {
            return;
        }
    }
}

fn assert_visible_stream_cover(stream: &BrowserTerrainStream, position: Vec3) {
    let visible_nodes = stream.render_nodes();
    assert!(!visible_nodes.is_empty());
    assert_no_visible_parent_child_overlap(&visible_nodes);
    assert!(
        visible_nodes
            .iter()
            .any(|key| node_covers_position(*key, position)),
        "no visible terrain node covered player position {position:?}; visible nodes: {visible_nodes:?}"
    );
}

fn assert_no_visible_parent_child_overlap(visible_nodes: &[TerrainNodeKey]) {
    let visible = visible_nodes.iter().copied().collect::<BTreeSet<_>>();
    for key in visible_nodes {
        let mut ancestor = terrain_node_parent(*key);
        while let Some(parent) = ancestor {
            assert!(
                !visible.contains(&parent),
                "visible terrain nodes overlap parent {parent:?} and child {key:?}"
            );
            ancestor = terrain_node_parent(parent);
        }
    }
}

fn node_covers_position(key: TerrainNodeKey, position: Vec3) -> bool {
    let node_size = terrain_node_cell_size(1.0, key.lod) * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let min_x = key.coord.x as f64 * node_size;
    let min_y = key.coord.y as f64 * node_size;
    let min_z = key.coord.z as f64 * node_size;
    let x = position.x as f64;
    let y = position.y as f64;
    let z = position.z as f64;

    x >= min_x
        && x < min_x + node_size
        && y >= min_y
        && y < min_y + node_size
        && z >= min_z
        && z < min_z + node_size
}

fn assert_close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 0.00001,
        "expected {actual} to be close to {expected}"
    );
}
