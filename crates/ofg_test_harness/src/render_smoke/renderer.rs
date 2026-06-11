// Native wgpu renderer for OFG Rust image smoke tests.

use std::borrow::Cow;
use std::sync::mpsc;

use engine_core::{sky_state_at_elapsed_seconds, RenderCameraPacket, RenderSnapshot, Vec3};
use engine_web::{
    build_frame_packet_from_engine_snapshot, build_frame_uniform_values,
    build_object_uniform_values, build_shadow_cascades, RenderVec3, ShadowCascadeSet,
    WaterSettings, REQUIRED_TEXTURE_ARRAY_LAYERS, SHADOW_CASCADE_COUNT, SHADOW_MAP_SIZE,
    SHADOW_UNIFORM_FLOATS, TERRAIN_MATERIAL_PACKET, TERRAIN_VERTEX_FLOATS,
    WATER_BATHYMETRY_RUNTIME, WATER_RUNTIME, WORLD_MATRIX_FLOATS,
};
use terrain_core::{
    build_water_node_packet_for_variant, MeshData, TerrainChunkCoord, TerrainNodeKey,
    TerrainVariantDescriptor, WaterNodePacket, WATER_NODE_BATHYMETRY_TEXEL_COUNT,
    WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
};
use wgpu::util::DeviceExt;

use super::error::{harness_error, HarnessResult};
use super::report::{RendererReport, WaterImageReport};
use super::shadow_debug::{ShadowDebugOutput, ShadowDebugRenderer};

pub const WIDTH: u32 = 960;
pub const HEIGHT: u32 = 540;

const TEXTURE_SIZE: u32 = 64;
const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth24Plus;
const COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;
const LINEAR_DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::R32Float;
const SHADER_SOURCE: &str = include_str!("../../../../src/engine/render/shaders/uber.wgsl");
const WATER_SHADER_SOURCE: &str = include_str!("../../../../src/engine/render/shaders/water.wgsl");
const WATER_UNIFORM_FLOATS: usize = 48;
const WATER_PATCH_INSTANCE_FLOATS: usize = 12;
const SMOKE_WATER_ATLAS_TILES_PER_AXIS: u32 = 4;
const SMOKE_WATER_ATLAS_TILE_COUNT: u32 =
    SMOKE_WATER_ATLAS_TILES_PER_AXIS * SMOKE_WATER_ATLAS_TILES_PER_AXIS;
const SMOKE_WATER_ATLAS_SIZE: u32 =
    WATER_NODE_BATHYMETRY_TEXEL_COUNT * SMOKE_WATER_ATLAS_TILES_PER_AXIS;
const IDENTITY_WORLD_MATRIX: [f32; WORLD_MATRIX_FLOATS] = [
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
];

pub struct OffscreenRenderer {
    device: wgpu::Device,
    queue: wgpu::Queue,
    adapter_info: wgpu::AdapterInfo,
    camera_uniform_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,
    object_uniform_buffer: wgpu::Buffer,
    object_bind_group: wgpu::BindGroup,
    shadow_bind_group: wgpu::BindGroup,
    _shadow_texture: wgpu::Texture,
    terrain_pipeline: wgpu::RenderPipeline,
    sky_pipeline: wgpu::RenderPipeline,
    water_copy_pipeline: wgpu::RenderPipeline,
    water_patch_pipeline: wgpu::RenderPipeline,
    water_patch_instance_buffer: wgpu::Buffer,
    water_bind_group_layout: wgpu::BindGroupLayout,
    water_uniform_buffer: wgpu::Buffer,
    water_sampler: wgpu::Sampler,
    shadow_debug_renderer: ShadowDebugRenderer,
}

pub struct CameraSetup {
    pub eye: Vec3,
    pub target: Vec3,
}

pub struct RenderedOffscreenFrame {
    pub pixels: Vec<u8>,
    pub water: WaterImageReport,
}

struct GpuMesh {
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    index_count: u32,
}

struct SmokeTerrainTextureViews {
    albedo: wgpu::TextureView,
    normal: wgpu::TextureView,
    material: wgpu::TextureView,
}

