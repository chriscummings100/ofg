// Rust/wgpu post-process frame graph helpers for browser rendering.
// This module owns HDR scene targets, post debug views, and the fullscreen pass
// that presents renderer-owned intermediate textures to the WebGPU surface.
#![cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]

use wgpu::util::DeviceExt;

pub(crate) const POST_PROCESS_COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba16Float;
pub(crate) const POST_PROCESS_LINEAR_DEPTH_FORMAT: wgpu::TextureFormat =
    wgpu::TextureFormat::R32Float;
const POST_PROCESS_SHADER_SOURCE: &str =
    include_str!("../../../src/engine/render/shaders/post.wgsl");
const POST_PROCESS_UNIFORM_FLOATS: usize = 12;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum PostProcessDebugView {
    Final,
    SceneColor,
    LinearDepth,
    PostToneMap,
    Bloom,
    DofCoc,
    DofBlurred,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct PostProcessSettings {
    debug_view: PostProcessDebugView,
    depth_debug_scale: f32,
    exposure: f32,
    tone_mapping_enabled: bool,
    bloom_enabled: bool,
    bloom_threshold: f32,
    bloom_intensity: f32,
    dof_enabled: bool,
    dof_focus_distance: f32,
    dof_focus_range: f32,
    dof_max_blur_pixels: f32,
}

pub(crate) struct PostProcessResources {
    scene_color: PostProcessTarget,
    linear_depth: PostProcessTarget,
    bind_group_layout: wgpu::BindGroupLayout,
    bind_group: wgpu::BindGroup,
    bloom: PostProcessTarget,
    bloom_empty_bind_group: wgpu::BindGroup,
    bloom_bind_group_layout: wgpu::BindGroupLayout,
    bloom_bind_group: wgpu::BindGroup,
    uniform_buffer: wgpu::Buffer,
    sampler: wgpu::Sampler,
    pipeline: wgpu::RenderPipeline,
    bloom_pipeline: wgpu::RenderPipeline,
}

struct PostProcessTarget {
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
}

impl PostProcessDebugView {
    /// Parses the browser/debug string for a post-process intermediate view.
    pub(crate) fn from_browser_name(name: &str) -> Option<Self> {
        match name {
            "final" => Some(Self::Final),
            "sceneColor" => Some(Self::SceneColor),
            "linearDepth" => Some(Self::LinearDepth),
            "postToneMap" => Some(Self::PostToneMap),
            "bloom" => Some(Self::Bloom),
            "dofCoc" => Some(Self::DofCoc),
            "dofBlurred" => Some(Self::DofBlurred),
            _ => None,
        }
    }

    /// Returns the stable browser/debug string for this view.
    pub(crate) fn browser_name(self) -> &'static str {
        match self {
            Self::Final => "final",
            Self::SceneColor => "sceneColor",
            Self::LinearDepth => "linearDepth",
            Self::PostToneMap => "postToneMap",
            Self::Bloom => "bloom",
            Self::DofCoc => "dofCoc",
            Self::DofBlurred => "dofBlurred",
        }
    }

    /// Returns the WGSL uniform code consumed by the post shader.
    fn shader_code(self) -> f32 {
        match self {
            Self::Final => 0.0,
            Self::SceneColor => 1.0,
            Self::LinearDepth => 2.0,
            Self::PostToneMap => 3.0,
            Self::Bloom => 4.0,
            Self::DofCoc => 5.0,
            Self::DofBlurred => 6.0,
        }
    }
}

impl PostProcessSettings {
    /// Creates default post settings that present the final scene image.
    pub(crate) fn new() -> Self {
        Self {
            debug_view: PostProcessDebugView::Final,
            depth_debug_scale: 0.02,
            exposure: 1.0,
            tone_mapping_enabled: true,
            bloom_enabled: true,
            bloom_threshold: 1.0,
            bloom_intensity: 0.08,
            dof_enabled: false,
            dof_focus_distance: 30.0,
            dof_focus_range: 8.0,
            dof_max_blur_pixels: 6.0,
        }
    }

    /// Returns the currently selected debug view.
    pub(crate) fn debug_view(&self) -> PostProcessDebugView {
        self.debug_view
    }

