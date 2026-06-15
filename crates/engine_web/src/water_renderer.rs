// Dormant WebGPU resources for the retired sea-level water composite pass.
// The sine-grass terrain rebuild baseline has no water, so the active renderer
// bypasses this module and writes the scene directly to post-process targets.
#![allow(dead_code)]

use std::collections::BTreeMap;

use wgpu::util::DeviceExt;

use crate::post_process::{POST_PROCESS_COLOR_FORMAT, POST_PROCESS_LINEAR_DEPTH_FORMAT};
use crate::water::{WaterBathymetryCoverage, WaterBathymetryError, WaterSettings};
use terrain_core::{TerrainNodeKey, WaterNodePacket, WATER_NODE_BATHYMETRY_TEXEL_COUNT};

const WATER_SHADER_SOURCE: &str = include_str!("../../../src/engine/render/shaders/water.wgsl");
const WATER_UNIFORM_FLOATS: usize = 48;
const BATHYMETRY_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::R32Float;
const WATER_PATCH_INSTANCE_FLOATS: usize = 12;
const WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS: u32 = 32;
const WATER_BATHYMETRY_ATLAS_TILE_COUNT: u32 =
    WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS * WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS;
const WATER_BATHYMETRY_ATLAS_SIZE: u32 =
    WATER_NODE_BATHYMETRY_TEXEL_COUNT * WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS;
const REFLECTION_SCALE_DIVISOR: u32 = 2;

pub(crate) struct WaterRendererResources {
    opaque_color: WaterTarget,
    opaque_linear_depth: WaterTarget,
    bathymetry_texture: wgpu::Texture,
    bathymetry_view: wgpu::TextureView,
    patches: BTreeMap<TerrainNodeKey, WaterPatchResource>,
    free_bathymetry_tiles: Vec<u32>,
    next_bathymetry_tile: u32,
    patch_instance_buffer: wgpu::Buffer,
    reflection_color: WaterTarget,
    reflection_linear_depth: WaterTarget,
    reflection_depth: WaterTarget,
    bind_group_layout: wgpu::BindGroupLayout,
    bind_group: wgpu::BindGroup,
    sampler: wgpu::Sampler,
    uniform_buffer: wgpu::Buffer,
    copy_pipeline: wgpu::RenderPipeline,
    patch_pipeline: wgpu::RenderPipeline,
}

struct WaterTarget {
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
    width: u32,
    height: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct WaterPatchResource {
    tile_index: u32,
    origin_x: f32,
    origin_z: f32,
    world_span_x: f32,
    world_span_z: f32,
    texel_count: u32,
    sea_level_meters: f32,
    max_depth_meters: f32,
}

impl WaterRendererResources {
    /// Creates resize-owned water resources and the fullscreen composite pipeline.
    pub(crate) fn new(
        device: &wgpu::Device,
        camera_bind_group_layout: &wgpu::BindGroupLayout,
        width: u32,
        height: u32,
    ) -> Self {
        let width = width.max(1);
        let height = height.max(1);
        let opaque_color = create_color_target(device, "water opaque scene color", width, height);
        let opaque_linear_depth =
            create_linear_depth_target(device, "water opaque linear depth", width, height);
        let reflection_width = reflection_extent(width);
        let reflection_height = reflection_extent(height);
        let reflection_color = create_color_target(
            device,
            "water reflection scene color",
            reflection_width,
            reflection_height,
        );
        let reflection_linear_depth = create_linear_depth_target(
            device,
            "water reflection linear depth",
            reflection_width,
            reflection_height,
        );
        let reflection_depth = create_depth_target(
            device,
            "water reflection depth",
            reflection_width,
            reflection_height,
        );
        let (bathymetry_texture, bathymetry_view) = create_bathymetry_texture(device);
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("water sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("water uniforms"),
            contents: f32_as_bytes(&[0.0; WATER_UNIFORM_FLOATS]),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        let patch_instance_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("water patch instances"),
            size: (WATER_BATHYMETRY_ATLAS_TILE_COUNT as u64
                * WATER_PATCH_INSTANCE_FLOATS as u64
                * 4),
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let bind_group_layout = create_bind_group_layout(device);
        let bind_group = create_bind_group(
            device,
            &bind_group_layout,
            &opaque_color.view,
            &opaque_linear_depth.view,
            &bathymetry_view,
            &reflection_color.view,
            &sampler,
            &uniform_buffer,
        );
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("water shader"),
            source: wgpu::ShaderSource::Wgsl(WATER_SHADER_SOURCE.into()),
        });
        let copy_pipeline = create_copy_pipeline(
            device,
            camera_bind_group_layout,
            &bind_group_layout,
            &shader,
        );
        let patch_pipeline = create_patch_pipeline(
            device,
            camera_bind_group_layout,
            &bind_group_layout,
            &shader,
        );