impl OffscreenRenderer {
    /// Creates a native wgpu device, pipelines, and synthetic terrain textures.
    pub async fn new() -> HarnessResult<Self> {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::PRIMARY,
            ..Default::default()
        });
        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                force_fallback_adapter: false,
                compatible_surface: None,
            })
            .await
            .ok_or_else(|| {
                harness_error("No native wgpu adapter is available for Rust image smoke rendering.")
            })?;
        let adapter_limits = adapter.limits();
        if adapter_limits.max_texture_array_layers < REQUIRED_TEXTURE_ARRAY_LAYERS {
            return Err(harness_error(format!(
                "Native wgpu adapter supports {} texture array layers; OFG terrain requires at least {}.",
                adapter_limits.max_texture_array_layers, REQUIRED_TEXTURE_ARRAY_LAYERS
            )));
        }

        let mut limits = wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter_limits);
        limits.max_texture_array_layers = limits
            .max_texture_array_layers
            .max(REQUIRED_TEXTURE_ARRAY_LAYERS);
        let adapter_info = adapter.get_info();
        let (device, queue) = adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    label: Some("ofg native smoke device"),
                    required_features: wgpu::Features::empty(),
                    required_limits: limits,
                },
                None,
            )
            .await?;

        let camera_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke camera uniforms"),
            size: uniform_byte_len(engine_web::FRAME_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let camera_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("smoke camera bind group layout"),
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
            label: Some("smoke camera bind group"),
            layout: &camera_bind_group_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: camera_uniform_buffer.as_entire_binding(),
            }],
        });
        let object_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke terrain object uniforms"),
            size: uniform_byte_len(engine_web::OBJECT_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("smoke terrain sampler"),
            address_mode_u: wgpu::AddressMode::Repeat,
            address_mode_v: wgpu::AddressMode::Repeat,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let object_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("smoke object bind group layout"),
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
        let textures = create_smoke_terrain_textures(&device, &queue);
        let object_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("smoke terrain object bind group"),
            layout: &object_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: object_uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&textures.albedo),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(&textures.normal),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: wgpu::BindingResource::TextureView(&textures.material),
                },
                wgpu::BindGroupEntry {
                    binding: 4,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });
        let shadow_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke disabled shadow uniforms"),
            size: uniform_byte_len(SHADOW_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let shadow_texture = create_disabled_shadow_texture(&device);
        let shadow_texture_view = shadow_texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some("smoke disabled shadow texture view"),
            dimension: Some(wgpu::TextureViewDimension::D2Array),
            base_array_layer: 0,
            array_layer_count: Some(SHADOW_CASCADE_COUNT as u32),
            ..Default::default()
        });
        let shadow_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("smoke disabled shadow sampler"),
            compare: Some(wgpu::CompareFunction::LessEqual),
            ..Default::default()
        });
        let shadow_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("smoke disabled shadow bind group layout"),
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
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Texture {
                            sample_type: wgpu::TextureSampleType::Depth,
                            view_dimension: wgpu::TextureViewDimension::D2Array,
                            multisampled: false,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 2,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Comparison),
                        count: None,
                    },
                ],
            });
        let shadow_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("smoke disabled shadow bind group"),
            layout: &shadow_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: shadow_uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&shadow_texture_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&shadow_sampler),
                },
            ],
        });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("smoke uber shader"),
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(SHADER_SOURCE)),
        });
        let water_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("smoke water shader"),
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(WATER_SHADER_SOURCE)),
        });
        let terrain_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("smoke terrain pipeline layout"),
                bind_group_layouts: &[
                    &camera_bind_group_layout,
                    &object_bind_group_layout,
                    &shadow_bind_group_layout,
                ],
                push_constant_ranges: &[],
            });
        let sky_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("smoke sky pipeline layout"),
            bind_group_layouts: &[&camera_bind_group_layout],
            push_constant_ranges: &[],
        });
        let terrain_pipeline =
            create_terrain_pipeline(&device, &terrain_pipeline_layout, &shader, COLOR_FORMAT);
        let sky_pipeline =
            create_sky_pipeline(&device, &sky_pipeline_layout, &shader, COLOR_FORMAT);
        let water_bind_group_layout = create_water_bind_group_layout(&device);
        let water_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("smoke water pipeline layout"),
                bind_group_layouts: &[&camera_bind_group_layout, &water_bind_group_layout],
                push_constant_ranges: &[],
            });
        let water_copy_pipeline =
            create_water_copy_pipeline(&device, &water_pipeline_layout, &water_shader);
        let water_patch_pipeline =
            create_water_patch_pipeline(&device, &water_pipeline_layout, &water_shader);
        let water_patch_instance_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke water patch instances"),
            size: (SMOKE_WATER_ATLAS_TILE_COUNT as u64 * WATER_PATCH_INSTANCE_FLOATS as u64 * 4),
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let water_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke water uniforms"),
            size: uniform_byte_len(WATER_UNIFORM_FLOATS),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let water_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("smoke water sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let shadow_debug_renderer = ShadowDebugRenderer::new(
            &device,
            &camera_bind_group_layout,
            &object_bind_group_layout,
            &shader,
        );

        Ok(Self {
            device,
            queue,
            adapter_info,
            camera_uniform_buffer,
            camera_bind_group,
            object_uniform_buffer,
            object_bind_group,
            shadow_bind_group,
            _shadow_texture: shadow_texture,
            terrain_pipeline,
            sky_pipeline,
            water_copy_pipeline,
            water_patch_pipeline,
            water_patch_instance_buffer,
            water_bind_group_layout,
            water_uniform_buffer,
            water_sampler,
            shadow_debug_renderer,
        })
    }

    /// Renders terrain meshes to CPU-readable RGBA pixels.
    pub fn render(
        &self,
        camera: &CameraSetup,
        meshes: &[MeshData],
        terrain_seed: u32,
        terrain_variant: TerrainVariantDescriptor,
    ) -> HarnessResult<RenderedOffscreenFrame> {
        self.write_common_uniforms(camera)?;

        let gpu_meshes = meshes
            .iter()
            .map(|mesh| self.create_gpu_mesh(mesh))
            .collect::<HarnessResult<Vec<_>>>()?;
        let opaque_color = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("smoke opaque scene color"),
            size: wgpu::Extent3d {
                width: WIDTH,
                height: HEIGHT,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: COLOR_FORMAT,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let opaque_color_view = opaque_color.create_view(&wgpu::TextureViewDescriptor::default());
        let output = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("smoke output texture"),
            size: wgpu::Extent3d {
                width: WIDTH,
                height: HEIGHT,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: COLOR_FORMAT,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let output_view = output.create_view(&wgpu::TextureViewDescriptor::default());
        let linear_depth = create_linear_depth_texture(&self.device, WIDTH, HEIGHT);
        let linear_depth_view = linear_depth.create_view(&wgpu::TextureViewDescriptor::default());
        let final_linear_depth = create_linear_depth_texture(&self.device, WIDTH, HEIGHT);
        let final_linear_depth_view =
            final_linear_depth.create_view(&wgpu::TextureViewDescriptor::default());
        let depth = create_depth_texture(&self.device, WIDTH, HEIGHT);
        let depth_view = depth.create_view(&wgpu::TextureViewDescriptor::default());
        let unpadded_bytes_per_row = WIDTH * 4;
        let padded_bytes_per_row =
            align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
        let output_buffer_size = padded_bytes_per_row as u64 * HEIGHT as u64;
        let output_buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("smoke output readback buffer"),
            size: output_buffer_size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("smoke render encoder"),
            });
        {
            let color_attachments = [
                Some(wgpu::RenderPassColorAttachment {
                    view: &opaque_color_view,
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
                }),
                Some(wgpu::RenderPassColorAttachment {
                    view: &linear_depth_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                }),
            ];
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("smoke render pass"),
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
            pass.set_pipeline(&self.terrain_pipeline);
            pass.set_bind_group(1, &self.object_bind_group, &[]);
            pass.set_bind_group(2, &self.shadow_bind_group, &[]);
            for mesh in &gpu_meshes {
                pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                pass.draw_indexed(0..mesh.index_count, 0, 0..1);
            }
        }
        let mut water_settings = WaterSettings::default();
        water_settings.reflection_enabled = false;
        let water_packets =
            build_smoke_water_packets(terrain_seed, terrain_variant, camera, water_settings)?;
        let bathymetry_texture =
            create_bathymetry_texture(&self.device, &self.queue, &water_packets);
        let bathymetry_view =
            bathymetry_texture.create_view(&wgpu::TextureViewDescriptor::default());
        let water_instances = smoke_water_instance_values(&water_packets);
        if !water_instances.is_empty() {
            self.queue.write_buffer(
                &self.water_patch_instance_buffer,
                0,
                f32_as_bytes(&water_instances),
            );
        }
        self.queue.write_buffer(
            &self.water_uniform_buffer,
            0,
            f32_as_bytes(&smoke_water_uniform_values(
                water_settings,
                water_packets.len() as u32,
            )),
        );
        let water_bind_group = self.create_water_bind_group(
            &opaque_color_view,
            &linear_depth_view,
            &bathymetry_view,
            &opaque_color_view,
        );
        {
            let color_attachments = [
                Some(wgpu::RenderPassColorAttachment {
                    view: &output_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                }),
                Some(wgpu::RenderPassColorAttachment {
                    view: &final_linear_depth_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                }),
            ];
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("smoke water composite pass"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&self.water_copy_pipeline);
            pass.set_bind_group(0, &self.camera_bind_group, &[]);
            pass.set_bind_group(1, &water_bind_group, &[]);
            pass.draw(0..3, 0..1);
            if water_settings.enabled && !water_instances.is_empty() {
                let instance_count = (water_instances.len() / WATER_PATCH_INSTANCE_FLOATS) as u32;
                pass.set_pipeline(&self.water_patch_pipeline);
                pass.set_bind_group(0, &self.camera_bind_group, &[]);
                pass.set_bind_group(1, &water_bind_group, &[]);
                pass.set_vertex_buffer(0, self.water_patch_instance_buffer.slice(..));
                pass.draw(0..6, 0..instance_count);
            }
        }
        encoder.copy_texture_to_buffer(
            wgpu::ImageCopyTexture {
                texture: &output,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::ImageCopyBuffer {
                buffer: &output_buffer,
                layout: wgpu::ImageDataLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row),
                    rows_per_image: Some(HEIGHT),
                },
            },
            wgpu::Extent3d {
                width: WIDTH,
                height: HEIGHT,
                depth_or_array_layers: 1,
            },
        );
        self.queue.submit(Some(encoder.finish()));

        let pixels = read_rgba_output(
            &self.device,
            &output_buffer,
            WIDTH,
            HEIGHT,
            unpadded_bytes_per_row,
            padded_bytes_per_row,
        )?;

        Ok(RenderedOffscreenFrame {
            pixels,
            water: WaterImageReport {
                runtime: WATER_RUNTIME,
                enabled: water_settings.enabled,
                reflection_enabled: water_settings.reflection_enabled,
                sea_level_meters: water_settings.sea_level_meters,
                bathymetry_runtime: WATER_BATHYMETRY_RUNTIME,
                bathymetry_grid_size: WATER_NODE_BATHYMETRY_TEXEL_COUNT,
                bathymetry_world_span_meters: water_packets
                    .first()
                    .map(|packet| packet.world_span_x.max(packet.world_span_z))
                    .unwrap_or(0.0),
                bathymetry_center_x: water_packets
                    .first()
                    .map(|packet| packet.origin_x + packet.world_span_x * 0.5)
                    .unwrap_or(0.0),
                bathymetry_center_z: water_packets
                    .first()
                    .map(|packet| packet.origin_z + packet.world_span_z * 0.5)
                    .unwrap_or(0.0),
            },
        })
    }

    /// Renders and visualizes CSM depth layers for the terrain smoke scene.
    pub fn render_shadow_debug(
        &self,
        camera: &CameraSetup,
        meshes: &[MeshData],
    ) -> HarnessResult<ShadowDebugOutput> {
        self.write_common_uniforms(camera)?;
        self.shadow_debug_renderer.render(
            &self.device,
            &self.queue,
            &self.camera_bind_group,
            &self.object_bind_group,
            camera,
            meshes,
        )
    }

    /// Builds the renderer section of the report.
    pub fn report(&self) -> RendererReport {
        RendererReport {
            backend: format!("{:?}", self.adapter_info.backend),
            device_type: format!("{:?}", self.adapter_info.device_type),
            name: self.adapter_info.name.clone(),
            width: WIDTH,
            height: HEIGHT,
        }
    }

    /// Creates GPU vertex and index buffers for one terrain mesh.
    fn create_gpu_mesh(&self, mesh: &MeshData) -> HarnessResult<GpuMesh> {
        if mesh.vertices.is_empty()
            || mesh.indices.is_empty()
            || mesh.vertices.len() % TERRAIN_VERTEX_FLOATS as usize != 0
            || mesh.indices.len() % 3 != 0
        {
            return Err(harness_error(
                "Rust smoke received an invalid terrain mesh.",
            ));
        }

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("smoke terrain vertices"),
                contents: f32_as_bytes(&mesh.vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
        let index_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("smoke terrain indices"),
                contents: u32_as_bytes(&mesh.indices),
                usage: wgpu::BufferUsages::INDEX,
            });

        Ok(GpuMesh {
            vertex_buffer,
            index_buffer,
            index_count: mesh.indices.len() as u32,
        })
    }

    /// Writes camera and terrain object uniforms shared by color and debug passes.
    fn write_common_uniforms(&self, camera: &CameraSetup) -> HarnessResult<()> {
        let frame_uniforms = camera.frame_uniforms()?;
        let object_uniforms =
            build_object_uniform_values(&IDENTITY_WORLD_MATRIX, &TERRAIN_MATERIAL_PACKET)
                .map_err(|error| harness_error(error.to_string()))?;
        self.queue.write_buffer(
            &self.camera_uniform_buffer,
            0,
            f32_as_bytes(&frame_uniforms),
        );
        self.queue.write_buffer(
            &self.object_uniform_buffer,
            0,
            f32_as_bytes(&object_uniforms),
        );
        Ok(())
    }

    fn create_water_bind_group(
        &self,
        opaque_color: &wgpu::TextureView,
        opaque_linear_depth: &wgpu::TextureView,
        bathymetry: &wgpu::TextureView,
        reflection_color: &wgpu::TextureView,
    ) -> wgpu::BindGroup {
        self.device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("smoke water bind group"),
            layout: &self.water_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(opaque_color),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(opaque_linear_depth),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(bathymetry),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: wgpu::BindingResource::TextureView(reflection_color),
                },
                wgpu::BindGroupEntry {
                    binding: 4,
                    resource: wgpu::BindingResource::Sampler(&self.water_sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 5,
                    resource: self.water_uniform_buffer.as_entire_binding(),
                },
            ],
        })
    }
}

