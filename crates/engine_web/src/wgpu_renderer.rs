use std::borrow::Cow;

use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use wgpu::util::DeviceExt;

use crate::config::{
    REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
};
use crate::render_uniforms::{
    build_frame_uniform_values, build_object_uniform_values, FRAME_PACKET_FLOATS,
    FRAME_UNIFORM_FLOATS, MATERIAL_PACKET_FLOATS, OBJECT_UNIFORM_FLOATS, WORLD_MATRIX_FLOATS,
};
use crate::resources::{ResourceHandle, ResourceStore};
use crate::ENGINE_WEB_VERSION;

const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth24Plus;
const SHADER_SOURCE: &str = include_str!("../../../src/engine/render/shaders/uber.wgsl");

#[wasm_bindgen]
pub struct RustWgpuRenderer {
    renderer: BrowserWgpuRenderer,
}

#[derive(Debug)]
#[wasm_bindgen]
pub struct RustWgpuRendererStatus {
    version: u32,
    configured: bool,
    canvas_width: u32,
    canvas_height: u32,
    required_texture_array_layers: u32,
    max_texture_array_layers: u32,
    mesh_count: u32,
    texture_count: u32,
    object_count: u32,
    frame_index: u32,
    frame_draw_count: u32,
}

struct BrowserWgpuRenderer {
    canvas: web_sys::HtmlCanvasElement,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    depth_texture: wgpu::Texture,
    camera_uniform_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,
    object_bind_group_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    sky_pipeline: wgpu::RenderPipeline,
    pipeline: wgpu::RenderPipeline,
    max_texture_array_layers: u32,
    meshes: ResourceStore<GpuMesh>,
    textures: ResourceStore<GpuTexture>,
    objects: ResourceStore<GpuObject>,
    fallback_albedo: ResourceHandle,
    fallback_normal: ResourceHandle,
    fallback_material: ResourceHandle,
    frame_index: u32,
    frame_draw_count: u32,
}

struct GpuMesh {
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    index_count: u32,
}

struct GpuTexture {
    view: wgpu::TextureView,
}

struct GpuObject {
    uniform_buffer: wgpu::Buffer,
    bind_group: Option<wgpu::BindGroup>,
    albedo_texture: Option<ResourceHandle>,
    normal_texture: Option<ResourceHandle>,
    material_texture: Option<ResourceHandle>,
}

#[wasm_bindgen]
impl RustWgpuRenderer {
    #[wasm_bindgen(js_name = create)]
    pub async fn create(canvas: web_sys::HtmlCanvasElement) -> Result<RustWgpuRenderer, JsValue> {
        console_error_panic_hook::set_once();
        let renderer = BrowserWgpuRenderer::new(canvas).await?;
        Ok(Self { renderer })
    }

    #[wasm_bindgen(js_name = resize)]
    pub fn resize(&mut self, width: u32, height: u32) -> Result<(), JsValue> {
        self.renderer.resize(width, height)
    }

    #[wasm_bindgen(js_name = registerMesh)]
    pub fn register_mesh(
        &mut self,
        vertices: &[f32],
        indices: &[u32],
        floats_per_vertex: u32,
    ) -> Result<f64, JsValue> {
        self.renderer
            .register_mesh(vertices, indices, floats_per_vertex)
            .map(handle_to_js)
    }

    #[wasm_bindgen(js_name = destroyMesh)]
    pub fn destroy_mesh(&mut self, handle: f64) -> Result<(), JsValue> {
        self.renderer.destroy_mesh(handle_from_js(handle)?)
    }

    #[wasm_bindgen(js_name = registerTexture)]
    pub fn register_texture(
        &mut self,
        width: u32,
        height: u32,
        layers: u32,
        format_code: u32,
        data: &[u8],
    ) -> Result<f64, JsValue> {
        self.renderer
            .register_texture(width, height, layers, format_code, data)
            .map(handle_to_js)
    }

    #[wasm_bindgen(js_name = destroyTexture)]
    pub fn destroy_texture(&mut self, handle: f64) -> Result<(), JsValue> {
        self.renderer.destroy_texture(handle_from_js(handle)?)
    }

    #[wasm_bindgen(js_name = registerObject)]
    pub fn register_object(&mut self) -> Result<f64, JsValue> {
        self.renderer.register_object().map(handle_to_js)
    }

    #[wasm_bindgen(js_name = destroyObject)]
    pub fn destroy_object(&mut self, handle: f64) -> Result<(), JsValue> {
        self.renderer.destroy_object(handle_from_js(handle)?)
    }