    /// Selects which post-process intermediate is presented to the surface.
    pub(crate) fn set_debug_view(&mut self, debug_view: PostProcessDebugView) {
        self.debug_view = debug_view;
    }

    /// Returns the current scene exposure multiplier before tone mapping.
    pub(crate) fn exposure(&self) -> f32 {
        self.exposure
    }

    /// Returns whether the final pass applies the filmic tone-map curve.
    pub(crate) fn tone_mapping_enabled(&self) -> bool {
        self.tone_mapping_enabled
    }

    /// Updates filmic tone-map enablement and exposure.
    pub(crate) fn set_tone_mapping(&mut self, enabled: bool, exposure: f32) {
        self.tone_mapping_enabled = enabled;
        self.exposure = exposure;
    }

    /// Returns whether bloom is composited into final post output.
    pub(crate) fn bloom_enabled(&self) -> bool {
        self.bloom_enabled
    }

    /// Returns the HDR threshold where bloom extraction starts.
    pub(crate) fn bloom_threshold(&self) -> f32 {
        self.bloom_threshold
    }

    /// Returns the bloom contribution multiplier used before tone mapping.
    pub(crate) fn bloom_intensity(&self) -> f32 {
        self.bloom_intensity
    }

    /// Updates bloom enablement, threshold, and intensity.
    pub(crate) fn set_bloom(&mut self, enabled: bool, threshold: f32, intensity: f32) {
        self.bloom_enabled = enabled;
        self.bloom_threshold = threshold;
        self.bloom_intensity = intensity;
    }

    /// Returns whether depth of field is composited into final output.
    pub(crate) fn dof_enabled(&self) -> bool {
        self.dof_enabled
    }

    /// Returns the camera-space distance that remains sharp for DoF.
    pub(crate) fn dof_focus_distance(&self) -> f32 {
        self.dof_focus_distance
    }

    /// Returns the symmetric focus range around the focus distance.
    pub(crate) fn dof_focus_range(&self) -> f32 {
        self.dof_focus_range
    }

    /// Returns the maximum fullscreen blur radius used by DoF.
    pub(crate) fn dof_max_blur_pixels(&self) -> f32 {
        self.dof_max_blur_pixels
    }

    /// Updates depth-of-field enablement and artist-friendly focus controls.
    pub(crate) fn set_depth_of_field(
        &mut self,
        enabled: bool,
        focus_distance: f32,
        focus_range: f32,
        max_blur_pixels: f32,
    ) {
        self.dof_enabled = enabled;
        self.dof_focus_distance = focus_distance;
        self.dof_focus_range = focus_range;
        self.dof_max_blur_pixels = max_blur_pixels;
    }

    /// Packs the post-process uniform block for GPU upload.
    fn uniform_values(self) -> [f32; POST_PROCESS_UNIFORM_FLOATS] {
        [
            self.debug_view.shader_code(),
            self.depth_debug_scale,
            self.exposure,
            if self.tone_mapping_enabled { 1.0 } else { 0.0 },
            if self.bloom_enabled { 1.0 } else { 0.0 },
            self.bloom_threshold,
            self.bloom_intensity,
            0.0,
            if self.dof_enabled { 1.0 } else { 0.0 },
            self.dof_focus_distance,
            self.dof_focus_range,
            self.dof_max_blur_pixels,
        ]
    }
}

impl Default for PostProcessSettings {
    fn default() -> Self {
        Self::new()
    }
}