impl CameraSetup {
    /// Converts camera and light values into renderer frame uniforms.
    fn frame_uniforms(&self) -> HarnessResult<[f32; engine_web::FRAME_UNIFORM_FLOATS]> {
        let snapshot = self.render_snapshot_f32s();

        let frame_packet = build_frame_packet_from_engine_snapshot(&snapshot, aspect_ratio())
            .map_err(|error| harness_error(error.to_string()))?;
        build_frame_uniform_values(&frame_packet).map_err(|error| harness_error(error.to_string()))
    }

    /// Builds shadow cascade matrices for this deterministic smoke camera.
    pub(super) fn shadow_cascades(&self) -> HarnessResult<ShadowCascadeSet> {
        let snapshot = self.render_snapshot_f32s();
        build_shadow_cascades(
            RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]),
            RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]),
            snapshot[8],
            aspect_ratio(),
            snapshot[9],
            snapshot[10],
            RenderVec3::new(snapshot[11], snapshot[12], snapshot[13]),
        )
        .ok_or_else(|| harness_error("Rust smoke could not build shadow cascades."))
    }

    /// Writes a stable engine render snapshot for color and shadow paths.
    pub(super) fn render_snapshot_f32s(&self) -> [f32; engine_core::RENDER_SNAPSHOT_FLOAT_COUNT] {
        let mut snapshot = [0.0; engine_core::RENDER_SNAPSHOT_FLOAT_COUNT];
        let sky_state = sky_state_at_elapsed_seconds(0.0);
        RenderSnapshot {
            camera: RenderCameraPacket {
                eye: self.eye,
                target: self.target,
                yaw: 0.0,
                pitch: 0.0,
                fov_y_radians: 70.0_f32.to_radians(),
                near_plane: 0.05,
                far_plane: 500.0,
            },
            main_light: sky_state.main_light,
            sky: sky_state.sky,
        }
        .write_f32s(&mut snapshot);
        snapshot
    }
}