    #[wasm_bindgen(js_name = render)]
    pub fn render(
        &mut self,
        frame_packet: &[f32],
        mesh_handles: &[f64],
        object_handles: &[f64],
        albedo_texture_handles: &[f64],
        normal_texture_handles: &[f64],
        material_texture_handles: &[f64],
        world_matrices: &[f32],
        material_packets: &[f32],
    ) -> Result<(), JsValue> {
        self.renderer.render(
            frame_packet,
            mesh_handles,
            object_handles,
            albedo_texture_handles,
            normal_texture_handles,
            material_texture_handles,
            world_matrices,
            material_packets,
        )
    }

    #[wasm_bindgen(js_name = status)]
    pub fn status(&self) -> RustWgpuRendererStatus {
        self.renderer.status()
    }

    #[wasm_bindgen(js_name = fallbackAlbedoTextureHandle)]
    pub fn fallback_albedo_texture_handle(&self) -> f64 {
        handle_to_js(self.renderer.fallback_albedo)
    }

    #[wasm_bindgen(js_name = fallbackNormalTextureHandle)]
    pub fn fallback_normal_texture_handle(&self) -> f64 {
        handle_to_js(self.renderer.fallback_normal)
    }

    #[wasm_bindgen(js_name = fallbackMaterialTextureHandle)]
    pub fn fallback_material_texture_handle(&self) -> f64 {
        handle_to_js(self.renderer.fallback_material)
    }
}

#[wasm_bindgen]
impl RustWgpuRendererStatus {
    #[wasm_bindgen(getter)]
    pub fn version(&self) -> u32 {
        self.version
    }

    #[wasm_bindgen(getter)]
    pub fn runtime(&self) -> String {
        "rust-wgpu".to_string()
    }

    #[wasm_bindgen(getter, js_name = configured)]
    pub fn configured(&self) -> bool {
        self.configured
    }

    #[wasm_bindgen(getter, js_name = canvasWidth)]
    pub fn canvas_width(&self) -> u32 {
        self.canvas_width
    }

    #[wasm_bindgen(getter, js_name = canvasHeight)]
    pub fn canvas_height(&self) -> u32 {
        self.canvas_height
    }

    #[wasm_bindgen(getter, js_name = requiredTextureArrayLayers)]
    pub fn required_texture_array_layers(&self) -> u32 {
        self.required_texture_array_layers
    }

    #[wasm_bindgen(getter, js_name = maxTextureArrayLayers)]
    pub fn max_texture_array_layers(&self) -> u32 {
        self.max_texture_array_layers
    }

    #[wasm_bindgen(getter, js_name = meshCount)]
    pub fn mesh_count(&self) -> u32 {
        self.mesh_count
    }

    #[wasm_bindgen(getter, js_name = textureCount)]
    pub fn texture_count(&self) -> u32 {
        self.texture_count
    }

    #[wasm_bindgen(getter, js_name = objectCount)]
    pub fn object_count(&self) -> u32 {
        self.object_count
    }

    #[wasm_bindgen(getter, js_name = frameIndex)]
    pub fn frame_index(&self) -> u32 {
        self.frame_index
    }

    #[wasm_bindgen(getter, js_name = frameDrawCount)]
    pub fn frame_draw_count(&self) -> u32 {
        self.frame_draw_count
    }
}

