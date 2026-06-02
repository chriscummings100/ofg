use std::sync::{Mutex, OnceLock};

use crate::config::REQUIRED_TEXTURE_ARRAY_LAYERS;
use crate::renderer::{RendererState, RendererStateError};
use crate::resources::ResourceHandle;
use crate::ENGINE_WEB_VERSION;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FacadeErrorCode {
    None = 0,
    NotConfigured = 1,
    InvalidCanvasSize = 2,
    InsufficientTextureArrayLayers = 3,
    InvalidMesh = 4,
    InvalidTexture = 5,
    UnsupportedTextureFormat = 6,
    StaleHandle = 7,
}

struct FacadeRenderer {
    renderer: RendererState,
    last_error: FacadeErrorCode,
}

impl FacadeRenderer {
    fn new() -> Self {
        Self {
            renderer: RendererState::new(),
            last_error: FacadeErrorCode::None,
        }
    }

    fn reset(&mut self) {
        *self = Self::new();
    }

    fn ok(&mut self) -> u32 {
        self.last_error = FacadeErrorCode::None;
        1
    }

    fn fail(&mut self, error: RendererStateError) -> u32 {
        self.last_error = FacadeErrorCode::from(error);
        0
    }

    fn handle_result(&mut self, result: Result<ResourceHandle, RendererStateError>) -> u64 {
        match result {
            Ok(handle) => {
                self.last_error = FacadeErrorCode::None;
                handle.raw()
            }
            Err(error) => {
                self.last_error = FacadeErrorCode::from(error);
                ResourceHandle::INVALID_RAW
            }
        }
    }
}

fn with_facade_renderer<R>(callback: impl FnOnce(&mut FacadeRenderer) -> R) -> R {
    static FACADE_RENDERER: OnceLock<Mutex<FacadeRenderer>> = OnceLock::new();
    let mutex = FACADE_RENDERER.get_or_init(|| Mutex::new(FacadeRenderer::new()));
    let mut renderer = match mutex.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    };

    callback(&mut renderer)
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_version() -> u32 {
    ENGINE_WEB_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_required_texture_array_layers() -> u32 {
    REQUIRED_TEXTURE_ARRAY_LAYERS
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_reset() {
    with_facade_renderer(|facade| facade.reset());
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_configure(
    canvas_width: u32,
    canvas_height: u32,
    max_texture_array_layers: u32,
) -> u32 {
    with_facade_renderer(|facade| {
        match facade
            .renderer
            .configure(canvas_width, canvas_height, max_texture_array_layers)
        {
            Ok(()) => facade.ok(),
            Err(error) => facade.fail(error),
        }
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_configured() -> u32 {
    with_facade_renderer(|facade| u32::from(facade.renderer.is_configured()))
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_resize(canvas_width: u32, canvas_height: u32) -> u32 {
    with_facade_renderer(
        |facade| match facade.renderer.resize(canvas_width, canvas_height) {
            Ok(()) => facade.ok(),
            Err(error) => facade.fail(error),
        },
    )
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_canvas_width() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .config()
            .map(|config| config.canvas_width())
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_canvas_height() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .config()
            .map(|config| config.canvas_height())
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_max_texture_array_layers() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .config()
            .map(|config| config.max_texture_array_layers())
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_register_mesh(
    vertex_float_count: u32,
    index_count: u32,
    floats_per_vertex: u32,
) -> u64 {
    with_facade_renderer(|facade| {
        let result =
            facade
                .renderer
                .register_mesh(vertex_float_count, index_count, floats_per_vertex);
        facade.handle_result(result)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_destroy_mesh(handle: u64) -> u32 {
    with_facade_renderer(|facade| {
        let Some(handle) = ResourceHandle::from_raw(handle) else {
            facade.last_error = FacadeErrorCode::StaleHandle;
            return 0;
        };

        match facade.renderer.unregister_mesh(handle) {
            Ok(_) => facade.ok(),
            Err(error) => facade.fail(error),
        }
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_register_texture(
    width: u32,
    height: u32,
    layers: u32,
    format_code: u32,
) -> u64 {
    with_facade_renderer(|facade| {
        let result = facade
            .renderer
            .register_texture(width, height, layers, format_code);
        facade.handle_result(result)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_destroy_texture(handle: u64) -> u32 {
    with_facade_renderer(|facade| {
        let Some(handle) = ResourceHandle::from_raw(handle) else {
            facade.last_error = FacadeErrorCode::StaleHandle;
            return 0;
        };

        match facade.renderer.unregister_texture(handle) {
            Ok(_) => facade.ok(),
            Err(error) => facade.fail(error),
        }
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_register_object() -> u64 {
    with_facade_renderer(|facade| {
        let result = facade.renderer.register_object();
        facade.handle_result(result)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_destroy_object(handle: u64) -> u32 {
    with_facade_renderer(|facade| {
        let Some(handle) = ResourceHandle::from_raw(handle) else {
            facade.last_error = FacadeErrorCode::StaleHandle;
            return 0;
        };

        match facade.renderer.unregister_object(handle) {
            Ok(()) => facade.ok(),
            Err(error) => facade.fail(error),
        }
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_begin_frame(canvas_width: u32, canvas_height: u32) -> u32 {
    with_facade_renderer(
        |facade| match facade.renderer.begin_frame(canvas_width, canvas_height) {
            Ok(()) => facade.ok(),
            Err(error) => facade.fail(error),
        },
    )
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_note_draw(mesh_handle: u64, object_handle: u64) -> u32 {
    with_facade_renderer(|facade| {
        let Some(mesh_handle) = ResourceHandle::from_raw(mesh_handle) else {
            facade.last_error = FacadeErrorCode::StaleHandle;
            return 0;
        };
        let Some(object_handle) = ResourceHandle::from_raw(object_handle) else {
            facade.last_error = FacadeErrorCode::StaleHandle;
            return 0;
        };

        match facade.renderer.note_draw(mesh_handle, object_handle) {
            Ok(()) => facade.ok(),
            Err(error) => facade.fail(error),
        }
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_mesh_count() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .resource_counts()
            .meshes
            .min(u32::MAX as usize) as u32
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_texture_count() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .resource_counts()
            .textures
            .min(u32::MAX as usize) as u32
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_object_count() -> u32 {
    with_facade_renderer(|facade| {
        facade
            .renderer
            .resource_counts()
            .objects
            .min(u32::MAX as usize) as u32
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_frame_index() -> u64 {
    with_facade_renderer(|facade| facade.renderer.frame_index())
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_frame_draw_count() -> u32 {
    with_facade_renderer(|facade| facade.renderer.frame_draw_count())
}

#[no_mangle]
pub extern "C" fn ofg_engine_web_last_error_code() -> u32 {
    with_facade_renderer(|facade| facade.last_error as u32)
}

impl From<RendererStateError> for FacadeErrorCode {
    fn from(error: RendererStateError) -> Self {
        match error {
            RendererStateError::NotConfigured => Self::NotConfigured,
            RendererStateError::InvalidCanvasSize => Self::InvalidCanvasSize,
            RendererStateError::InsufficientTextureArrayLayers => {
                Self::InsufficientTextureArrayLayers
            }
            RendererStateError::InvalidMesh => Self::InvalidMesh,
            RendererStateError::InvalidTexture => Self::InvalidTexture,
            RendererStateError::UnsupportedTextureFormat => Self::UnsupportedTextureFormat,
            RendererStateError::StaleHandle => Self::StaleHandle,
        }
    }
}