/// Creates synthetic terrain texture arrays for Rust-only shader coverage.
fn create_smoke_terrain_textures(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
) -> SmokeTerrainTextureViews {
    let albedo = create_texture_array_view(device, queue, "smoke terrain albedo", |layer, x, y| {
        let base = terrain_layer_color(layer);
        let checker = if ((x / 8) + (y / 8) + layer) % 2 == 0 {
            1.0
        } else {
            0.78
        };
        [
            (base[0] as f32 * checker).round() as u8,
            (base[1] as f32 * checker).round() as u8,
            (base[2] as f32 * checker).round() as u8,
            255,
        ]
    });
    let normal = create_texture_array_view(device, queue, "smoke terrain normal", |_, _, _| {
        [128, 128, 255, 255]
    });
    let material =
        create_texture_array_view(device, queue, "smoke terrain material", |layer, x, y| {
            let roughness = 80 + ((layer * 11 + x / 8 + y / 8) % 96) as u8;
            [roughness, 255, 255, 255]
        });

    SmokeTerrainTextureViews {
        albedo,
        normal,
        material,
    }
}

/// Creates one 16-layer RGBA texture array and returns a D2-array view.
fn create_texture_array_view(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    label: &'static str,
    pixel: impl Fn(u32, u32, u32) -> [u8; 4],
) -> wgpu::TextureView {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some(label),
        size: wgpu::Extent3d {
            width: TEXTURE_SIZE,
            height: TEXTURE_SIZE,
            depth_or_array_layers: REQUIRED_TEXTURE_ARRAY_LAYERS,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8Unorm,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    let mut data = Vec::with_capacity(
        TEXTURE_SIZE as usize * TEXTURE_SIZE as usize * REQUIRED_TEXTURE_ARRAY_LAYERS as usize * 4,
    );
    for layer in 0..REQUIRED_TEXTURE_ARRAY_LAYERS {
        for y in 0..TEXTURE_SIZE {
            for x in 0..TEXTURE_SIZE {
                data.extend_from_slice(&pixel(layer, x, y));
            }
        }
    }
    queue.write_texture(
        wgpu::ImageCopyTexture {
            texture: &texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        &data,
        wgpu::ImageDataLayout {
            offset: 0,
            bytes_per_row: Some(TEXTURE_SIZE * 4),
            rows_per_image: Some(TEXTURE_SIZE),
        },
        wgpu::Extent3d {
            width: TEXTURE_SIZE,
            height: TEXTURE_SIZE,
            depth_or_array_layers: REQUIRED_TEXTURE_ARRAY_LAYERS,
        },
    );

    texture.create_view(&wgpu::TextureViewDescriptor {
        label: Some(label),
        dimension: Some(wgpu::TextureViewDimension::D2Array),
        base_array_layer: 0,
        array_layer_count: Some(REQUIRED_TEXTURE_ARRAY_LAYERS),
        ..Default::default()
    })
}

/// Returns a stable synthetic color for a terrain material layer.
fn terrain_layer_color(layer: u32) -> [u8; 3] {
    const COLORS: [[u8; 3]; 16] = [
        [92, 137, 82],
        [128, 148, 92],
        [112, 112, 98],
        [156, 148, 124],
        [83, 118, 73],
        [126, 101, 78],
        [168, 169, 150],
        [78, 122, 127],
        [118, 135, 64],
        [99, 92, 83],
        [141, 158, 107],
        [91, 111, 139],
        [135, 130, 104],
        [72, 105, 91],
        [162, 139, 93],
        [109, 121, 114],
    ];

    COLORS[layer as usize % COLORS.len()]
}

/// Creates the terrain render pipeline matching the browser terrain layout.
fn create_terrain_pipeline(
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
        label: Some("smoke terrain pipeline"),
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
            targets: &scene_render_targets(format),
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

/// Creates the procedural sky pipeline shared by smoke images.
fn create_sky_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
    format: wgpu::TextureFormat,
) -> wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("smoke sky pipeline"),
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
            targets: &scene_render_targets(format),
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

/// Returns the scene color plus linear-depth target layout used by uber.wgsl.
fn scene_render_targets(format: wgpu::TextureFormat) -> [Option<wgpu::ColorTargetState>; 2] {
    [
        Some(wgpu::ColorTargetState {
            format,
            blend: None,
            write_mask: wgpu::ColorWrites::ALL,
        }),
        Some(wgpu::ColorTargetState {
            format: LINEAR_DEPTH_FORMAT,
            blend: None,
            write_mask: wgpu::ColorWrites::RED,
        }),
    ]
}

fn create_water_bind_group_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("smoke water bind group layout"),
        entries: &[
            water_texture_binding(0, wgpu::TextureSampleType::Float { filterable: true }),
            water_texture_binding(1, wgpu::TextureSampleType::Float { filterable: false }),
            water_texture_binding(2, wgpu::TextureSampleType::Float { filterable: false }),
            water_texture_binding(3, wgpu::TextureSampleType::Float { filterable: true }),
            wgpu::BindGroupLayoutEntry {
                binding: 4,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 5,
                visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
        ],
    })
}

