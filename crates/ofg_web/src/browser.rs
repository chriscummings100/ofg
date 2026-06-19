//! WASM-only browser runtime that owns WebGPU surface rendering.

use ofg_core::FrameState;
use ofg_render::BootstrapRenderer;
use wasm_bindgen::prelude::*;

use crate::RuntimeDebugStatus;

#[wasm_bindgen]
pub struct BrowserGame {
    frame_state: FrameState,
    runtime: Option<BrowserWgpuRuntime>,
    last_error: Option<String>,
}

#[wasm_bindgen]
impl BrowserGame {
    #[wasm_bindgen(js_name = create)]
    pub async fn create(canvas: web_sys::HtmlCanvasElement) -> Result<BrowserGame, JsValue> {
        console_error_panic_hook::set_once();
        let runtime = BrowserWgpuRuntime::new(canvas).await?;
        Ok(Self {
            frame_state: FrameState::new(),
            runtime: Some(runtime),
            last_error: None,
        })
    }

    pub fn resize(
        &mut self,
        width: u32,
        height: u32,
        device_pixel_ratio: f64,
    ) -> Result<(), JsValue> {
        let result = self
            .runtime
            .as_mut()
            .ok_or_else(disposed_error)
            .and_then(|runtime| runtime.resize(width, height, device_pixel_ratio));
        match result {
            Ok(()) => {
                self.last_error = None;
                Ok(())
            }
            Err(error) => {
                self.last_error = Some(error.clone());
                Err(js_error(error))
            }
        }
    }

    pub fn frame(&mut self, time_ms: f64) -> Result<(), JsValue> {
        let result = match self.runtime.as_mut() {
            Some(runtime) => {
                self.frame_state.tick(time_ms);
                runtime.render()
            }
            None => Err(disposed_error()),
        };
        match result {
            Ok(()) => {
                self.last_error = None;
                Ok(())
            }
            Err(error) => {
                self.last_error = Some(error.clone());
                Err(js_error(error))
            }
        }
    }

    pub fn debug_status_json(&self) -> String {
        match self.runtime.as_ref() {
            Some(runtime) => runtime
                .status(&self.frame_state, self.last_error.clone())
                .to_json(),
            None => RuntimeDebugStatus::uninitialized(disposed_error()).to_json(),
        }
    }

    pub fn dispose(&mut self) {
        self.runtime = None;
        self.last_error = Some(disposed_error());
    }
}

struct BrowserWgpuRuntime {
    instance: wgpu::Instance,
    canvas: web_sys::HtmlCanvasElement,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    renderer: BootstrapRenderer,
    config: Option<wgpu::SurfaceConfiguration>,
    format: wgpu::TextureFormat,
    alpha_mode: wgpu::CompositeAlphaMode,
    adapter_name: String,
    backend: String,
    width: u32,
    height: u32,
    device_pixel_ratio: f64,
    surface_configure_count: u32,
}

