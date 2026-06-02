use crate::{
    RendererState, RendererStateError, ResourceHandle, REQUIRED_TEXTURE_ARRAY_LAYERS,
    TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
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

fn configured_renderer() -> RendererState {
    let mut renderer = RendererState::new();
    renderer
        .configure(1280, 720, REQUIRED_TEXTURE_ARRAY_LAYERS)
        .unwrap();
    renderer
}