fn create_water_copy_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("smoke water copy pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "waterCopyVertexMain",
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "waterCopyFragmentMain",
            compilation_options: Default::default(),
            targets: &scene_render_targets(COLOR_FORMAT),
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: None,
        multisample: Default::default(),
        multiview: None,
    })
}

fn create_water_patch_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    const ATTRIBUTES: [wgpu::VertexAttribute; 3] = [
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 0,
            shader_location: 0,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 16,
            shader_location: 1,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 32,
            shader_location: 2,
        },
    ];
    let instance_layout = [wgpu::VertexBufferLayout {
        array_stride: (WATER_PATCH_INSTANCE_FLOATS * 4) as wgpu::BufferAddress,
        step_mode: wgpu::VertexStepMode::Instance,
        attributes: &ATTRIBUTES,
    }];

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("smoke water patch pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "waterPatchVertexMain",
            compilation_options: Default::default(),
            buffers: &instance_layout,
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "waterPatchFragmentMain",
            compilation_options: Default::default(),
            targets: &scene_render_targets(COLOR_FORMAT),
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: None,
        multisample: Default::default(),
        multiview: None,
    })
}

/// Returns the texture binding layout entry used by terrain material arrays.
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

fn water_texture_binding(
    binding: u32,
    sample_type: wgpu::TextureSampleType,
) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding,
        visibility: wgpu::ShaderStages::FRAGMENT,
        ty: wgpu::BindingType::Texture {
            sample_type,
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
        },
        count: None,
    }
}