impl BrowserWgpuRenderer {
    async fn new(canvas: web_sys::HtmlCanvasElement) -> Result<Self, JsValue> {
        let display_width = canvas.width().max(1);
        let display_height = canvas.height().max(1);
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::BROWSER_WEBGPU,
            ..Default::default()
        });
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
            .ok_or_else(|| js_error("No browser WebGPU adapter is available."))?;

        if adapter.limits().max_texture_array_layers < REQUIRED_TEXTURE_ARRAY_LAYERS {
            return Err(js_error(format!(
                "WebGPU adapter only supports {} texture array layers; terrain requires at least {}.",
                adapter.limits().max_texture_array_layers,
                REQUIRED_TEXTURE_ARRAY_LAYERS
            )));
        }
        let max_texture_array_layers = adapter.limits().max_texture_array_layers;

        let mut limits =
            wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
        limits.max_texture_array_layers = limits
            .max_texture_array_layers
            .max(REQUIRED_TEXTURE_ARRAY_LAYERS);
        let (device, queue) = adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    label: Some("ofg rust webgpu device"),
                    required_features: wgpu::Features::empty(),
                    required_limits: limits,
                },
                None,
            )
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
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: display_width,
            height: display_height,
            present_mode: wgpu::PresentMode::Fifo,
            desired_maximum_frame_latency: 2,
            alpha_mode,
            view_formats: vec![],
        };
        surface.configure(&device, &config);

        let depth_texture = create_depth_texture(&device, display_width, display_height);
        let camera_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("camera uniforms"),
            size: uniform_byte_len(FRAME_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let camera_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("camera bind group layout"),
                entries: &[wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                }],
            });
        let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("camera bind group"),
            layout: &camera_bind_group_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: camera_uniform_buffer.as_entire_binding(),
            }],
        });
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("terrain texture sampler"),
            address_mode_u: wgpu::AddressMode::Repeat,
            address_mode_v: wgpu::AddressMode::Repeat,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let object_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("object bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Uniform,
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                    texture_binding(1),
                    texture_binding(2),
                    texture_binding(3),
                    wgpu::BindGroupLayoutEntry {
                        binding: 4,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                        count: None,
                    },
                ],
            });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("uber shader"),
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(SHADER_SOURCE)),
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("terrain pipeline layout"),
            bind_group_layouts: &[&camera_bind_group_layout, &object_bind_group_layout],
            push_constant_ranges: &[],
        });
        let sky_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("sky pipeline layout"),
            bind_group_layouts: &[&camera_bind_group_layout],
            push_constant_ranges: &[],
        });
        let pipeline = create_main_pipeline(&device, &pipeline_layout, &shader, format);
        let sky_pipeline = create_sky_pipeline(&device, &sky_pipeline_layout, &shader, format);
        let mut renderer = Self {
            canvas,
            surface,
            device,
            queue,
            config,
            depth_texture,
            camera_uniform_buffer,
            camera_bind_group,
            object_bind_group_layout,
            sampler,
            sky_pipeline,
            pipeline,
            max_texture_array_layers,
            meshes: ResourceStore::new(),
            textures: ResourceStore::new(),
            objects: ResourceStore::new(),
            fallback_albedo: ResourceHandle::INVALID,
            fallback_normal: ResourceHandle::INVALID,
            fallback_material: ResourceHandle::INVALID,
            frame_index: 0,
            frame_draw_count: 0,
        };
        renderer.create_fallback_textures()?;
        Ok(renderer)
    }

    fn resize(&mut self, width: u32, height: u32) -> Result<(), JsValue> {
        if width == 0 || height == 0 {
            return Err(js_error(
                "Rust WebGPU renderer rejected a zero-sized canvas.",
            ));
        }

        if self.config.width == width && self.config.height == height {
            return Ok(());
        }

        self.config.width = width;
        self.config.height = height;
        self.canvas.set_width(width);
        self.canvas.set_height(height);
        self.surface.configure(&self.device, &self.config);
        self.depth_texture = create_depth_texture(&self.device, width, height);
        Ok(())
    }

    fn register_mesh(
        &mut self,
        vertices: &[f32],
        indices: &[u32],
        floats_per_vertex: u32,
    ) -> Result<ResourceHandle, JsValue> {
        if floats_per_vertex != TERRAIN_VERTEX_FLOATS
            || vertices.is_empty()
            || indices.is_empty()
            || vertices.len() % TERRAIN_VERTEX_FLOATS as usize != 0
            || indices.len() % 3 != 0
        {
            return Err(js_error("Rust WebGPU renderer rejected an invalid mesh."));
        }

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("mesh vertices"),
                contents: f32_as_bytes(vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
        let index_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("mesh indices"),
                contents: u32_as_bytes(indices),
                usage: wgpu::BufferUsages::INDEX,
            });

        Ok(self.meshes.insert(GpuMesh {
            vertex_buffer,
            index_buffer,
            index_count: indices.len() as u32,
        }))
    }

    fn destroy_mesh(&mut self, handle: ResourceHandle) -> Result<(), JsValue> {
        let mesh = self
            .meshes
            .remove(handle)
            .map_err(|_| js_error("Rust WebGPU renderer rejected a stale mesh handle."))?;
        mesh.vertex_buffer.destroy();
        mesh.index_buffer.destroy();
        Ok(())
    }

    fn register_texture(
        &mut self,
        width: u32,
        height: u32,
        layers: u32,
        format_code: u32,
        data: &[u8],
    ) -> Result<ResourceHandle, JsValue> {
        if width == 0 || height == 0 || layers == 0 || layers > REQUIRED_TEXTURE_ARRAY_LAYERS {
            return Err(js_error(
                "Rust WebGPU renderer rejected an invalid texture shape.",
            ));
        }
        if format_code != TEXTURE_FORMAT_RGBA8_UNORM {
            return Err(js_error(
                "Rust WebGPU renderer rejected an unsupported texture format.",
            ));
        }

        let expected_bytes = width as usize * height as usize * layers as usize * 4;
        if data.len() != expected_bytes {
            return Err(js_error(format!(
                "Rust WebGPU renderer rejected texture data length {}, expected {}.",
                data.len(),
                expected_bytes
            )));
        }

        self.create_texture(width, height, layers, data)
    }

    fn destroy_texture(&mut self, handle: ResourceHandle) -> Result<(), JsValue> {
        self.textures
            .remove(handle)
            .map(|_| ())
            .map_err(|_| js_error("Rust WebGPU renderer rejected a stale texture handle."))
    }

    fn register_object(&mut self) -> Result<ResourceHandle, JsValue> {
        let uniform_buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("object uniforms"),
            size: uniform_byte_len(OBJECT_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        Ok(self.objects.insert(GpuObject {
            uniform_buffer,
            bind_group: None,
            albedo_texture: None,
            normal_texture: None,
            material_texture: None,
        }))
    }

    fn destroy_object(&mut self, handle: ResourceHandle) -> Result<(), JsValue> {
        let object = self
            .objects
            .remove(handle)
            .map_err(|_| js_error("Rust WebGPU renderer rejected a stale object handle."))?;
        object.uniform_buffer.destroy();
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    fn render(
        &mut self,
        frame_packet: &[f32],
        mesh_handles: &[f64],
        object_handles: &[f64],
        albedo_texture_handles: &[f64],
        normal_texture_handles: &[f64],
        material_texture_handles: &[f64],
        world_matrices: &[f32],
        material_packets: &[f32],
    ) -> Result<(), JsValue> {
        if frame_packet.len() != FRAME_PACKET_FLOATS {
            return Err(js_error(
                "Rust WebGPU renderer received an invalid frame packet.",
            ));
        }

        let item_count = mesh_handles.len();
        if object_handles.len() != item_count
            || albedo_texture_handles.len() != item_count
            || normal_texture_handles.len() != item_count
            || material_texture_handles.len() != item_count
            || world_matrices.len() != item_count * WORLD_MATRIX_FLOATS
            || material_packets.len() != item_count * MATERIAL_PACKET_FLOATS
        {
            return Err(js_error(
                "Rust WebGPU renderer received mismatched render packet arrays.",
            ));
        }
        let frame_uniforms = build_frame_uniform_values(frame_packet).map_err(js_error)?;

        let frame = match self.surface.get_current_texture() {
            Ok(frame) => frame,
            Err(wgpu::SurfaceError::Lost | wgpu::SurfaceError::Outdated) => {
                self.surface.configure(&self.device, &self.config);
                self.surface.get_current_texture().map_err(js_error)?
            }
            Err(error) => return Err(js_error(error)),
        };
        let view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let depth_view = self
            .depth_texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        self.queue.write_buffer(
            &self.camera_uniform_buffer,
            0,
            f32_as_bytes(&frame_uniforms),
        );
        let mut render_items = Vec::with_capacity(item_count);
        for index in 0..item_count {
            let mesh_handle = handle_from_js(mesh_handles[index])?;
            let object_handle = handle_from_js(object_handles[index])?;
            let albedo_texture = handle_from_js(albedo_texture_handles[index])?;
            let normal_texture = handle_from_js(normal_texture_handles[index])?;
            let material_texture = handle_from_js(material_texture_handles[index])?;
            if self.meshes.get(mesh_handle).is_none() {
                return Err(js_error(
                    "Rust WebGPU renderer received a stale mesh handle.",
                ));
            }
            let object_uniforms = build_object_uniform_values(
                &world_matrices[index * WORLD_MATRIX_FLOATS..(index + 1) * WORLD_MATRIX_FLOATS],
                &material_packets
                    [index * MATERIAL_PACKET_FLOATS..(index + 1) * MATERIAL_PACKET_FLOATS],
            )
            .map_err(js_error)?;
            self.update_object(
                object_handle,
                albedo_texture,
                normal_texture,
                material_texture,
                &object_uniforms,
            )?;
            render_items.push((mesh_handle, object_handle));
        }

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("rust webgpu frame encoder"),
            });
        {
            let color_attachments = [Some(wgpu::RenderPassColorAttachment {
                view: &view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: 0.08,
                        g: 0.09,
                        b: 0.08,
                        a: 1.0,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })];
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("rust webgpu render pass"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(1.0),
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_bind_group(0, &self.camera_bind_group, &[]);
            pass.set_pipeline(&self.sky_pipeline);
            pass.draw(0..3, 0..1);

            pass.set_pipeline(&self.pipeline);
            for (mesh_handle, object_handle) in render_items {
                let mesh = self.meshes.get(mesh_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale mesh handle.")
                })?;
                let object = self.objects.get(object_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale object handle.")
                })?;
                let bind_group = object.bind_group.as_ref().ok_or_else(|| {
                    js_error("Rust WebGPU renderer object bind group was not prepared.")
                })?;

                pass.set_bind_group(1, bind_group, &[]);
                pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                pass.draw_indexed(0..mesh.index_count, 0, 0..1);
            }
        }
        self.queue.submit(Some(encoder.finish()));
        frame.present();
        self.frame_index = self.frame_index.saturating_add(1);
        self.frame_draw_count = item_count as u32;
        Ok(())
    }

    fn update_object(
        &mut self,
        handle: ResourceHandle,
        albedo_texture: ResourceHandle,
        normal_texture: ResourceHandle,
        material_texture: ResourceHandle,
        object_uniforms: &[f32],
    ) -> Result<(), JsValue> {
        if self.textures.get(albedo_texture).is_none()
            || self.textures.get(normal_texture).is_none()
            || self.textures.get(material_texture).is_none()
        {
            return Err(js_error(
                "Rust WebGPU renderer received a stale texture handle.",
            ));
        }

        let object = self
            .objects
            .get_mut(handle)
            .ok_or_else(|| js_error("Rust WebGPU renderer received a stale object handle."))?;
        self.queue
            .write_buffer(&object.uniform_buffer, 0, f32_as_bytes(object_uniforms));
        if object.bind_group.is_none()
            || object.albedo_texture != Some(albedo_texture)
            || object.normal_texture != Some(normal_texture)
            || object.material_texture != Some(material_texture)
        {
            let albedo_view = &self
                .textures
                .get(albedo_texture)
                .ok_or_else(|| js_error("Rust WebGPU renderer received a stale albedo texture."))?
                .view;
            let normal_view = &self
                .textures
                .get(normal_texture)
                .ok_or_else(|| js_error("Rust WebGPU renderer received a stale normal texture."))?
                .view;
            let material_view = &self
                .textures
                .get(material_texture)
                .ok_or_else(|| js_error("Rust WebGPU renderer received a stale material texture."))?
                .view;
            object.bind_group = Some(self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("object bind group"),
                layout: &self.object_bind_group_layout,
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: object.uniform_buffer.as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: wgpu::BindingResource::TextureView(albedo_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 2,
                        resource: wgpu::BindingResource::TextureView(normal_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 3,
                        resource: wgpu::BindingResource::TextureView(material_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 4,
                        resource: wgpu::BindingResource::Sampler(&self.sampler),
                    },
                ],
            }));
            object.albedo_texture = Some(albedo_texture);
            object.normal_texture = Some(normal_texture);
            object.material_texture = Some(material_texture);
        }

        Ok(())
    }

    fn create_fallback_textures(&mut self) -> Result<(), JsValue> {
        self.fallback_albedo = self.create_texture(1, 1, 1, &[255, 255, 255, 255])?;
        self.fallback_normal = self.create_texture(1, 1, 1, &[128, 128, 255, 255])?;
        self.fallback_material = self.create_texture(1, 1, 1, &[0, 255, 255, 128])?;
        Ok(())
    }

    fn create_texture(
        &mut self,
        width: u32,
        height: u32,
        layers: u32,
        data: &[u8],
    ) -> Result<ResourceHandle, JsValue> {
        let texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("renderer texture array"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: layers,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let bytes_per_layer = width as usize * height as usize * 4;
        for layer in 0..layers {
            let layer_start = layer as usize * bytes_per_layer;
            let layer_end = layer_start + bytes_per_layer;
            self.queue.write_texture(
                wgpu::ImageCopyTexture {
                    texture: &texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d {
                        x: 0,
                        y: 0,
                        z: layer,
                    },
                    aspect: wgpu::TextureAspect::All,
                },
                &data[layer_start..layer_end],
                wgpu::ImageDataLayout {
                    offset: 0,
                    bytes_per_row: Some(width * 4),
                    rows_per_image: Some(height),
                },
                wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
            );
        }
        let view = texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some("renderer texture array view"),
            dimension: Some(wgpu::TextureViewDimension::D2Array),
            base_array_layer: 0,
            array_layer_count: Some(layers),
            ..Default::default()
        });

        Ok(self.textures.insert(GpuTexture { view }))
    }

    fn status(&self) -> RustWgpuRendererStatus {
        RustWgpuRendererStatus {
            version: ENGINE_WEB_VERSION,
            configured: true,
            canvas_width: self.config.width,
            canvas_height: self.config.height,
            required_texture_array_layers: REQUIRED_TEXTURE_ARRAY_LAYERS,
            max_texture_array_layers: self.max_texture_array_layers,
            mesh_count: self.meshes.len().min(u32::MAX as usize) as u32,
            texture_count: self.textures.len().min(u32::MAX as usize) as u32,
            object_count: self.objects.len().min(u32::MAX as usize) as u32,
            frame_index: self.frame_index,
            frame_draw_count: self.frame_draw_count,
        }
    }
}

