// Shadow-map visualization support for the native Rust image smoke harness.
// It renders the same terrain meshes into the CSM depth texture array, then
// converts each depth layer into CPU-readable grayscale PNG pixels.

use std::borrow::Cow;

use engine_web::{
    build_shadow_uniform_values, SHADOW_CASCADE_COUNT, SHADOW_DEBUG_MODE_OFFSET, SHADOW_MAP_SIZE,
    SHADOW_UNIFORM_FLOATS, TERRAIN_VERTEX_FLOATS, WORLD_MATRIX_FLOATS,
};
use terrain_core::MeshData;
use wgpu::util::DeviceExt;

use super::error::{harness_error, HarnessResult};
use super::renderer::{
    align_to, f32_as_bytes, read_rgba_output, u32_as_bytes, uniform_byte_len, CameraSetup,
};

const SHADOW_VISUALIZER_SOURCE: &str = r#"
struct LayerUniform {
  layer: vec4<u32>,
};

@group(0) @binding(0) var shadowTexture: texture_depth_2d_array;
@group(0) @binding(1) var<uniform> layerUniform: LayerUniform;

struct VertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex
fn vertexMain(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
  var positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  let position = positions[vertexIndex];

  var output: VertexOutput;
  output.clipPosition = vec4<f32>(position, 0.0, 1.0);
  output.uv = vec2<f32>(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
  let dimensions = textureDimensions(shadowTexture);
  let clampedUv = clamp(input.uv, vec2<f32>(0.0), vec2<f32>(0.999999));
  let texel = vec2<i32>(
    i32(clampedUv.x * f32(dimensions.x)),
    i32(clampedUv.y * f32(dimensions.y))
  );
  let depth = textureLoad(shadowTexture, texel, i32(layerUniform.layer.x), 0);
  if (depth >= 0.9999) {
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
  }

  let value = clamp(0.08 + (1.0 - depth) * 8.0, 0.0, 1.0);
  return vec4<f32>(vec3<f32>(value), 1.0);
}
"#;

const SHADOW_DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;
const COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;

pub struct ShadowDebugRenderer {
    shadow_bind_group_layout: wgpu::BindGroupLayout,
    visualization_bind_group_layout: wgpu::BindGroupLayout,
    shadow_pipeline: wgpu::RenderPipeline,
    visualization_pipeline: wgpu::RenderPipeline,
}

pub struct ShadowDebugOutput {
    pub layers: Vec<ShadowDebugLayer>,
    pub atlas: Vec<u8>,
    pub atlas_width: u32,
    pub atlas_height: u32,
}

pub struct ShadowDebugLayer {
    pub cascade_index: usize,
    pub pixels: Vec<u8>,
}

struct ShadowTextureResources {
    _texture: wgpu::Texture,
    layer_views: Vec<wgpu::TextureView>,
    array_view: wgpu::TextureView,
}

struct ShadowMesh {
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    index_count: u32,
}

impl ShadowDebugRenderer {
    /// Creates depth-render and depth-visualization pipelines for smoke dumps.
    pub fn new(
        device: &wgpu::Device,
        camera_bind_group_layout: &wgpu::BindGroupLayout,
        object_bind_group_layout: &wgpu::BindGroupLayout,
        shader: &wgpu::ShaderModule,
    ) -> Self {
        let shadow_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("smoke shadow depth bind group layout"),
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
        let shadow_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("smoke shadow pipeline layout"),
                bind_group_layouts: &[
                    camera_bind_group_layout,
                    object_bind_group_layout,
                    &shadow_bind_group_layout,
                ],
                push_constant_ranges: &[],
            });
        let visualization_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("smoke shadow visualization bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Texture {
                            sample_type: wgpu::TextureSampleType::Depth,
                            view_dimension: wgpu::TextureViewDimension::D2Array,
                            multisampled: false,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Uniform,
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                ],
            });
        let visualization_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("smoke shadow visualization shader"),
            source: wgpu::ShaderSource::Wgsl(Cow::Borrowed(SHADOW_VISUALIZER_SOURCE)),
        });
        let visualization_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("smoke shadow visualization pipeline layout"),
                bind_group_layouts: &[&visualization_bind_group_layout],
                push_constant_ranges: &[],
            });

        Self {
            shadow_bind_group_layout,
            visualization_bind_group_layout,
            shadow_pipeline: create_shadow_pipeline(device, &shadow_pipeline_layout, shader),
            visualization_pipeline: create_visualization_pipeline(
                device,
                &visualization_pipeline_layout,
                &visualization_shader,
            ),
        }
    }

    /// Renders and visualizes all configured shadow cascade layers.
    pub fn render(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        camera_bind_group: &wgpu::BindGroup,
        object_bind_group: &wgpu::BindGroup,
        camera: &CameraSetup,
        meshes: &[MeshData],
    ) -> HarnessResult<ShadowDebugOutput> {
        let cascades = camera.shadow_cascades()?;
        let base_uniforms =
            build_shadow_uniform_values(&cascades, true, 0.0015, 0.0, 1.0 / SHADOW_MAP_SIZE as f32)
                .map_err(|error| harness_error(error.to_string()))?;
        let shadow_texture = create_shadow_texture_resources(device);
        let shadow_meshes = meshes
            .iter()
            .map(|mesh| create_shadow_mesh(device, mesh))
            .collect::<HarnessResult<Vec<_>>>()?;
        let cascade_uniform_buffers =
            create_cascade_uniform_buffers(device, queue, &base_uniforms, &cascades);
        let cascade_bind_groups = cascade_uniform_buffers
            .iter()
            .map(|buffer| create_shadow_bind_group(device, &self.shadow_bind_group_layout, buffer))
            .collect::<Vec<_>>();
        let visualization_targets = create_visualization_targets(device);
        let layer_uniform_buffers = create_layer_uniform_buffers(device);
        let visualization_bind_groups = layer_uniform_buffers
            .iter()
            .map(|buffer| {
                create_visualization_bind_group(
                    device,
                    &self.visualization_bind_group_layout,
                    &shadow_texture.array_view,
                    buffer,
                )
            })
            .collect::<Vec<_>>();

        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("smoke shadow debug encoder"),
        });
        for cascade_index in 0..SHADOW_CASCADE_COUNT {
            render_shadow_layer(
                &mut encoder,
                &self.shadow_pipeline,
                camera_bind_group,
                object_bind_group,
                &cascade_bind_groups[cascade_index],
                &shadow_texture.layer_views[cascade_index],
                &shadow_meshes,
            );
        }
        for cascade_index in 0..SHADOW_CASCADE_COUNT {
            render_visualization_layer(
                &mut encoder,
                &self.visualization_pipeline,
                &visualization_bind_groups[cascade_index],
                &visualization_targets[cascade_index],
            );
        }
        queue.submit(Some(encoder.finish()));

        let mut layers = Vec::with_capacity(SHADOW_CASCADE_COUNT);
        for (cascade_index, target) in visualization_targets.iter().enumerate() {
            let pixels = read_rgba_output(
                device,
                &target.output_buffer,
                SHADOW_MAP_SIZE,
                SHADOW_MAP_SIZE,
                SHADOW_MAP_SIZE * 4,
                target.padded_bytes_per_row,
            )?;
            layers.push(ShadowDebugLayer {
                cascade_index,
                pixels,
            });
        }
        let atlas = build_shadow_atlas(&layers);

        Ok(ShadowDebugOutput {
            layers,
            atlas,
            atlas_width: SHADOW_MAP_SIZE * 2,
            atlas_height: SHADOW_MAP_SIZE * 2,
        })
    }
}