/// Creates the linear-depth color attachment required by the shared scene shader.
fn create_linear_depth_texture(device: &wgpu::Device, width: u32, height: u32) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some("smoke linear depth texture"),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: LINEAR_DEPTH_FORMAT,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
        view_formats: &[],
    })
}

fn build_smoke_water_packets(
    terrain_seed: u32,
    terrain_variant: TerrainVariantDescriptor,
    camera: &CameraSetup,
    settings: WaterSettings,
) -> HarnessResult<Vec<WaterNodePacket>> {
    let node_size = WATER_NODE_BATHYMETRY_TEXEL_COUNT as f64;
    let center_x = (f64::from(camera.target.x) / node_size).floor() as i32;
    let center_z = (f64::from(camera.target.z) / node_size).floor() as i32;
    let sea_y = (f64::from(settings.sea_level_meters) / node_size).floor() as i32;
    let mut packets = Vec::new();

    for z_offset in -1..=1 {
        for x_offset in -1..=1 {
            let key = TerrainNodeKey {
                lod: 0,
                coord: TerrainChunkCoord {
                    x: center_x + x_offset,
                    y: sea_y,
                    z: center_z + z_offset,
                },
            };
            let packet = build_water_node_packet_for_variant(
                terrain_seed,
                terrain_variant,
                key,
                1.0,
                f64::from(settings.sea_level_meters),
                WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
            )
            .map_err(|error| {
                harness_error(format!(
                    "Rust smoke could not build water node packet: {error}"
                ))
            })?;
            if let Some(packet) = packet {
                packets.push(packet);
            }
        }
    }

    Ok(packets)
}