        Self {
            opaque_color,
            opaque_linear_depth,
            bathymetry_texture,
            bathymetry_view,
            patches: BTreeMap::new(),
            free_bathymetry_tiles: Vec::new(),
            next_bathymetry_tile: 0,
            patch_instance_buffer,
            reflection_color,
            reflection_linear_depth,
            reflection_depth,
            bind_group_layout,
            bind_group,
            sampler,
            uniform_buffer,
            copy_pipeline,
            patch_pipeline,
        }
    }

    /// Recreates viewport-dependent render targets and bind groups.
    pub(crate) fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        let width = width.max(1);
        let height = height.max(1);
        self.opaque_color = create_color_target(device, "water opaque scene color", width, height);
        self.opaque_linear_depth =
            create_linear_depth_target(device, "water opaque linear depth", width, height);
        let reflection_width = reflection_extent(width);
        let reflection_height = reflection_extent(height);
        self.reflection_color = create_color_target(
            device,
            "water reflection scene color",
            reflection_width,
            reflection_height,
        );
        self.reflection_linear_depth = create_linear_depth_target(
            device,
            "water reflection linear depth",
            reflection_width,
            reflection_height,
        );
        self.reflection_depth = create_depth_target(
            device,
            "water reflection depth",
            reflection_width,
            reflection_height,
        );
        self.recreate_bind_group(device);
    }

    /// Returns the target view for the opaque scene color pass.
    pub(crate) fn opaque_scene_color_view(&self) -> &wgpu::TextureView {
        &self.opaque_color.view
    }

    /// Returns the target view for the opaque scene linear-depth pass.
    pub(crate) fn opaque_linear_depth_view(&self) -> &wgpu::TextureView {
        &self.opaque_linear_depth.view
    }

    /// Returns the target view for the half-resolution reflection color pass.
    pub(crate) fn reflection_scene_color_view(&self) -> &wgpu::TextureView {
        &self.reflection_color.view
    }

    /// Returns the target view for the half-resolution reflection linear depth pass.
    pub(crate) fn reflection_linear_depth_view(&self) -> &wgpu::TextureView {
        &self.reflection_linear_depth.view
    }

    /// Returns the depth target view for the half-resolution reflection pass.
    pub(crate) fn reflection_depth_view(&self) -> &wgpu::TextureView {
        &self.reflection_depth.view
    }

    /// Returns the current reflection target dimensions.
    pub(crate) fn reflection_size(&self) -> (u32, u32) {
        (self.reflection_color.width, self.reflection_color.height)
    }

    /// Returns coverage metadata for the latest uploaded terrain-job bathymetry packets.
    pub(crate) fn bathymetry_coverage(&self) -> Option<WaterBathymetryCoverage> {
        let first_patch = self.patches.values().next()?;
        Some(WaterBathymetryCoverage {
            texel_count: WATER_NODE_BATHYMETRY_TEXEL_COUNT,
            world_span_meters: first_patch.world_span_x.max(first_patch.world_span_z),
            center_x: first_patch.origin_x + first_patch.world_span_x * 0.5,
            center_z: first_patch.origin_z + first_patch.world_span_z * 0.5,
            patch_count: self.patches.len().min(u32::MAX as usize) as u32,
        })
    }

    /// Uploads or replaces one terrain-job water patch in the bathymetry atlas.
    pub(crate) fn upsert_water_patch(
        &mut self,
        queue: &wgpu::Queue,
        key: TerrainNodeKey,
        packet: &WaterNodePacket,
    ) -> Result<bool, WaterBathymetryError> {
        validate_water_patch(packet)?;
        let tile_index = match self.patches.get(&key) {
            Some(existing) => existing.tile_index,
            None => self.allocate_bathymetry_tile()?,
        };
        self.upload_water_patch(queue, tile_index, packet);
        self.patches.insert(
            key,
            WaterPatchResource {
                tile_index,
                origin_x: packet.origin_x,
                origin_z: packet.origin_z,
                world_span_x: packet.world_span_x,
                world_span_z: packet.world_span_z,
                texel_count: packet.texel_count,
                sea_level_meters: packet.sea_level_meters,
                max_depth_meters: packet.max_depth_meters,
            },
        );
        Ok(true)
    }

    /// Removes one uploaded water patch and releases its atlas tile.
    pub(crate) fn remove_water_patch(&mut self, key: TerrainNodeKey) -> bool {
        let Some(patch) = self.patches.remove(&key) else {
            return false;
        };
        self.free_bathymetry_tiles.push(patch.tile_index);
        true
    }

    /// Clears all uploaded water patches, used when terrain is reset.
    pub(crate) fn clear_water_patches(&mut self) {
        self.patches.clear();
        self.free_bathymetry_tiles.clear();
        self.next_bathymetry_tile = 0;
    }

    fn allocate_bathymetry_tile(&mut self) -> Result<u32, WaterBathymetryError> {
        if let Some(tile) = self.free_bathymetry_tiles.pop() {
            return Ok(tile);
        }
        if self.next_bathymetry_tile >= WATER_BATHYMETRY_ATLAS_TILE_COUNT {
            return Err(WaterBathymetryError::AtlasFull);
        }
        let tile = self.next_bathymetry_tile;
        self.next_bathymetry_tile += 1;
        Ok(tile)
    }

    fn upload_water_patch(&self, queue: &wgpu::Queue, tile_index: u32, packet: &WaterNodePacket) {
        let tile_x = (tile_index % WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS)
            * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        let tile_y = (tile_index / WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS)
            * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
        queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &self.bathymetry_texture,
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

    /// Composites sea water into final scene targets for post-process.
    pub(crate) fn render(
        &self,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        camera_bind_group: &wgpu::BindGroup,
        output_scene_color: &wgpu::TextureView,
        output_linear_depth: &wgpu::TextureView,
        settings: WaterSettings,
        time_seconds: f32,
        reflection_view_projection: &[f32; 16],
    ) {
        let instances = self.patch_instance_values();
        queue.write_buffer(
            &self.uniform_buffer,
            0,
            f32_as_bytes(&self.uniform_values(settings, time_seconds, reflection_view_projection)),
        );
        if !instances.is_empty() {
            queue.write_buffer(&self.patch_instance_buffer, 0, f32_as_bytes(&instances));
        }

        let color_attachments = [
            Some(wgpu::RenderPassColorAttachment {
                view: output_scene_color,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                    store: wgpu::StoreOp::Store,
                },
            }),
            Some(wgpu::RenderPassColorAttachment {
                view: output_linear_depth,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                    store: wgpu::StoreOp::Store,
                },
            }),
        ];
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("water composite pass"),
            color_attachments: &color_attachments,
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.copy_pipeline);
        pass.set_bind_group(0, camera_bind_group, &[]);
        pass.set_bind_group(1, &self.bind_group, &[]);
        pass.draw(0..3, 0..1);
        if settings.enabled && !instances.is_empty() {
            let instance_count = (instances.len() / WATER_PATCH_INSTANCE_FLOATS) as u32;
            pass.set_pipeline(&self.patch_pipeline);
            pass.set_bind_group(0, camera_bind_group, &[]);
            pass.set_bind_group(1, &self.bind_group, &[]);
            pass.set_vertex_buffer(0, self.patch_instance_buffer.slice(..));
            pass.draw(0..6, 0..instance_count);
        }
    }

    fn recreate_bind_group(&mut self, device: &wgpu::Device) {
        self.bind_group = create_bind_group(
            device,
            &self.bind_group_layout,
            &self.opaque_color.view,
            &self.opaque_linear_depth.view,
            &self.bathymetry_view,
            &self.reflection_color.view,
            &self.sampler,
            &self.uniform_buffer,
        );
    }

    fn uniform_values(
        &self,
        settings: WaterSettings,
        time_seconds: f32,
        reflection_view_projection: &[f32; 16],
    ) -> [f32; WATER_UNIFORM_FLOATS] {
        let reflection_enabled =
            settings.enabled && settings.reflection_enabled && self.reflection_color.width > 0;
        let mut values = [
            if settings.enabled { 1.0 } else { 0.0 },
            if reflection_enabled { 1.0 } else { 0.0 },
            settings.sea_level_meters,
            time_seconds,
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
            self.reflection_color.width as f32,
            self.reflection_color.height as f32,
            WATER_BATHYMETRY_ATLAS_SIZE as f32,
            WATER_BATHYMETRY_ATLAS_SIZE as f32,
            1.0 / WATER_BATHYMETRY_ATLAS_SIZE as f32,
            self.patches.len().min(u32::MAX as usize) as f32,
            self.opaque_color.width as f32,
            self.opaque_color.height as f32,
            1.0 / self.opaque_color.width.max(1) as f32,
            1.0 / self.opaque_color.height.max(1) as f32,
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
        values[32..48].copy_from_slice(reflection_view_projection);
        values
    }

    fn patch_instance_values(&self) -> Vec<f32> {
        let mut values = Vec::with_capacity(self.patches.len() * WATER_PATCH_INSTANCE_FLOATS);
        for patch in self.patches.values() {
            let tile_x = (patch.tile_index % WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS)
                * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
            let tile_y = (patch.tile_index / WATER_BATHYMETRY_ATLAS_TILES_PER_AXIS)
                * WATER_NODE_BATHYMETRY_TEXEL_COUNT;
            values.extend_from_slice(&[
                patch.origin_x,
                patch.origin_z,
                patch.world_span_x,
                patch.world_span_z,
                tile_x as f32,
                tile_y as f32,
                patch.texel_count as f32,
                patch.max_depth_meters,
                patch.sea_level_meters,
                0.0,
                0.0,
                0.0,
            ]);
        }
        values
    }
}