impl PostProcessResources {
    /// Creates all resize-owned post-process targets and the present pipeline.
    pub(crate) fn new(
        device: &wgpu::Device,
        surface_format: wgpu::TextureFormat,
        width: u32,
        height: u32,
    ) -> Self {
        let scene_color = create_target(
            device,
            "post scene color",
            width,
            height,
            POST_PROCESS_COLOR_FORMAT,
        );
        let linear_depth = create_target(
            device,
            "post linear depth",
            width,
            height,
            POST_PROCESS_LINEAR_DEPTH_FORMAT,
        );
        let bloom = create_target(
            device,
            "post bloom level 0",
            bloom_width(width),
            bloom_height(height),
            POST_PROCESS_COLOR_FORMAT,
        );
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("post process sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("post process uniforms"),
            contents: f32_as_bytes(&PostProcessSettings::default().uniform_values()),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("post process bind group layout"),
            entries: &[
                texture_binding(0, wgpu::TextureSampleType::Float { filterable: true }),
                texture_binding(1, wgpu::TextureSampleType::Float { filterable: false }),
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 3,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                texture_binding(4, wgpu::TextureSampleType::Float { filterable: true }),
            ],
        });
        let bloom_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("post bloom bind group layout"),
                entries: &[
                    texture_binding(0, wgpu::TextureSampleType::Float { filterable: true }),
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::FRAGMENT,
                        ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 2,
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
        let bloom_empty_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("post bloom unused group 0 layout"),
                entries: &[],
            });
        let bloom_empty_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("post bloom unused group 0 bind group"),
            layout: &bloom_empty_bind_group_layout,
            entries: &[],
        });
        let bind_group = create_bind_group(
            device,
            &bind_group_layout,
            &scene_color.view,
            &linear_depth.view,
            &bloom.view,
            &sampler,
            &uniform_buffer,
        );
        let bloom_bind_group = create_bloom_bind_group(
            device,
            &bloom_bind_group_layout,
            &scene_color.view,
            &sampler,
            &uniform_buffer,
        );
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("post process shader"),
            source: wgpu::ShaderSource::Wgsl(POST_PROCESS_SHADER_SOURCE.into()),
        });
        let pipeline = create_pipeline(device, &bind_group_layout, &shader, surface_format);
        let bloom_pipeline = create_bloom_pipeline(
            device,
            &bloom_empty_bind_group_layout,
            &bloom_bind_group_layout,
            &shader,
        );

        Self {
            scene_color,
            linear_depth,
            bind_group_layout,
            bind_group,
            bloom,
            bloom_empty_bind_group,
            bloom_bind_group_layout,
            bloom_bind_group,
            uniform_buffer,
            sampler,
            pipeline,
            bloom_pipeline,
        }
    }

    /// Recreates render targets and bind groups for a new viewport size.
    pub(crate) fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        self.scene_color = create_target(
            device,
            "post scene color",
            width,
            height,
            POST_PROCESS_COLOR_FORMAT,
        );
        self.linear_depth = create_target(
            device,
            "post linear depth",
            width,
            height,
            POST_PROCESS_LINEAR_DEPTH_FORMAT,
        );
        self.bloom = create_target(
            device,
            "post bloom level 0",
            bloom_width(width),
            bloom_height(height),
            POST_PROCESS_COLOR_FORMAT,
        );
        self.bind_group = create_bind_group(
            device,
            &self.bind_group_layout,
            &self.scene_color.view,
            &self.linear_depth.view,
            &self.bloom.view,
            &self.sampler,
            &self.uniform_buffer,
        );
        self.bloom_bind_group = create_bloom_bind_group(
            device,
            &self.bloom_bind_group_layout,
            &self.scene_color.view,
            &self.sampler,
            &self.uniform_buffer,
        );
    }

    /// Returns the HDR color target view for the main scene render pass.
    pub(crate) fn scene_color_view(&self) -> &wgpu::TextureView {
        &self.scene_color.view
    }

    /// Returns the linear-depth color target view for the main scene pass.
    pub(crate) fn linear_depth_view(&self) -> &wgpu::TextureView {
        &self.linear_depth.view
    }

    /// Presents the selected post-process view into the WebGPU surface view.
    pub(crate) fn render(
        &self,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        surface_view: &wgpu::TextureView,
        settings: PostProcessSettings,
    ) {
        queue.write_buffer(
            &self.uniform_buffer,
            0,
            f32_as_bytes(&settings.uniform_values()),
        );
        self.render_bloom(encoder);

        let color_attachments = [Some(wgpu::RenderPassColorAttachment {
            view: surface_view,
            resolve_target: None,
            ops: wgpu::Operations {
                load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                store: wgpu::StoreOp::Store,
            },
        })];
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("post process present pass"),
            color_attachments: &color_attachments,
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &self.bind_group, &[]);
        pass.draw(0..3, 0..1);
    }

    fn render_bloom(&self, encoder: &mut wgpu::CommandEncoder) {
        let color_attachments = [Some(wgpu::RenderPassColorAttachment {
            view: &self.bloom.view,
            resolve_target: None,
            ops: wgpu::Operations {
                load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                store: wgpu::StoreOp::Store,
            },
        })];
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("post bloom pass"),
            color_attachments: &color_attachments,
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.bloom_pipeline);
        pass.set_bind_group(0, &self.bloom_empty_bind_group, &[]);
        pass.set_bind_group(1, &self.bloom_bind_group, &[]);
        pass.draw(0..3, 0..1);
    }
}