fn smoke_water_instance_values(packets: &[WaterNodePacket]) -> Vec<f32> {
    let mut values = Vec::with_capacity(packets.len() * WATER_PATCH_INSTANCE_FLOATS);
    for (index, packet) in packets.iter().enumerate() {
        let tile_index = index as u32;
        let tile_x =
            (tile_index % SMOKE_WATER_ATLAS_TILES_PER_AXIS) * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        let tile_y =
            (tile_index / SMOKE_WATER_ATLAS_TILES_PER_AXIS) * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        values.extend_from_slice(&[
            packet.origin_x,
            packet.origin_z,
            packet.world_span_x,
            packet.world_span_z,
            tile_x as f32,
            tile_y as f32,
            packet.texel_count as f32,
            packet.max_depth_meters,
            packet.sea_level_meters,
            0.0,
            0.0,
            0.0,
        ]);
    }
    values
}

fn create_bathymetry_texture(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    packets: &[WaterNodePacket],
) -> wgpu::Texture {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("smoke water bathymetry atlas"),
        size: wgpu::Extent3d {
            width: SMOKE_WATER_ATLAS_SIZE,
            height: SMOKE_WATER_ATLAS_SIZE,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::R32Float,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    for (index, packet) in packets.iter().enumerate() {
        let tile_index = index as u32;
        let tile_x =
            (tile_index % SMOKE_WATER_ATLAS_TILES_PER_AXIS) * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        let tile_y =
            (tile_index / SMOKE_WATER_ATLAS_TILES_PER_AXIS) * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d {
                    x: tile_x,
                    y: tile_y,
                    z: 0,
                },
                aspect: wgpu::TextureAspect::All,
            },
            f32_as_bytes(&packet.depths_meters),
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(packet.texel_count * 4),
                rows_per_image: Some(packet.texel_count),
            },
            wgpu::Extent3d {
                width: packet.texel_count,
                height: packet.texel_count,
                depth_or_array_layers: 1,
            },
        );
    }
    texture
}

fn smoke_water_uniform_values(
    settings: WaterSettings,
    patch_count: u32,
) -> [f32; WATER_UNIFORM_FLOATS] {
    let mut values = [
        if settings.enabled { 1.0 } else { 0.0 },
        if settings.reflection_enabled {
            1.0
        } else {
            0.0
        },
        settings.sea_level_meters,
        0.0,
        settings.shallow_depth_meters,
        settings.deep_depth_meters,
        settings.open_water_path_meters,
        settings.debug_view.shader_code(),
        settings.absorption_rgb[0],
        settings.absorption_rgb[1],
        settings.absorption_rgb[2],
        0.0,
        settings.shallow_color[0],
        settings.shallow_color[1],
        settings.shallow_color[2],
        0.0,
        settings.deep_color[0],
        settings.deep_color[1],
        settings.deep_color[2],
        0.0,
        settings.wave_scale,
        settings.wave_strength,
        WIDTH as f32,
        HEIGHT as f32,
        SMOKE_WATER_ATLAS_SIZE as f32,
        SMOKE_WATER_ATLAS_SIZE as f32,
        1.0 / SMOKE_WATER_ATLAS_SIZE as f32,
        patch_count as f32,
        WIDTH as f32,
        HEIGHT as f32,
        1.0 / WIDTH as f32,
        1.0 / HEIGHT as f32,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
    ];
    values[32..48].copy_from_slice(&IDENTITY_WORLD_MATRIX);
    values
}

/// Creates a depth texture for offscreen rendering.
fn create_depth_texture(device: &wgpu::Device, width: u32, height: u32) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some("smoke depth texture"),
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

/// Creates an inert shadow texture array for normal color smoke rendering.
fn create_disabled_shadow_texture(device: &wgpu::Device) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some("smoke disabled shadow texture"),
        size: wgpu::Extent3d {
            width: SHADOW_MAP_SIZE,
            height: SHADOW_MAP_SIZE,
            depth_or_array_layers: SHADOW_CASCADE_COUNT as u32,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Depth32Float,
        usage: wgpu::TextureUsages::TEXTURE_BINDING,
        view_formats: &[],
    })
}