fn texture_binding(binding: u32) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: true },
            view_dimension: wgpu::TextureViewDimension::D2Array,
            multisampled: false,
        },
        count: None,
    }
}

fn create_main_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
    format: wgpu::TextureFormat,
) -> wgpu::RenderPipeline {
    const ATTRIBUTES: [wgpu::VertexAttribute; 6] = [
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x3,
            offset: 0,
            shader_location: 0,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x3,
            offset: 3 * 4,
            shader_location: 1,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x3,
            offset: 6 * 4,
            shader_location: 2,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x2,
            offset: 9 * 4,
            shader_location: 3,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 11 * 4,
            shader_location: 4,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 15 * 4,
            shader_location: 5,
        },
    ];
    let vertex_buffers = [wgpu::VertexBufferLayout {
        array_stride: TERRAIN_VERTEX_FLOATS as wgpu::BufferAddress * 4,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &ATTRIBUTES,
    }];

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("seed terrain pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "vertexMain",
            compilation_options: Default::default(),
            buffers: &vertex_buffers,
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "fragmentMain",
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format,
                blend: None,
                write_mask: wgpu::ColorWrites::ALL,
            })],
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: Some(wgpu::Face::Back),
            ..Default::default()
        },
        depth_stencil: Some(wgpu::DepthStencilState {
            format: DEPTH_FORMAT,
            depth_write_enabled: true,
            depth_compare: wgpu::CompareFunction::Less,
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        multiview: None,
    })
}