fn create_bind_group_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("water bind group layout"),
        entries: &[
            texture_binding(0, wgpu::TextureSampleType::Float { filterable: true }),
            texture_binding(1, wgpu::TextureSampleType::Float { filterable: false }),
            texture_binding(2, wgpu::TextureSampleType::Float { filterable: false }),
            texture_binding(3, wgpu::TextureSampleType::Float { filterable: true }),
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

fn create_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    opaque_color: &wgpu::TextureView,
    opaque_linear_depth: &wgpu::TextureView,
    bathymetry: &wgpu::TextureView,
    reflection_color: &wgpu::TextureView,
    sampler: &wgpu::Sampler,
    uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("water bind group"),
        layout,
        entries: &[
            bind_texture(0, opaque_color),
            bind_texture(1, opaque_linear_depth),
            bind_texture(2, bathymetry),
            bind_texture(3, reflection_color),
            wgpu::BindGroupEntry {
                binding: 4,
                resource: wgpu::BindingResource::Sampler(sampler),
            },
            wgpu::BindGroupEntry {
                binding: 5,
                resource: uniform_buffer.as_entire_binding(),
            },
        ],
    })
}

fn create_copy_pipeline(
    device: &wgpu::Device,
    camera_bind_group_layout: &wgpu::BindGroupLayout,
    water_bind_group_layout: &wgpu::BindGroupLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("water pipeline layout"),
        bind_group_layouts: &[camera_bind_group_layout, water_bind_group_layout],
        push_constant_ranges: &[],
    });

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("water copy pipeline"),
        layout: Some(&layout),
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
            targets: &[
                Some(wgpu::ColorTargetState {
                    format: POST_PROCESS_COLOR_FORMAT,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                }),
                Some(wgpu::ColorTargetState {
                    format: POST_PROCESS_LINEAR_DEPTH_FORMAT,
                    blend: None,
                    write_mask: wgpu::ColorWrites::RED,
                }),
            ],
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