/// Reads padded GPU output into tightly packed RGBA pixels.
pub(super) fn read_rgba_output(
    device: &wgpu::Device,
    output_buffer: &wgpu::Buffer,
    width: u32,
    height: u32,
    unpadded_bytes_per_row: u32,
    padded_bytes_per_row: u32,
) -> HarnessResult<Vec<u8>> {
    let slice = output_buffer.slice(..);
    let (sender, receiver) = mpsc::channel();
    slice.map_async(wgpu::MapMode::Read, move |result| {
        let _ = sender.send(result);
    });
    device.poll(wgpu::Maintain::Wait);
    receiver
        .recv()
        .map_err(|error| harness_error(format!("Could not receive GPU map result: {error}")))?
        .map_err(|error| harness_error(format!("Could not map GPU output buffer: {error:?}")))?;

    let mapped = slice.get_mapped_range();
    let mut pixels = vec![0; width as usize * height as usize * 4];
    for row in 0..height as usize {
        let source_start = row * padded_bytes_per_row as usize;
        let source_end = source_start + unpadded_bytes_per_row as usize;
        let target_start = row * unpadded_bytes_per_row as usize;
        let target_end = target_start + unpadded_bytes_per_row as usize;
        pixels[target_start..target_end].copy_from_slice(&mapped[source_start..source_end]);
    }
    drop(mapped);
    output_buffer.unmap();
    Ok(pixels)
}

/// Returns the renderer aspect ratio.
fn aspect_ratio() -> f32 {
    WIDTH as f32 / HEIGHT as f32
}

/// Returns the byte length for a f32 uniform array.
pub(super) fn uniform_byte_len(float_count: usize) -> wgpu::BufferAddress {
    (float_count * std::mem::size_of::<f32>()) as wgpu::BufferAddress
}

/// Returns an aligned row byte count.
pub(super) fn align_to(value: u32, alignment: u32) -> u32 {
    value.div_ceil(alignment) * alignment
}

/// Converts f32 values to bytes for GPU upload.
pub(super) fn f32_as_bytes(values: &[f32]) -> &[u8] {
    unsafe {
        // SAFETY: `f32` has no invalid bit patterns, and the returned byte slice
        // is tied to the input slice lifetime for immediate GPU upload.
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

/// Converts u32 values to bytes for GPU upload.
pub(super) fn u32_as_bytes(values: &[u32]) -> &[u8] {
    unsafe {
        // SAFETY: `u32` has no invalid bit patterns, and the returned byte slice
        // is tied to the input slice lifetime for immediate GPU upload.
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renderer_constants_match_smoke_report_dimensions() {
        assert_eq!(WIDTH, 960);
        assert_eq!(HEIGHT, 540);
        assert_eq!(aspect_ratio(), 16.0 / 9.0);
        assert_eq!(uniform_byte_len(4), 16);
        assert_eq!(
            uniform_byte_len(engine_web::FRAME_UNIFORM_FLOATS),
            (engine_web::FRAME_UNIFORM_FLOATS * 4) as wgpu::BufferAddress
        );
    }

    #[test]
    fn row_alignment_rounds_up_to_webgpu_copy_boundaries() {
        assert_eq!(align_to(0, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT), 0);
        assert_eq!(align_to(3_840, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT), 3_840);
        assert_eq!(align_to(3_841, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT), 4_096);
    }

    #[test]
    fn terrain_layer_colors_are_stable_and_wrap_after_sixteen_layers() {
        assert_eq!(terrain_layer_color(0), [92, 137, 82]);
        assert_eq!(terrain_layer_color(3), [156, 148, 124]);
        assert_eq!(terrain_layer_color(16), terrain_layer_color(0));
        assert_ne!(terrain_layer_color(1), terrain_layer_color(2));
    }

    #[test]
    fn texture_bindings_use_filterable_two_dimensional_arrays() {
        let entry = texture_binding(7);

        assert_eq!(entry.binding, 7);
        assert_eq!(entry.visibility, wgpu::ShaderStages::FRAGMENT);
        assert!(entry.count.is_none());
        match entry.ty {
            wgpu::BindingType::Texture {
                sample_type,
                view_dimension,
                multisampled,
            } => {
                assert_eq!(
                    sample_type,
                    wgpu::TextureSampleType::Float { filterable: true }
                );
                assert_eq!(view_dimension, wgpu::TextureViewDimension::D2Array);
                assert!(!multisampled);
            }
            other => panic!("expected texture binding, got {other:?}"),
        }
    }

    #[test]
    fn upload_byte_views_preserve_input_lengths_and_native_bytes() {
        let floats = [1.25_f32, -2.5];
        let float_bytes = f32_as_bytes(&floats);
        assert_eq!(float_bytes.len(), 8);
        assert_eq!(&float_bytes[0..4], &1.25_f32.to_ne_bytes());
        assert_eq!(&float_bytes[4..8], &(-2.5_f32).to_ne_bytes());

        let integers = [1_u32, u32::MAX];
        let integer_bytes = u32_as_bytes(&integers);
        assert_eq!(integer_bytes.len(), 8);
        assert_eq!(&integer_bytes[0..4], &1_u32.to_ne_bytes());
        assert_eq!(&integer_bytes[4..8], &u32::MAX.to_ne_bytes());
    }
}