fn create_sky_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
    format: wgpu::TextureFormat,
) -> wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("sky pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "skyVertexMain",
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "skyFragmentMain",
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format,
                blend: None,
                write_mask: wgpu::ColorWrites::ALL,
            })],
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: Some(wgpu::DepthStencilState {
            format: DEPTH_FORMAT,
            depth_write_enabled: false,
            depth_compare: wgpu::CompareFunction::Always,
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        multiview: None,
    })
}

fn create_depth_texture(device: &wgpu::Device, width: u32, height: u32) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some("depth texture"),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: DEPTH_FORMAT,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        view_formats: &[],
    })
}

fn uniform_byte_len(float_count: usize) -> wgpu::BufferAddress {
    (float_count * std::mem::size_of::<f32>()) as wgpu::BufferAddress
}

fn f32_as_bytes(values: &[f32]) -> &[u8] {
    unsafe {
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

fn u32_as_bytes(values: &[u32]) -> &[u8] {
    unsafe {
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

fn handle_to_js(handle: ResourceHandle) -> f64 {
    handle.raw() as f64
}

fn handle_from_js(handle: f64) -> Result<ResourceHandle, JsValue> {
    if !handle.is_finite() || handle < 0.0 || handle > u64::MAX as f64 {
        return Err(js_error(
            "Rust WebGPU renderer received an invalid resource handle.",
        ));
    }

    ResourceHandle::from_raw(handle as u64)
        .ok_or_else(|| js_error("Rust WebGPU renderer received an invalid resource handle."))
}

fn js_error(error: impl std::fmt::Display) -> JsValue {
    js_sys::Error::new(&error.to_string()).unchecked_into()
}