fn create_target(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
    format: wgpu::TextureFormat,
) -> PostProcessTarget {
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
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
        view_formats: &[],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());

    PostProcessTarget {
        _texture: texture,
        view,
    }
}

fn create_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    scene_color: &wgpu::TextureView,
    linear_depth: &wgpu::TextureView,
    bloom: &wgpu::TextureView,
    sampler: &wgpu::Sampler,
    uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("post process bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(scene_color),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::TextureView(linear_depth),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::Sampler(sampler),
            },
            wgpu::BindGroupEntry {
                binding: 3,
                resource: uniform_buffer.as_entire_binding(),
            },
            wgpu::BindGroupEntry {
                binding: 4,
                resource: wgpu::BindingResource::TextureView(bloom),
            },
        ],
    })
}

fn create_bloom_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    scene_color: &wgpu::TextureView,
    sampler: &wgpu::Sampler,
    uniform_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("post bloom bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(scene_color),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::Sampler(sampler),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: uniform_buffer.as_entire_binding(),
            },
        ],
    })
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

fn create_pipeline(
    device: &wgpu::Device,
    bind_group_layout: &wgpu::BindGroupLayout,
    shader: &wgpu::ShaderModule,
    surface_format: wgpu::TextureFormat,
) -> wgpu::RenderPipeline {
    let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("post process pipeline layout"),
        bind_group_layouts: &[bind_group_layout],
        push_constant_ranges: &[],
    });

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("post process pipeline"),
        layout: Some(&layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "postVertexMain",
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "postFragmentMain",
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: surface_format,
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

fn create_bloom_pipeline(
    device: &wgpu::Device,
    empty_bind_group_layout: &wgpu::BindGroupLayout,
    bind_group_layout: &wgpu::BindGroupLayout,
    shader: &wgpu::ShaderModule,
) -> wgpu::RenderPipeline {
    let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("post bloom pipeline layout"),
        bind_group_layouts: &[empty_bind_group_layout, bind_group_layout],
        push_constant_ranges: &[],
    });

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("post bloom pipeline"),
        layout: Some(&layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "postVertexMain",
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "bloomFragmentMain",
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: POST_PROCESS_COLOR_FORMAT,
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

fn bloom_width(width: u32) -> u32 {
    (width / 2).max(1)
}

fn bloom_height(height: u32) -> u32 {
    (height / 2).max(1)
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

    #[test]
    fn debug_views_round_trip_browser_names_and_shader_codes() {
        let cases = [
            ("final", PostProcessDebugView::Final, 0.0),
            ("sceneColor", PostProcessDebugView::SceneColor, 1.0),
            ("linearDepth", PostProcessDebugView::LinearDepth, 2.0),
            ("postToneMap", PostProcessDebugView::PostToneMap, 3.0),
            ("bloom", PostProcessDebugView::Bloom, 4.0),
            ("dofCoc", PostProcessDebugView::DofCoc, 5.0),
            ("dofBlurred", PostProcessDebugView::DofBlurred, 6.0),
        ];

        for (name, view, code) in cases {
            assert_eq!(PostProcessDebugView::from_browser_name(name), Some(view));
            assert_eq!(view.browser_name(), name);
            assert_eq!(view.shader_code(), code);
        }
        assert_eq!(PostProcessDebugView::from_browser_name("unknown"), None);
    }

    #[test]
    fn default_settings_present_final_view_with_depth_scale() {
        let settings = PostProcessSettings::default();

        assert_eq!(settings.debug_view(), PostProcessDebugView::Final);
        assert_eq!(settings.exposure(), 1.0);
        assert!(settings.tone_mapping_enabled());
        assert!(settings.bloom_enabled());
        assert_eq!(settings.bloom_threshold(), 1.0);
        assert_eq!(settings.bloom_intensity(), 0.08);
        assert!(!settings.dof_enabled());
        assert_eq!(settings.dof_focus_distance(), 30.0);
        assert_eq!(settings.dof_focus_range(), 8.0);
        assert_eq!(settings.dof_max_blur_pixels(), 6.0);
        assert_eq!(
            settings.uniform_values(),
            [0.0, 0.02, 1.0, 1.0, 1.0, 1.0, 0.08, 0.0, 0.0, 30.0, 8.0, 6.0]
        );
    }

    #[test]
    fn settings_pack_selected_debug_view_and_tone_mapping_for_the_shader() {
        let mut settings = PostProcessSettings::default();
        settings.set_debug_view(PostProcessDebugView::PostToneMap);
        settings.set_tone_mapping(false, 1.75);
        settings.set_bloom(false, 0.8, 0.35);
        settings.set_depth_of_field(true, 18.0, 4.0, 12.0);

        assert_eq!(settings.debug_view(), PostProcessDebugView::PostToneMap);
        assert!(!settings.tone_mapping_enabled());
        assert_eq!(settings.exposure(), 1.75);
        assert!(!settings.bloom_enabled());
        assert_eq!(settings.bloom_threshold(), 0.8);
        assert_eq!(settings.bloom_intensity(), 0.35);
        assert!(settings.dof_enabled());
        assert_eq!(settings.dof_focus_distance(), 18.0);
        assert_eq!(settings.dof_focus_range(), 4.0);
        assert_eq!(settings.dof_max_blur_pixels(), 12.0);
        assert_eq!(
            settings.uniform_values(),
            [3.0, 0.02, 1.75, 0.0, 0.0, 0.8, 0.35, 0.0, 1.0, 18.0, 4.0, 12.0]
        );
    }

    #[test]
    fn resources_render_debug_views_to_native_offscreen_targets() {
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
                eprintln!("Skipping post-process GPU test because no native adapter is available.");
                return;
            };
            let limits =
                wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
            let Ok((device, queue)) = adapter
                .request_device(
                    &wgpu::DeviceDescriptor {
                        label: Some("post process test device"),
                        required_features: wgpu::Features::empty(),
                        required_limits: limits,
                    },
                    None,
                )
                .await
            else {
                eprintln!("Skipping post-process GPU test because no native device is available.");
                return;
            };

            let mut resources =
                PostProcessResources::new(&device, wgpu::TextureFormat::Rgba8Unorm, 64, 32);
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::Final,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::SceneColor,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::LinearDepth,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::PostToneMap,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::Bloom,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::DofCoc,
                64,
                32,
            );
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::DofBlurred,
                64,
                32,
            );

            resources.resize(&device, 32, 16);
            render_debug_view(
                &device,
                &queue,
                &resources,
                PostProcessDebugView::LinearDepth,
                32,
                16,
            );
        });
    }

    fn render_debug_view(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        resources: &PostProcessResources,
        debug_view: PostProcessDebugView,
        width: u32,
        height: u32,
    ) {
        let target = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("post process test output"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let target_view = target.create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("post process test encoder"),
        });
        {
            let color_attachments = [
                Some(wgpu::RenderPassColorAttachment {
                    view: resources.scene_color_view(),
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color {
                            r: 0.4,
                            g: 0.2,
                            b: 0.1,
                            a: 1.0,
                        }),
                        store: wgpu::StoreOp::Store,
                    },
                }),
                Some(wgpu::RenderPassColorAttachment {
                    view: resources.linear_depth_view(),
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
            let _pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("post process test scene clear"),
                color_attachments: &color_attachments,
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
        }
        let mut settings = PostProcessSettings::default();
        settings.set_debug_view(debug_view);
        resources.render(queue, &mut encoder, &target_view, settings);
        queue.submit(Some(encoder.finish()));
        device.poll(wgpu::Maintain::Wait);
    }
}