struct VisualizationTarget {
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    output_buffer: wgpu::Buffer,
    padded_bytes_per_row: u32,
}

fn create_shadow_texture_resources(device: &wgpu::Device) -> ShadowTextureResources {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("smoke shadow texture array"),
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
                label: Some("smoke shadow cascade layer view"),
                dimension: Some(wgpu::TextureViewDimension::D2),
                base_array_layer: layer,
                array_layer_count: Some(1),
                ..Default::default()
            })
        })
        .collect::<Vec<_>>();
    let array_view = texture.create_view(&wgpu::TextureViewDescriptor {
        label: Some("smoke shadow texture array view"),
        dimension: Some(wgpu::TextureViewDimension::D2Array),
        base_array_layer: 0,
        array_layer_count: Some(SHADOW_CASCADE_COUNT as u32),
        ..Default::default()
    });

    ShadowTextureResources {
        _texture: texture,
        layer_views,
        array_view,
    }
}

fn create_cascade_uniform_buffers(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    base_uniforms: &[f32; SHADOW_UNIFORM_FLOATS],
    cascades: &engine_web::ShadowCascadeSet,
) -> Vec<wgpu::Buffer> {
    (0..SHADOW_CASCADE_COUNT)
        .map(|cascade_index| {
            let mut uniforms = *base_uniforms;
            uniforms[0..WORLD_MATRIX_FLOATS]
                .copy_from_slice(&cascades.cascades[cascade_index].light_view_projection);
            uniforms[SHADOW_DEBUG_MODE_OFFSET] = 0.0;
            let buffer = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("smoke shadow cascade uniforms"),
                size: uniform_byte_len(SHADOW_UNIFORM_FLOATS),
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
            queue.write_buffer(&buffer, 0, f32_as_bytes(&uniforms));
            buffer
        })
        .collect()
}