fn create_patch_pipeline(
    device: &wgpu::Device,
    camera_bind_group_layout: &wgpu::BindGroupLayout,
    water_bind_group_layout: &wgpu::BindGroupLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("water patch pipeline layout"),
        bind_group_layouts: &[camera_bind_group_layout, water_bind_group_layout],
        push_constant_ranges: &[],
    });
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
        label: Some("water patch pipeline"),
        layout: Some(&layout),
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
            targets: &[
                Some(wgpu::ColorTargetState {
                    format: POST_PROCESS_COLOR_FORMAT,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                }),
                Some(wgpu::ColorTargetState {
                    format: POST_PROCESS_LINEAR_DEPTH_FORMAT,
                    blend: None,
                    write_mask: wgpu::ColorWrites::RED,
                }),
            ],
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

fn create_color_target(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
) -> WaterTarget {
    create_target(device, label, width, height, POST_PROCESS_COLOR_FORMAT)
}

fn create_linear_depth_target(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
) -> WaterTarget {
    create_target(
        device,
        label,
        width,
        height,
        POST_PROCESS_LINEAR_DEPTH_FORMAT,
    )
}

fn create_depth_target(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
) -> WaterTarget {
    create_target(
        device,
        label,
        width,
        height,
        wgpu::TextureFormat::Depth24Plus,
    )
}

