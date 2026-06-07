// WebGPU resource helpers for cascaded shadow maps.
// Browser draw submission stays in `wgpu_renderer.rs`, while shadow-specific
// texture, sampler, and bind-group setup lives here to keep that file from
// growing another large private helper section.

use crate::config::{SHADOW_CASCADE_COUNT, SHADOW_MAP_SIZE};
use crate::render_uniforms::SHADOW_UNIFORM_FLOATS;
use wgpu::util::DeviceExt;

pub const SHADOW_DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;

pub struct ShadowResources {
    pub texture: wgpu::Texture,
    pub layer_views: Vec<wgpu::TextureView>,
    pub array_view: wgpu::TextureView,
    pub uniform_buffer: wgpu::Buffer,
    pub cascade_uniform_buffers: Vec<wgpu::Buffer>,
    pub bind_group_layout: wgpu::BindGroupLayout,
    pub depth_bind_group_layout: wgpu::BindGroupLayout,
    pub bind_group: wgpu::BindGroup,
    pub cascade_bind_groups: Vec<wgpu::BindGroup>,
    pub sampler: wgpu::Sampler,
}

pub struct ShadowPipelines {
    pub terrain: wgpu::RenderPipeline,
    pub model: wgpu::RenderPipeline,
}

/// Creates the persistent WebGPU resources shared by all CSM passes.
pub fn create_shadow_resources(device: &wgpu::Device) -> ShadowResources {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("shadow map texture array"),
        size: wgpu::Extent3d {
            width: SHADOW_MAP_SIZE,
            height: SHADOW_MAP_SIZE,
            depth_or_array_layers: SHADOW_CASCADE_COUNT as u32,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: SHADOW_DEPTH_FORMAT,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
        view_formats: &[],
    });
    let layer_views = (0..SHADOW_CASCADE_COUNT as u32)
        .map(|layer| {
            texture.create_view(&wgpu::TextureViewDescriptor {
                label: Some("shadow map cascade layer view"),
                dimension: Some(wgpu::TextureViewDimension::D2),
                base_array_layer: layer,
                array_layer_count: Some(1),
                ..Default::default()
            })
        })
        .collect();
    let array_view = texture.create_view(&wgpu::TextureViewDescriptor {
        label: Some("shadow map texture array view"),
        dimension: Some(wgpu::TextureViewDimension::D2Array),
        base_array_layer: 0,
        array_layer_count: Some(SHADOW_CASCADE_COUNT as u32),
        ..Default::default()
    });

    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        label: Some("shadow comparison sampler"),
        address_mode_u: wgpu::AddressMode::ClampToEdge,
        address_mode_v: wgpu::AddressMode::ClampToEdge,
        address_mode_w: wgpu::AddressMode::ClampToEdge,
        mag_filter: wgpu::FilterMode::Linear,
        min_filter: wgpu::FilterMode::Linear,
        compare: Some(wgpu::CompareFunction::LessEqual),
        ..Default::default()
    });
    let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("shadow bind group layout"),
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
    let depth_bind_group_layout =
        device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("shadow depth bind group layout"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });
    let uniform_buffer = create_shadow_uniform_buffer(device, "shadow uniform buffer");
    let bind_group = create_shadow_bind_group(
        device,
        "shadow bind group",
        &bind_group_layout,
        &uniform_buffer,
        &array_view,
        &sampler,
    );
    let cascade_uniform_buffers = (0..SHADOW_CASCADE_COUNT)
        .map(|_| create_shadow_uniform_buffer(device, "shadow cascade uniform buffer"))
        .collect::<Vec<_>>();
    let cascade_bind_groups = cascade_uniform_buffers
        .iter()
        .map(|uniform_buffer| {
            create_shadow_depth_bind_group(
                device,
                "shadow cascade depth bind group",
                &depth_bind_group_layout,
                uniform_buffer,
            )
        })
        .collect();

    ShadowResources {
        texture,
        layer_views,
        array_view,
        uniform_buffer,
        cascade_uniform_buffers,
        bind_group_layout,
        depth_bind_group_layout,
        bind_group,
        cascade_bind_groups,
        sampler,
    }
}

/// Creates terrain and model depth-only pipelines for shadow map rendering.
pub fn create_shadow_pipelines(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
) -> ShadowPipelines {
    ShadowPipelines {
        terrain: create_terrain_shadow_pipeline(device, layout, shader),
        model: create_model_shadow_pipeline(device, layout, shader),
    }
}

fn create_shadow_uniform_buffer(device: &wgpu::Device, label: &'static str) -> wgpu::Buffer {
    let uniform_zeroes = vec![0_u8; SHADOW_UNIFORM_FLOATS * std::mem::size_of::<f32>()];
    device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some(label),
        contents: &uniform_zeroes,
        usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
    })
}

fn create_shadow_bind_group(
    device: &wgpu::Device,
    label: &'static str,
    layout: &wgpu::BindGroupLayout,
    uniform_buffer: &wgpu::Buffer,
    array_view: &wgpu::TextureView,
    sampler: &wgpu::Sampler,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some(label),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: uniform_buffer.as_entire_binding(),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::TextureView(array_view),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::Sampler(sampler),
            },
        ],
    })
}

fn create_shadow_depth_bind_group(
    device: &wgpu::Device,
    label: &'static str,
    layout: &wgpu::BindGroupLayout,
    uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some(label),
        layout,
        entries: &[wgpu::BindGroupEntry {
            binding: 0,
            resource: uniform_buffer.as_entire_binding(),
        }],
    })
}

fn create_terrain_shadow_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
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
        array_stride: crate::config::TERRAIN_VERTEX_FLOATS as wgpu::BufferAddress * 4,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &ATTRIBUTES,
    }];

    create_shadow_pipeline(
        device,
        layout,
        shader,
        "terrain shadow pipeline",
        "shadowVertexMain",
        &vertex_buffers,
    )
}

fn create_model_shadow_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    const ATTRIBUTES: [wgpu::VertexAttribute; 4] = [
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
            format: wgpu::VertexFormat::Float32x2,
            offset: 6 * 4,
            shader_location: 2,
        },
        wgpu::VertexAttribute {
            format: wgpu::VertexFormat::Float32x4,
            offset: 8 * 4,
            shader_location: 3,
        },
    ];
    let vertex_buffers = [wgpu::VertexBufferLayout {
        array_stride: crate::config::MODEL_VERTEX_FLOATS as wgpu::BufferAddress * 4,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &ATTRIBUTES,
    }];

    create_shadow_pipeline(
        device,
        layout,
        shader,
        "model shadow pipeline",
        "shadowModelVertexMain",
        &vertex_buffers,
    )
}

fn create_shadow_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
    label: &'static str,
    entry_point: &'static str,
    vertex_buffers: &[wgpu::VertexBufferLayout<'_>],
) -> wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some(label),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point,
            compilation_options: Default::default(),
            buffers: vertex_buffers,
        },
        fragment: None,
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: Some(wgpu::Face::Back),
            ..Default::default()
        },
        depth_stencil: Some(wgpu::DepthStencilState {
            format: SHADOW_DEPTH_FORMAT,
            depth_write_enabled: true,
            depth_compare: wgpu::CompareFunction::LessEqual,
            stencil: Default::default(),
            bias: wgpu::DepthBiasState {
                constant: 2,
                slope_scale: 2.0,
                clamp: 0.0,
            },
        }),
        multisample: Default::default(),
        multiview: None,
    })
}