impl BrowserWgpuRuntime {
    async fn new(canvas: web_sys::HtmlCanvasElement) -> Result<Self, JsValue> {
        let mut instance_descriptor = wgpu::InstanceDescriptor::new_without_display_handle();
        instance_descriptor.backends = wgpu::Backends::BROWSER_WEBGPU;
        let instance = wgpu::Instance::new(instance_descriptor);
        let surface = instance
            .create_surface(wgpu::SurfaceTarget::Canvas(canvas.clone()))
            .map_err(js_error)?;
        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                force_fallback_adapter: false,
                compatible_surface: Some(&surface),
            })
            .await
            .map_err(js_error)?;
        let adapter_info = adapter.get_info();
        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("ofg bootstrap device"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::downlevel_webgl2_defaults(),
                ..Default::default()
            })
            .await
            .map_err(js_error)?;
        let capabilities = surface.get_capabilities(&adapter);
        let format = capabilities
            .formats
            .iter()
            .copied()
            .find(|format| format.is_srgb())
            .unwrap_or_else(|| capabilities.formats[0]);
        let alpha_mode = capabilities
            .alpha_modes
            .iter()
            .copied()
            .find(|mode| *mode == wgpu::CompositeAlphaMode::Opaque)
            .unwrap_or(wgpu::CompositeAlphaMode::Auto);
        let renderer = BootstrapRenderer::new(&device, format);

        let mut runtime = Self {
            instance,
            canvas,
            surface,
            device,
            queue,
            renderer,
            config: None,
            format,
            alpha_mode,
            adapter_name: adapter_info.name,
            backend: format!("{:?}", adapter_info.backend),
            width: 0,
            height: 0,
            device_pixel_ratio: 1.0,
            surface_configure_count: 0,
        };
        runtime
            .resize(runtime.canvas.width(), runtime.canvas.height(), 1.0)
            .map_err(js_error)?;
        Ok(runtime)
    }

    fn resize(&mut self, width: u32, height: u32, device_pixel_ratio: f64) -> Result<(), String> {
        if !device_pixel_ratio.is_finite() || device_pixel_ratio <= 0.0 {
            return Err(format!(
                "Device pixel ratio must be a positive finite number, got {device_pixel_ratio}."
            ));
        }

        let should_configure = needs_surface_configure(
            self.width,
            self.height,
            self.device_pixel_ratio,
            self.config.is_some(),
            width,
            height,
            device_pixel_ratio,
        );
        self.canvas.set_width(width);
        self.canvas.set_height(height);
        self.device_pixel_ratio = device_pixel_ratio;

        if width == 0 || height == 0 {
            self.width = width;
            self.height = height;
            self.config = None;
            return Ok(());
        }

        if !should_configure {
            return Ok(());
        }

        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: self.format,
            width,
            height,
            present_mode: wgpu::PresentMode::Fifo,
            desired_maximum_frame_latency: 2,
            alpha_mode: self.alpha_mode,
            view_formats: vec![],
        };
        self.surface.configure(&self.device, &config);
        self.config = Some(config);
        self.width = width;
        self.height = height;
        self.surface_configure_count = self.surface_configure_count.saturating_add(1);
        Ok(())
    }

    fn render(&mut self) -> Result<(), String> {
        if self.config.is_none() {
            return Ok(());
        }

        let (frame, reconfigure_after_present) = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame) => (frame, false),
            wgpu::CurrentSurfaceTexture::Suboptimal(frame) => (frame, true),
            wgpu::CurrentSurfaceTexture::Timeout | wgpu::CurrentSurfaceTexture::Occluded => {
                return Ok(());
            }
            wgpu::CurrentSurfaceTexture::Outdated => {
                self.reconfigure_surface();
                return Ok(());
            }
            wgpu::CurrentSurfaceTexture::Lost => {
                self.recreate_surface()?;
                return Ok(());
            }
            other => return Err(format!("Failed to acquire WebGPU frame: {other:?}")),
        };
        let view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("ofg bootstrap frame encoder"),
            });
        self.renderer.render_to_view(&mut encoder, &view);
        self.queue.submit(std::iter::once(encoder.finish()));
        frame.present();
        if reconfigure_after_present {
            self.reconfigure_surface();
        }
        Ok(())
    }

    fn reconfigure_surface(&mut self) {
        if let Some(config) = self.config.as_ref() {
            self.surface.configure(&self.device, config);
            self.surface_configure_count = self.surface_configure_count.saturating_add(1);
        }
    }

    fn recreate_surface(&mut self) -> Result<(), String> {
        self.surface = self
            .instance
            .create_surface(wgpu::SurfaceTarget::Canvas(self.canvas.clone()))
            .map_err(|error| error.to_string())?;
        self.reconfigure_surface();
        Ok(())
    }

    fn status(&self, frame_state: &FrameState, last_error: Option<String>) -> RuntimeDebugStatus {
        let counters = self.renderer.counters();
        RuntimeDebugStatus {
            initialized: self.config.is_some(),
            frame_count: frame_state.frame_count(),
            canvas_width: self.width,
            canvas_height: self.height,
            device_pixel_ratio: self.device_pixel_ratio,
            surface_format: format!("{:?}", self.format),
            adapter_name: self.adapter_name.clone(),
            backend: self.backend.clone(),
            pipeline_create_count: counters.pipeline_create_count,
            buffer_create_count: counters.buffer_create_count,
            surface_configure_count: self.surface_configure_count,
            last_error,
        }
    }
}

fn js_error(error: impl std::fmt::Display) -> JsValue {
    JsValue::from_str(&error.to_string())
}

fn disposed_error() -> String {
    "Browser game runtime has been disposed.".to_string()
}

fn needs_surface_configure(
    current_width: u32,
    current_height: u32,
    current_device_pixel_ratio: f64,
    has_config: bool,
    next_width: u32,
    next_height: u32,
    next_device_pixel_ratio: f64,
) -> bool {
    !has_config
        || current_width != next_width
        || current_height != next_height
        || current_device_pixel_ratio != next_device_pixel_ratio
}

#[cfg(test)]
mod tests {
    use wasm_bindgen_test::wasm_bindgen_test;

    use super::needs_surface_configure;

    #[wasm_bindgen_test]
    fn dpr_only_changes_require_surface_reconfigure() {
        assert!(!needs_surface_configure(800, 450, 1.0, true, 800, 450, 1.0));
        assert!(needs_surface_configure(800, 450, 1.0, true, 800, 450, 1.5));
        assert!(needs_surface_configure(800, 450, 1.0, false, 800, 450, 1.0));
    }
}