fn create_target(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
    format: wgpu::TextureFormat,
) -> WaterTarget {
    let usage = if format == wgpu::TextureFormat::Depth24Plus {
        wgpu::TextureUsages::RENDER_ATTACHMENT
    } else {
        wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING
    };
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some(label),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format,
        usage,
        view_formats: &[],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    WaterTarget {
        _texture: texture,
        view,
        width,
        height,
    }
}

fn create_bathymetry_texture(device: &wgpu::Device) -> (wgpu::Texture, wgpu::TextureView) {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("water bathymetry atlas"),
        size: wgpu::Extent3d {
            width: WATER_BATHYMETRY_ATLAS_SIZE,
            height: WATER_BATHYMETRY_ATLAS_SIZE,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: BATHYMETRY_FORMAT,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    (texture, view)
}

fn validate_water_patch(packet: &WaterNodePacket) -> Result<(), WaterBathymetryError> {
    let expected_texels = WATER_NODE_BATHYMETRY_TEXEL_COUNT;
    let expected_len = (expected_texels * expected_texels) as usize;
    if packet.texel_count != expected_texels
        || packet.depths_meters.len() != expected_len
        || packet.world_span_x <= 0.0
        || packet.world_span_z <= 0.0
        || packet.max_depth_meters <= 0.0
        || !packet.origin_x.is_finite()
        || !packet.origin_z.is_finite()
        || !packet.world_span_x.is_finite()
        || !packet.world_span_z.is_finite()
        || !packet.sea_level_meters.is_finite()
        || !packet.max_depth_meters.is_finite()
        || packet
            .depths_meters
            .iter()
            .any(|depth| !depth.is_finite() || *depth < 0.0)
    {
        return Err(WaterBathymetryError::InvalidWaterPacket);
    }

    Ok(())
}

fn texture_binding(
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

fn bind_texture(binding: u32, view: &wgpu::TextureView) -> wgpu::BindGroupEntry<'_> {
    wgpu::BindGroupEntry {
        binding,
        resource: wgpu::BindingResource::TextureView(view),
    }
}

fn reflection_extent(value: u32) -> u32 {
    (value / REFLECTION_SCALE_DIVISOR).max(1)
}

fn f32_as_bytes(values: &[f32]) -> &[u8] {
    unsafe {
        // SAFETY: `f32` has no invalid bit patterns, and the returned byte slice
        // is tied to the input slice lifetime for immediate GPU upload.
        std::slice::from_raw_parts(values.as_ptr().cast::<u8>(), std::mem::size_of_val(values))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::post_process::{POST_PROCESS_COLOR_FORMAT, POST_PROCESS_LINEAR_DEPTH_FORMAT};

    #[test]
    fn water_resources_render_disabled_copy_path_to_native_targets() {
        pollster::block_on(async {
            let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
                backends: wgpu::Backends::PRIMARY,
                ..Default::default()
            });
            let Some(adapter) = instance
                .request_adapter(&wgpu::RequestAdapterOptions {
                    power_preference: wgpu::PowerPreference::HighPerformance,
                    force_fallback_adapter: false,
                    compatible_surface: None,
                })
                .await
            else {
                eprintln!("Skipping water GPU test because no native adapter is available.");
                return;
            };
            let limits =
                wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
            let Ok((device, queue)) = adapter
                .request_device(
                    &wgpu::DeviceDescriptor {
                        label: Some("water test device"),
                        required_features: wgpu::Features::empty(),
                        required_limits: limits,
                    },
                    None,
                )
                .await
            else {
                eprintln!("Skipping water GPU test because no native device is available.");
                return;
            };

            let camera_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("water test camera layout"),
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
            let camera_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("water test camera uniforms"),
                contents: f32_as_bytes(&[0.0; crate::FRAME_UNIFORM_FLOATS]),
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            });
            let camera_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("water test camera bind group"),
                layout: &camera_layout,
                entries: &[wgpu::BindGroupEntry {
                    binding: 0,
                    resource: camera_buffer.as_entire_binding(),
                }],
            });
            let mut resources = WaterRendererResources::new(&device, &camera_layout, 64, 32);
            assert_eq!(resources.reflection_size(), (32, 16));
            assert!(resources.bathymetry_coverage().is_none());
            let _reflection_color_view = resources.reflection_scene_color_view();
            let _reflection_linear_depth_view = resources.reflection_linear_depth_view();
            let _reflection_depth_view = resources.reflection_depth_view();

            let enabled_settings = WaterSettings::default();
            let enabled_uniforms =
                resources.uniform_values(enabled_settings, 4.25, &identity_mat4());
            assert_eq!(enabled_uniforms[0], 1.0);
            assert_eq!(enabled_uniforms[1], 0.0);
            assert_eq!(enabled_uniforms[3], 4.25);
            assert_eq!(enabled_uniforms[24], WATER_BATHYMETRY_ATLAS_SIZE as f32);
            assert_eq!(enabled_uniforms[25], WATER_BATHYMETRY_ATLAS_SIZE as f32);
            assert_eq!(enabled_uniforms[27], 0.0);
            let reflection_uniforms = resources.uniform_values(
                enabled_settings
                    .apply_update(crate::water::WaterSettingsUpdate {
                        reflection_enabled: Some(true),
                        ..crate::water::WaterSettingsUpdate::default()
                    })
                    .expect("reflection opt-in should validate"),
                4.25,
                &identity_mat4(),
            );
            assert_eq!(reflection_uniforms[0], 1.0);
            assert_eq!(reflection_uniforms[1], 1.0);

            let key = terrain_core::TerrainNodeKey {
                lod: 0,
                coord: terrain_core::TerrainChunkCoord { x: 0, y: 0, z: 0 },
            };
            let packet = test_water_packet(0.0, 0.0, 32.0, 32.0, 4.0);
            assert!(resources
                .upsert_water_patch(&queue, key, &packet)
                .expect("valid node bathymetry should upload"));
            let coverage = resources
                .bathymetry_coverage()
                .expect("uploaded node bathymetry should expose coverage");
            assert_eq!(coverage.texel_count, WATER_NODE_BATHYMETRY_TEXEL_COUNT);
            assert_eq!(coverage.world_span_meters, 32.0);
            assert_eq!(coverage.center_x, 16.0);
            assert_eq!(coverage.center_z, 16.0);
            assert_eq!(coverage.patch_count, 1);

            let mut invalid_packet = packet.clone();
            invalid_packet.depths_meters[0] = f32::NAN;
            assert_eq!(
                resources.upsert_water_patch(&queue, key, &invalid_packet),
                Err(WaterBathymetryError::InvalidWaterPacket)
            );

            assert!(resources.remove_water_patch(key));
            assert!(resources.bathymetry_coverage().is_none());
            assert!(resources
                .upsert_water_patch(&queue, key, &packet)
                .expect("valid node bathymetry should reupload after removal"));

            resources.resize(&device, 63, 31);
            assert_eq!(resources.reflection_size(), (31, 15));
            let mut settings = enabled_settings;
            settings.enabled = false;
            let disabled_uniforms = resources.uniform_values(settings, 0.0, &identity_mat4());
            assert_eq!(disabled_uniforms[0], 0.0);
            assert_eq!(disabled_uniforms[1], 0.0);

            let output_color = create_target(
                &device,
                "water test output color",
                63,
                31,
                POST_PROCESS_COLOR_FORMAT,
            );
            let output_depth = create_target(
                &device,
                "water test output depth",
                63,
                31,
                POST_PROCESS_LINEAR_DEPTH_FORMAT,
            );
            let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("water test encoder"),
            });
            {
                let color_attachments = [
                    Some(wgpu::RenderPassColorAttachment {
                        view: resources.reflection_scene_color_view(),
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Clear(wgpu::Color {
                                r: 0.08,
                                g: 0.12,
                                b: 0.18,
                                a: 1.0,
                            }),
                            store: wgpu::StoreOp::Store,
                        },
                    }),
                    Some(wgpu::RenderPassColorAttachment {
                        view: resources.reflection_linear_depth_view(),
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Clear(wgpu::Color {
                                r: 24.0,
                                g: 0.0,
                                b: 0.0,
                                a: 0.0,
                            }),
                            store: wgpu::StoreOp::Store,
                        },
                    }),
                ];
                let depth_stencil_attachment = Some(wgpu::RenderPassDepthStencilAttachment {
                    view: resources.reflection_depth_view(),
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(1.0),
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                });
                let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("water test reflection clear"),
                    color_attachments: &color_attachments,
                    depth_stencil_attachment,
                    timestamp_writes: None,
                    occlusion_query_set: None,
                });
            }
            {
                let color_attachments = [
                    Some(wgpu::RenderPassColorAttachment {
                        view: resources.opaque_scene_color_view(),
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Clear(wgpu::Color {
                                r: 0.15,
                                g: 0.25,
                                b: 0.35,
                                a: 1.0,
                            }),
                            store: wgpu::StoreOp::Store,
                        },
                    }),
                    Some(wgpu::RenderPassColorAttachment {
                        view: resources.opaque_linear_depth_view(),
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Clear(wgpu::Color {
                                r: 12.0,
                                g: 0.0,
                                b: 0.0,
                                a: 0.0,
                            }),
                            store: wgpu::StoreOp::Store,
                        },
                    }),
                ];
                let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("water test opaque clear"),
                    color_attachments: &color_attachments,
                    depth_stencil_attachment: None,
                    timestamp_writes: None,
                    occlusion_query_set: None,
                });
            }
            resources.render(
                &queue,
                &mut encoder,
                &camera_bind_group,
                &output_color.view,
                &output_depth.view,
                settings,
                0.0,
                &identity_mat4(),
            );
            queue.submit(Some(encoder.finish()));
            device.poll(wgpu::Maintain::Wait);
        });
    }

    fn identity_mat4() -> [f32; 16] {
        [
            1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
        ]
    }

    fn test_water_packet(
        origin_x: f32,
        origin_z: f32,
        world_span_x: f32,
        world_span_z: f32,
        depth: f32,
    ) -> WaterNodePacket {
        let len = (WATER_NODE_BATHYMETRY_TEXEL_COUNT * WATER_NODE_BATHYMETRY_TEXEL_COUNT) as usize;
        WaterNodePacket {
            texel_count: WATER_NODE_BATHYMETRY_TEXEL_COUNT,
            origin_x,
            origin_z,
            world_span_x,
            world_span_z,
            sea_level_meters: 0.0,
            max_depth_meters: depth,
            depths_meters: vec![depth; len],
        }
    }
}
