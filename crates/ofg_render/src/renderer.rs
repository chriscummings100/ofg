//! WGPU pipeline and draw submission for the bootstrap triangle.

use wgpu::util::DeviceExt;

use crate::bootstrap_scene::{clear_color, BootstrapVertex, BOOTSTRAP_VERTICES};

const SHADER_SOURCE: &str = include_str!("shaders/bootstrap.wgsl");

/// Resource creation counters exposed to browser smoke diagnostics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RendererCounters {
    pub pipeline_create_count: u32,
    pub buffer_create_count: u32,
}

/// Small renderer that owns the pipeline and vertex buffer for one triangle.
pub struct BootstrapRenderer {
    pipeline: wgpu::RenderPipeline,
    vertex_buffer: wgpu::Buffer,
    counters: RendererCounters,
}

impl BootstrapRenderer {
    /// Creates stable GPU resources for the bootstrap scene.
    pub fn new(device: &wgpu::Device, format: wgpu::TextureFormat) -> Self {
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("ofg bootstrap shader"),
            source: wgpu::ShaderSource::Wgsl(SHADER_SOURCE.into()),
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("ofg bootstrap pipeline layout"),
            bind_group_layouts: &[],
            immediate_size: 0,
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("ofg bootstrap pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[BootstrapVertex::layout()],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                ..Default::default()
            },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });
        let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("ofg bootstrap vertex buffer"),
            contents: bytemuck::cast_slice(&BOOTSTRAP_VERTICES),
            usage: wgpu::BufferUsages::VERTEX,
        });

        Self {
            pipeline,
            vertex_buffer,
            counters: RendererCounters {
                pipeline_create_count: 1,
                buffer_create_count: 1,
            },
        }
    }

    /// Encodes one render pass into the provided texture view.
    pub fn render_to_view(&self, encoder: &mut wgpu::CommandEncoder, view: &wgpu::TextureView) {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("ofg bootstrap render pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(clear_color()),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
            multiview_mask: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
        pass.draw(0..BOOTSTRAP_VERTICES.len() as u32, 0..1);
    }

    /// Resource creation counters that should not change on ordinary frames.
    pub fn counters(&self) -> RendererCounters {
        self.counters
    }
}