fn create_shadow_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("smoke shadow bind group"),
        layout,
        entries: &[wgpu::BindGroupEntry {
            binding: 0,
            resource: uniform_buffer.as_entire_binding(),
        }],
    })
}

fn create_layer_uniform_buffers(device: &wgpu::Device) -> Vec<wgpu::Buffer> {
    (0..SHADOW_CASCADE_COUNT)
        .map(|cascade_index| {
            let values = [cascade_index as u32, 0, 0, 0];
            device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("smoke shadow visualization layer uniform"),
                contents: u32_as_bytes(&values),
                usage: wgpu::BufferUsages::UNIFORM,
            })
        })
        .collect()
}

fn create_visualization_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    array_view: &wgpu::TextureView,
    layer_uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("smoke shadow visualization bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(array_view),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: layer_uniform_buffer.as_entire_binding(),
            },
        ],
    })
}

fn create_visualization_targets(device: &wgpu::Device) -> Vec<VisualizationTarget> {
    let unpadded_bytes_per_row = SHADOW_MAP_SIZE * 4;
    let padded_bytes_per_row = align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
    let output_buffer_size = padded_bytes_per_row as u64 * SHADOW_MAP_SIZE as u64;

    (0..SHADOW_CASCADE_COUNT)
        .map(|_| {
            let texture = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("smoke shadow visualization texture"),
                size: wgpu::Extent3d {
                    width: SHADOW_MAP_SIZE,
                    height: SHADOW_MAP_SIZE,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: COLOR_FORMAT,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
                view_formats: &[],
            });
            let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
            let output_buffer = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("smoke shadow visualization readback buffer"),
                size: output_buffer_size,
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            });

            VisualizationTarget {
                texture,
                view,
                output_buffer,
                padded_bytes_per_row,
            }
        })
        .collect()
}

fn render_shadow_layer(
    encoder: &mut wgpu::CommandEncoder,
    pipeline: &wgpu::RenderPipeline,
    camera_bind_group: &wgpu::BindGroup,
    object_bind_group: &wgpu::BindGroup,
    shadow_bind_group: &wgpu::BindGroup,
    layer_view: &wgpu::TextureView,
    meshes: &[ShadowMesh],
) {
    let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
        label: Some("smoke shadow depth pass"),
        color_attachments: &[],
        depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
            view: layer_view,
            depth_ops: Some(wgpu::Operations {
                load: wgpu::LoadOp::Clear(1.0),
                store: wgpu::StoreOp::Store,
            }),
            stencil_ops: None,
        }),
        timestamp_writes: None,
        occlusion_query_set: None,
    });
    pass.set_pipeline(pipeline);
    pass.set_bind_group(0, camera_bind_group, &[]);
    pass.set_bind_group(1, object_bind_group, &[]);
    pass.set_bind_group(2, shadow_bind_group, &[]);
    for mesh in meshes {
        pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
        pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        pass.draw_indexed(0..mesh.index_count, 0, 0..1);
    }
}

