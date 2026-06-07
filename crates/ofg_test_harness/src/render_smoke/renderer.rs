// Native wgpu renderer for OFG Rust image smoke tests.

use std::borrow::Cow;
use std::sync::mpsc;

use engine_core::{sky_state_at_elapsed_seconds, RenderCameraPacket, RenderSnapshot, Vec3};
use engine_web::{
    build_frame_packet_from_engine_snapshot, build_frame_uniform_values,
    build_object_uniform_values, REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_MATERIAL_PACKET,
    TERRAIN_VERTEX_FLOATS, WORLD_MATRIX_FLOATS,
};
use terrain_core::MeshData;
use wgpu::util::DeviceExt;

use super::error::{harness_error, HarnessResult};
use super::report::RendererReport;

pub const WIDTH: u32 = 960;
pub const HEIGHT: u32 = 540;

const TEXTURE_SIZE: u32 = 64;
const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth24Plus;
const COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;
const SHADER_SOURCE: &str = include_str!("../../../../src/engine/render/shaders/uber.wgsl");
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
    terrain_pipeline: wgpu::RenderPipeline,
    sky_pipeline: wgpu::RenderPipeline,
}

pub struct CameraSetup {
    pub eye: Vec3,
    pub target: Vec3,
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
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("smoke uber shader"),
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(SHADER_SOURCE)),
        });
        let terrain_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("smoke terrain pipeline layout"),
                bind_group_layouts: &[&camera_bind_group_layout, &object_bind_group_layout],
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

        Ok(Self {
            device,
            queue,
            adapter_info,
            camera_uniform_buffer,
            camera_bind_group,
            object_uniform_buffer,
            object_bind_group,
            terrain_pipeline,
            sky_pipeline,
        })
    }

    /// Renders terrain meshes to CPU-readable RGBA pixels.
    pub fn render(&self, camera: &CameraSetup, meshes: &[MeshData]) -> HarnessResult<Vec<u8>> {
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

        let gpu_meshes = meshes
            .iter()
            .map(|mesh| self.create_gpu_mesh(mesh))
            .collect::<HarnessResult<Vec<_>>>()?;
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
            let color_attachments = [Some(wgpu::RenderPassColorAttachment {
                view: &output_view,
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
            for mesh in &gpu_meshes {
                pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                pass.draw_indexed(0..mesh.index_count, 0, 0..1);
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

        read_rgba_output(
            &self.device,
            &output_buffer,
            WIDTH,
            HEIGHT,
            unpadded_bytes_per_row,
            padded_bytes_per_row,
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
}

impl CameraSetup {
    /// Converts camera and light values into renderer frame uniforms.
    fn frame_uniforms(&self) -> HarnessResult<[f32; engine_web::FRAME_UNIFORM_FLOATS]> {
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

        let frame_packet = build_frame_packet_from_engine_snapshot(&snapshot, aspect_ratio())
            .map_err(|error| harness_error(error.to_string()))?;
        build_frame_uniform_values(&frame_packet).map_err(|error| harness_error(error.to_string()))
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

/// Reads padded GPU output into tightly packed RGBA pixels.
fn read_rgba_output(
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
fn uniform_byte_len(float_count: usize) -> wgpu::BufferAddress {
    (float_count * std::mem::size_of::<f32>()) as wgpu::BufferAddress
}

/// Returns an aligned row byte count.
fn align_to(value: u32, alignment: u32) -> u32 {
    value.div_ceil(alignment) * alignment
}

/// Converts f32 values to bytes for GPU upload.
fn f32_as_bytes(values: &[f32]) -> &[u8] {
    unsafe {
        // SAFETY: `f32` has no invalid bit patterns, and the returned byte slice
        // is tied to the input slice lifetime for immediate GPU upload.
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

/// Converts u32 values to bytes for GPU upload.
fn u32_as_bytes(values: &[u32]) -> &[u8] {
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