fn render_visualization_layer(
    encoder: &mut wgpu::CommandEncoder,
    pipeline: &wgpu::RenderPipeline,
    bind_group: &wgpu::BindGroup,
    target: &VisualizationTarget,
) {
    let color_attachments = [Some(wgpu::RenderPassColorAttachment {
        view: &target.view,
        resolve_target: None,
        ops: wgpu::Operations {
            load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
            store: wgpu::StoreOp::Store,
        },
    })];
    {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("smoke shadow visualization pass"),
            color_attachments: &color_attachments,
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(pipeline);
        pass.set_bind_group(0, bind_group, &[]);
        pass.draw(0..3, 0..1);
    }
    encoder.copy_texture_to_buffer(
        wgpu::ImageCopyTexture {
            texture: &target.texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        wgpu::ImageCopyBuffer {
            buffer: &target.output_buffer,
            layout: wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(target.padded_bytes_per_row),
                rows_per_image: Some(SHADOW_MAP_SIZE),
            },
        },
        wgpu::Extent3d {
            width: SHADOW_MAP_SIZE,
            height: SHADOW_MAP_SIZE,
            depth_or_array_layers: 1,
        },
    );
}

fn create_shadow_mesh(device: &wgpu::Device, mesh: &MeshData) -> HarnessResult<ShadowMesh> {
    if mesh.vertices.is_empty()
        || mesh.indices.is_empty()
        || mesh.vertices.len() % TERRAIN_VERTEX_FLOATS as usize != 0
        || mesh.indices.len() % 3 != 0
    {
        return Err(harness_error(
            "Rust smoke received an invalid terrain mesh for shadow debug.",
        ));
    }

    let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("smoke shadow terrain vertices"),
        contents: f32_as_bytes(&mesh.vertices),
        usage: wgpu::BufferUsages::VERTEX,
    });
    let index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("smoke shadow terrain indices"),
        contents: u32_as_bytes(&mesh.indices),
        usage: wgpu::BufferUsages::INDEX,
    });

    Ok(ShadowMesh {
        vertex_buffer,
        index_buffer,
        index_count: mesh.indices.len() as u32,
    })
}

fn create_shadow_pipeline(
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
        array_stride: TERRAIN_VERTEX_FLOATS as wgpu::BufferAddress * 4,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &ATTRIBUTES,
    }];

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("smoke terrain shadow pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "shadowVertexMain",
            compilation_options: Default::default(),
            buffers: &vertex_buffers,
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

fn create_visualization_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("smoke shadow visualization pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "vertexMain",
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "fragmentMain",
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: COLOR_FORMAT,
                blend: None,
                write_mask: wgpu::ColorWrites::ALL,
            })],
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

fn build_shadow_atlas(layers: &[ShadowDebugLayer]) -> Vec<u8> {
    let atlas_width = SHADOW_MAP_SIZE * 2;
    let atlas_height = SHADOW_MAP_SIZE * 2;
    let mut atlas = vec![0_u8; (atlas_width * atlas_height * 4) as usize];
    for layer in layers {
        let tile_x = (layer.cascade_index as u32 % 2) * SHADOW_MAP_SIZE;
        let tile_y = (layer.cascade_index as u32 / 2) * SHADOW_MAP_SIZE;
        for y in 0..SHADOW_MAP_SIZE {
            let source_start = ((y * SHADOW_MAP_SIZE) * 4) as usize;
            let source_end = source_start + (SHADOW_MAP_SIZE * 4) as usize;
            let target_start = (((tile_y + y) * atlas_width + tile_x) * 4) as usize;
            let target_end = target_start + (SHADOW_MAP_SIZE * 4) as usize;
            atlas[target_start..target_end]
                .copy_from_slice(&layer.pixels[source_start..source_end]);
        }
    }

    atlas
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shadow_atlas_places_four_layers_in_stable_tiles() {
        let layers = (0..SHADOW_CASCADE_COUNT)
            .map(|cascade_index| ShadowDebugLayer {
                cascade_index,
                pixels: vec![cascade_index as u8; (SHADOW_MAP_SIZE * SHADOW_MAP_SIZE * 4) as usize],
            })
            .collect::<Vec<_>>();

        let atlas = build_shadow_atlas(&layers);
        let atlas_width = SHADOW_MAP_SIZE * 2;

        assert_eq!(atlas[0], 0);
        assert_eq!(atlas[(SHADOW_MAP_SIZE * 4) as usize], 1);
        assert_eq!(atlas[((SHADOW_MAP_SIZE * atlas_width) * 4) as usize], 2);
        assert_eq!(
            atlas[((SHADOW_MAP_SIZE * atlas_width + SHADOW_MAP_SIZE) * 4) as usize],
            3
        );
    }
}
