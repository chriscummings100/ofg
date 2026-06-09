// Browser-facing Rust/wgpu game facade. It owns WebGPU resources, terrain draw
// submission, and render-facing GLTF model resources for the playable browser path.
use std::borrow::Cow;
use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::rc::Rc;
#[cfg(not(target_arch = "wasm32"))]
use std::sync::OnceLock;
#[cfg(not(target_arch = "wasm32"))]
use std::time::Instant;

use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use wgpu::util::DeviceExt;

use crate::config::{
    MODEL_VERTEX_FLOATS, REQUIRED_TEXTURE_ARRAY_LAYERS, SHADOW_CASCADE_COUNT, SHADOW_MAP_SIZE,
    SHADOW_MAX_DISTANCE, TERRAIN_VERTEX_FLOATS, TEXTURE_FORMAT_RGBA8_UNORM,
};
use crate::game_state::{BrowserGameInput, BrowserGameState};
use crate::materials::TERRAIN_MATERIAL_PACKET;
use crate::model_asset_loader::load_model_asset_bytes;
use crate::model_assets::{
    import_gltf_model_from_slice, model_primitive_vertex_floats, ModelAsset,
    PLAYER_QUATERNIUS_UAL1_MODEL_ID, PLAYER_QUATERNIUS_UAL1_MODEL_URL,
    SAMPLE_SPECULAR_GLOSSINESS_MATERIAL_LABEL, SAMPLE_SPECULAR_GLOSSINESS_MESH_LABEL,
    SAMPLE_SPECULAR_GLOSSINESS_MODEL_ID, SAMPLE_SPECULAR_GLOSSINESS_MODEL_URL,
};
use crate::model_locomotion::{PlayerCharacterLocomotionTuning, PlayerCharacterModel};
use crate::model_materials::{ModelMaterial, ModelMaterialWorkflow, ModelTextureInfo};
use crate::model_render_assets::model_material_packet;
use crate::model_texture_assets::decode_model_texture;
use crate::perf::{
    terrain_lod_from_node_key, FramePerfReport, FramePerfRing, FramePerfSample, GpuPassTimings,
    GpuTimedPass, GpuTimerStatus, GpuTimestampPair, RenderCounterSample, RenderCounterSummary,
    RenderDebugOptions, RenderDebugOptionsError, RenderDebugOptionsUpdate, RenderMaterialDebugMode,
    RustCpuFrameTimings, ShadowCascadeCounter, TerrainLodCounter,
};
use crate::player_character::{
    PlayerCharacterDescriptor, PlayerCharacterId, PLAYER_CHARACTER_DESCRIPTORS,
};
use crate::post_process::{
    PostProcessDebugView, PostProcessResources, PostProcessSettings, POST_PROCESS_COLOR_FORMAT,
    POST_PROCESS_LINEAR_DEPTH_FORMAT,
};
use crate::render_math::{
    aabb_from_vertex_positions, frustum_from_view_projection, frustum_intersects_aabb,
    transform_aabb, Aabb, RenderVec3,
};
use crate::render_packets::{
    build_frame_packet_from_engine_snapshot, ENGINE_RENDER_SNAPSHOT_FLOATS,
};
use crate::render_uniforms::{
    build_frame_uniform_values, build_object_uniform_values, build_shadow_uniform_values,
    FRAME_PACKET_FLOATS, FRAME_UNIFORM_FLOATS, FRAME_UNIFORM_SKY_CLOUD_COVERAGE_OFFSET,
    MATERIAL_PACKET_FLOATS, OBJECT_UNIFORM_FLOATS, SHADOW_DEBUG_MODE_OFFSET,
    SHADOW_STRENGTH_OFFSET, WORLD_MATRIX_FLOATS,
};
use crate::resources::{ResourceHandle, ResourceStore};
use crate::shadow_renderer::{
    create_shadow_pipelines, create_shadow_resources, ShadowPipelines, ShadowResources,
};
use crate::shadows::{
    build_shadow_cascades_with_max_distance, clamp_shadow_light_direction,
    shadow_caster_intersects_cascade, shadow_strength_for_sun_elevation, shadow_sun_mode_direction,
    ShadowCascadeSet, ShadowSunMode,
};
use crate::terrain_stream::{
    pop_ready_terrain_removal, BrowserTerrainBuildCompletion, BrowserTerrainBuildRequest,
    BrowserTerrainMeshUpdate, BrowserTerrainStream, BrowserTerrainStreamStatus, TerrainJobStats,
    MAX_SAFE_TERRAIN_WORKER_REQUEST_ID,
};
use crate::terrain_textures::{load_terrain_texture_arrays, TerrainTextureArrays};
use crate::texture_mips::build_rgba8_mip_chain;
use crate::ENGINE_WEB_VERSION;
use engine_core::{PlayerConfig, PlayerMode, Vec3};
use terrain_core::{terrain_node_key, TerrainChunkCoord, TerrainNodeKey, DEFAULT_TERRAIN_PRESET};

const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth24Plus;
const SHADER_SOURCE: &str = include_str!("../../../src/engine/render/shaders/uber.wgsl");
const SHADOW_CONSTANT_BIAS: f32 = 0.0015;
const SHADOW_NORMAL_BIAS: f32 = 0.0;
const SHADOW_DEBUG_MATERIAL_MODE_OFFSET: usize = SHADOW_DEBUG_MODE_OFFSET + 1;
const SHADOW_DEBUG_WHITE_TEXTURES_OFFSET: usize = SHADOW_DEBUG_MODE_OFFSET + 2;
const GPU_TIMESTAMP_QUERY_COUNT: u32 = 16;
const TERRAIN_UPLOAD_MAX_MESHES_PER_FRAME: u32 = 2;
const TERRAIN_UPLOAD_MAX_VERTEX_FLOATS_PER_FRAME: u32 = 350_000;
const TERRAIN_REMOVAL_MAX_MESHES_PER_FRAME: u32 = 4;

#[wasm_bindgen]
pub struct RustBrowserGame {
    game_state: BrowserGameState,
    terrain_stream: BrowserTerrainStream,
    renderer: BrowserWgpuRenderer,
    terrain_mesh_handles_by_key: HashMap<String, ResourceHandle>,
    terrain_textures: Option<TerrainTextureHandles>,
    object_handles_by_id: HashMap<String, ResourceHandle>,
    scene_mesh_handles_by_label: HashMap<String, ResourceHandle>,
    scene_material_resources_by_label: HashMap<String, SceneMaterialResource>,
    pending_terrain_uploads: VecDeque<BrowserTerrainMeshUpdate>,
    pending_terrain_removals: VecDeque<TerrainNodeKey>,
    player_characters: Vec<PlayerCharacterSlot>,
    active_player_character_id: PlayerCharacterId,
    model_skinning_runtime: Option<&'static str>,
    last_terrain_update_stats: TerrainUpdateStats,
    last_terrain_completion_stats: TerrainCompletionStats,
    last_terrain_request_stats: TerrainRequestStats,
    perf_history: FramePerfRing,
}

#[derive(Debug)]
struct RustBrowserGameStatus {
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
    frame_visible_draw_count: u32,
    frame_shadow_draw_count: u32,
    terrain_update_total_ms: f64,
    terrain_completion_ingest_ms: f64,
    terrain_worker_request_drain_ms: f64,
    terrain_stream_tick_ms: f64,
    terrain_stream_sync_ms: f64,
    terrain_stream_scheduler_ms: f64,
    terrain_stream_worker_queue_ms: f64,
    terrain_stream_visibility_ms: f64,
    terrain_stream_visibility_select_ms: f64,
    terrain_stream_visibility_status_ms: f64,
    terrain_stream_visibility_apply_ms: f64,
    terrain_mesh_destroy_ms: f64,
    terrain_mesh_upload_ms: f64,
    terrain_completion_count: u32,
    terrain_completion_accepted_count: u32,
    terrain_completion_vertex_float_count: u32,
    terrain_completion_index_count: u32,
    terrain_worker_request_count: u32,
    terrain_update_upserted_mesh_count: u32,
    terrain_update_removed_mesh_count: u32,
    terrain_update_uploaded_vertex_float_count: u32,
    terrain_update_uploaded_index_count: u32,
    terrain_update_deferred_upload_count: u32,
    terrain_update_deferred_removal_count: u32,
    terrain_update_upload_budget_hit: bool,
    terrain_update_removal_budget_hit: bool,
    shadow_cascade_count: u32,
    shadow_map_size: u32,
    shadow_max_distance_meters: f32,
    shadow_strength: f32,
    shadow_effective_sun_elevation: f32,
    shadow_effective_sun_direction: RenderVec3,
    post_process_debug_view: PostProcessDebugView,
    post_process_exposure: f32,
    post_process_tone_mapping_enabled: bool,
    post_process_bloom_enabled: bool,
    post_process_bloom_threshold: f32,
    post_process_bloom_intensity: f32,
    post_process_dof_enabled: bool,
    post_process_dof_focus_distance: f32,
    post_process_dof_focus_range: f32,
    post_process_dof_max_blur_pixels: f32,
    gpu_timer_status: GpuTimerStatus,
    render_debug_options: RenderDebugOptions,
    last_render_counters: RenderCounterSample,
    last_gpu_pass_timings: GpuPassTimings,
}

#[derive(Clone, Copy, Debug, Default)]
struct TerrainUpdateStats {
    total_ms: f64,
    stream_tick_ms: f64,
    stream_sync_ms: f64,
    stream_scheduler_ms: f64,
    stream_worker_queue_ms: f64,
    stream_visibility_ms: f64,
    stream_visibility_select_ms: f64,
    stream_visibility_status_ms: f64,
    stream_visibility_apply_ms: f64,
    mesh_destroy_ms: f64,
    mesh_upload_ms: f64,
    upserted_mesh_count: u32,
    removed_mesh_count: u32,
    uploaded_vertex_float_count: u32,
    uploaded_index_count: u32,
    deferred_upload_count: u32,
    deferred_removal_count: u32,
    upload_budget_hit: bool,
    removal_budget_hit: bool,
}

#[derive(Clone, Copy, Debug, Default)]
struct TerrainCompletionStats {
    total_ms: f64,
    completion_count: u32,
    accepted_count: u32,
    vertex_float_count: u32,
    index_count: u32,
}

#[derive(Clone, Copy, Debug, Default)]
struct TerrainRequestStats {
    total_ms: f64,
    request_count: u32,
}

#[derive(Clone, Copy, Debug, Default)]
struct RenderFrameCpuTimings {
    render_packet_build_ms: f64,
    renderer_prepare_ms: f64,
    renderer_shadow_cpu_ms: f64,
    renderer_scene_cpu_ms: f64,
    renderer_post_cpu_ms: f64,
    renderer_submit_ms: f64,
}

#[derive(Clone, Debug)]
struct RenderFrameResult {
    cpu_timings: RenderFrameCpuTimings,
    counters: RenderCounterSample,
    gpu_pass_timings: GpuPassTimings,
}

#[derive(Clone, Copy, Debug)]
struct ShadowRuntimeState {
    light_direction: RenderVec3,
    cascade_light_direction: RenderVec3,
    sun_elevation: f32,
    strength: f32,
    max_distance_meters: f32,
}

impl Default for ShadowRuntimeState {
    fn default() -> Self {
        Self {
            light_direction: RenderVec3::UP,
            cascade_light_direction: RenderVec3::UP,
            sun_elevation: 1.0,
            strength: 1.0,
            max_distance_meters: SHADOW_MAX_DISTANCE,
        }
    }
}

struct BrowserWgpuRenderer {
    canvas: web_sys::HtmlCanvasElement,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    depth_texture: wgpu::Texture,
    shadow_resources: ShadowResources,
    camera_uniform_buffer: wgpu::Buffer,
    camera_bind_group: wgpu::BindGroup,
    object_bind_group_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    sky_pipeline: wgpu::RenderPipeline,
    pipeline: wgpu::RenderPipeline,
    model_pipeline: wgpu::RenderPipeline,
    shadow_pipelines: ShadowPipelines,
    shadow_debug_view: ShadowDebugView,
    post_process: PostProcessResources,
    post_process_settings: PostProcessSettings,
    max_texture_array_layers: u32,
    meshes: ResourceStore<GpuMesh>,
    textures: ResourceStore<GpuTexture>,
    objects: ResourceStore<GpuObject>,
    fallback_albedo: ResourceHandle,
    fallback_normal: ResourceHandle,
    fallback_material: ResourceHandle,
    frame_index: u32,
    frame_draw_count: u32,
    frame_visible_draw_count: u32,
    frame_shadow_draw_count: u32,
    render_debug_options: RenderDebugOptions,
    last_shadow_runtime: ShadowRuntimeState,
    last_render_counters: RenderCounterSample,
    last_gpu_pass_timings: GpuPassTimings,
    gpu_timer_status: GpuTimerStatus,
    gpu_timers: Option<GpuTimerResources>,
}

struct GpuTimerResources {
    query_set: wgpu::QuerySet,
    resolve_buffer: wgpu::Buffer,
    timestamp_period_ns: f64,
    pending_readbacks: Vec<PendingGpuReadback>,
    latest_timings: GpuPassTimings,
}

struct PendingGpuReadback {
    buffer: wgpu::Buffer,
    query_count: u32,
    pairs: Vec<GpuTimestampPair>,
    completion: Rc<RefCell<Option<Result<(), String>>>>,
}

#[derive(Default)]
struct GpuFrameTimestampPlan {
    enabled: bool,
    next_query_index: u32,
    pairs: Vec<GpuTimestampPair>,
}

impl GpuFrameTimestampPlan {
    fn new(enabled: bool) -> Self {
        Self {
            enabled,
            next_query_index: 0,
            pairs: Vec::new(),
        }
    }

    fn reserve_pass(&mut self, pass: GpuTimedPass) -> Option<(u32, u32)> {
        if !self.enabled || self.next_query_index + 1 >= GPU_TIMESTAMP_QUERY_COUNT {
            return None;
        }

        let start_index = self.next_query_index;
        let end_index = self.next_query_index + 1;
        self.next_query_index += 2;
        self.pairs.push(GpuTimestampPair {
            pass,
            start_index,
            end_index,
        });
        Some((start_index, end_index))
    }
}

struct GpuMesh {
    vertex_buffer: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    index_count: u32,
    vertex_float_count: usize,
    vertex_layout: MeshVertexLayout,
    local_bounds: Aabb,
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

#[derive(Clone, Copy)]
struct PreparedRenderItem {
    mesh_handle: ResourceHandle,
    object_handle: ResourceHandle,
    world_bounds: Aabb,
    terrain_lod: Option<u8>,
}

struct PlayerCharacterSlot {
    descriptor: PlayerCharacterDescriptor,
    model: PlayerCharacterModel,
    mesh_handles: Vec<ResourceHandle>,
    scene_parts: Vec<(String, String)>,
    material_count: usize,
    texture_count: usize,
    non_fallback_albedo_part_count: usize,
}

#[derive(Clone, Copy)]
struct TerrainTextureHandles {
    albedo: ResourceHandle,
    normal: ResourceHandle,
    material: ResourceHandle,
}

#[derive(Clone, Copy)]
struct SceneMaterialResource {
    packet: [f32; MATERIAL_PACKET_FLOATS],
    albedo_texture: ResourceHandle,
    normal_texture: ResourceHandle,
    material_texture: ResourceHandle,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MeshVertexLayout {
    Terrain,
    Model,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ShadowDebugView {
    Off,
    CascadeIndex,
    ShadowVisibility,
    ShadowDepthCascade0,
    ShadowDepthCascade1,
    ShadowDepthCascade2,
    ShadowDepthCascade3,
}

impl MeshVertexLayout {
    fn from_floats_per_vertex(floats_per_vertex: u32) -> Option<Self> {
        match floats_per_vertex {
            TERRAIN_VERTEX_FLOATS => Some(Self::Terrain),
            MODEL_VERTEX_FLOATS => Some(Self::Model),
            _ => None,
        }
    }
}

impl GpuMesh {
    fn vertex_count(&self) -> usize {
        self.vertex_float_count
            / match self.vertex_layout {
                MeshVertexLayout::Terrain => TERRAIN_VERTEX_FLOATS as usize,
                MeshVertexLayout::Model => MODEL_VERTEX_FLOATS as usize,
            }
    }
}

impl ShadowDebugView {
    fn from_js_name(name: &str) -> Option<Self> {
        match name {
            "off" => Some(Self::Off),
            "cascadeIndex" => Some(Self::CascadeIndex),
            "shadowVisibility" => Some(Self::ShadowVisibility),
            "shadowDepthCascade0" => Some(Self::ShadowDepthCascade0),
            "shadowDepthCascade1" => Some(Self::ShadowDepthCascade1),
            "shadowDepthCascade2" => Some(Self::ShadowDepthCascade2),
            "shadowDepthCascade3" => Some(Self::ShadowDepthCascade3),
            _ => None,
        }
    }

    fn js_name(self) -> &'static str {
        match self {
            Self::Off => "off",
            Self::CascadeIndex => "cascadeIndex",
            Self::ShadowVisibility => "shadowVisibility",
            Self::ShadowDepthCascade0 => "shadowDepthCascade0",
            Self::ShadowDepthCascade1 => "shadowDepthCascade1",
            Self::ShadowDepthCascade2 => "shadowDepthCascade2",
            Self::ShadowDepthCascade3 => "shadowDepthCascade3",
        }
    }

    fn uniform_code(self) -> f32 {
        match self {
            Self::Off => 0.0,
            Self::CascadeIndex => 1.0,
            Self::ShadowVisibility => 2.0,
            Self::ShadowDepthCascade0 => 3.0,
            Self::ShadowDepthCascade1 => 4.0,
            Self::ShadowDepthCascade2 => 5.0,
            Self::ShadowDepthCascade3 => 6.0,
        }
    }
}

const IDENTITY_WORLD_MATRIX: [f32; WORLD_MATRIX_FLOATS] = [
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
];
const PLAYER_CHARACTER_SCENE_SCALE: f32 = 1.0;
const PLAYER_CHARACTER_HEIGHT_OFFSET: f32 = 0.0;
const SPECULAR_GLOSSINESS_FIXTURE_SCALE: f32 = 4.0;
const SPECULAR_GLOSSINESS_FIXTURE_HEIGHT_OFFSET: f32 = 0.08;

#[wasm_bindgen]
impl RustBrowserGame {
    #[wasm_bindgen(js_name = create)]
    pub async fn create(
        canvas: web_sys::HtmlCanvasElement,
        asset_loader: JsValue,
    ) -> Result<RustBrowserGame, JsValue> {
        console_error_panic_hook::set_once();
        let mut renderer = BrowserWgpuRenderer::new(canvas).await?;
        let terrain_texture_arrays = load_terrain_texture_arrays(&asset_loader).await?;
        let animation_model_bytes = load_model_asset_bytes(
            &asset_loader,
            PLAYER_QUATERNIUS_UAL1_MODEL_ID,
            PLAYER_QUATERNIUS_UAL1_MODEL_URL,
        )
        .await?;
        let animation_model =
            import_gltf_model_from_slice(&animation_model_bytes).map_err(js_error)?;

        let mut scene_mesh_handles_by_label = HashMap::new();
        let mut scene_material_resources_by_label = HashMap::new();
        let mut player_characters = Vec::with_capacity(PLAYER_CHARACTER_DESCRIPTORS.len());
        for descriptor in PLAYER_CHARACTER_DESCRIPTORS {
            let body_model_bytes =
                load_model_asset_bytes(&asset_loader, descriptor.model_id, descriptor.model_url)
                    .await?;
            let body_model = import_gltf_model_from_slice(&body_model_bytes).map_err(js_error)?;
            let player_character = PlayerCharacterModel::from_body_and_animation_models(
                body_model.clone(),
                &animation_model,
            )
            .map_err(js_error)?;
            let part_vertices = player_character.current_part_vertices().map_err(js_error)?;
            let mut model_texture_handles_by_index = HashMap::new();
            let mut mesh_handles = Vec::with_capacity(player_character.part_count());
            let mut scene_parts = Vec::with_capacity(player_character.part_count());
            let mut non_fallback_albedo_part_count = 0;
            for (part_index, vertices) in part_vertices.iter().enumerate() {
                let mesh_label = player_character_part_mesh_label(descriptor, part_index);
                let material_index = player_character.part_material_index(part_index);
                let material_label =
                    player_character_part_material_label(descriptor, part_index, material_index);
                let mesh_handle = renderer.register_mesh(
                    vertices,
                    player_character.part_indices(part_index),
                    MODEL_VERTEX_FLOATS,
                )?;
                let material_resource = model_scene_material_resource(
                    &mut renderer,
                    &body_model,
                    material_index,
                    player_character.part_material_packet(part_index),
                    &mut model_texture_handles_by_index,
                )?;
                if material_resource.albedo_texture != renderer.fallback_albedo {
                    non_fallback_albedo_part_count += 1;
                }

                scene_mesh_handles_by_label.insert(mesh_label.clone(), mesh_handle);
                scene_material_resources_by_label.insert(material_label.clone(), material_resource);
                mesh_handles.push(mesh_handle);
                scene_parts.push((mesh_label, material_label));
            }
            player_characters.push(PlayerCharacterSlot {
                descriptor,
                model: player_character,
                mesh_handles,
                scene_parts,
                material_count: body_model.material_count(),
                texture_count: body_model.texture_count(),
                non_fallback_albedo_part_count,
            });
        }
        let specular_glossiness_model_bytes = load_model_asset_bytes(
            &asset_loader,
            SAMPLE_SPECULAR_GLOSSINESS_MODEL_ID,
            SAMPLE_SPECULAR_GLOSSINESS_MODEL_URL,
        )
        .await?;
        let specular_glossiness_model =
            import_gltf_model_from_slice(&specular_glossiness_model_bytes).map_err(js_error)?;
        register_static_model_scene_item(
            &mut renderer,
            &mut scene_mesh_handles_by_label,
            &mut scene_material_resources_by_label,
            &specular_glossiness_model,
            SAMPLE_SPECULAR_GLOSSINESS_MESH_LABEL,
            SAMPLE_SPECULAR_GLOSSINESS_MATERIAL_LABEL,
        )?;

        let mut game_state = BrowserGameState::new();
        let active_player_character_id = PlayerCharacterId::Male;
        let active_player_character = player_characters
            .iter()
            .find(|slot| slot.descriptor.id == active_player_character_id)
            .ok_or_else(|| {
                js_error(format!(
                    "Rust browser game cannot resolve initial player character '{active_player_character_id}'."
                ))
            })?;
        game_state
            .configure_player_character_scene_parts(
                active_player_character.scene_parts.clone(),
                PLAYER_CHARACTER_SCENE_SCALE,
                PLAYER_CHARACTER_HEIGHT_OFFSET,
            )
            .map_err(js_error)?;
        game_state
            .configure_scaled_static_model_scene(
                SAMPLE_SPECULAR_GLOSSINESS_MESH_LABEL,
                SAMPLE_SPECULAR_GLOSSINESS_MATERIAL_LABEL,
                SPECULAR_GLOSSINESS_FIXTURE_SCALE,
                SPECULAR_GLOSSINESS_FIXTURE_HEIGHT_OFFSET,
            )
            .map_err(js_error)?;

        let mut game = Self {
            game_state,
            terrain_stream: BrowserTerrainStream::new(0, DEFAULT_TERRAIN_PRESET)
                .map_err(js_error)?,
            renderer,
            terrain_mesh_handles_by_key: HashMap::new(),
            terrain_textures: None,
            object_handles_by_id: HashMap::new(),
            scene_mesh_handles_by_label,
            scene_material_resources_by_label,
            pending_terrain_uploads: VecDeque::new(),
            pending_terrain_removals: VecDeque::new(),
            player_characters,
            active_player_character_id,
            model_skinning_runtime: Some("rust-cpu"),
            last_terrain_update_stats: TerrainUpdateStats::default(),
            last_terrain_completion_stats: TerrainCompletionStats::default(),
            last_terrain_request_stats: TerrainRequestStats::default(),
            perf_history: FramePerfRing::default(),
        };
        game.install_terrain_textures(terrain_texture_arrays)?;

        Ok(game)
    }

    #[wasm_bindgen(js_name = resize)]
    pub fn resize(&mut self, viewport: JsValue) -> Result<(), JsValue> {
        let width = js_required_u32(&viewport, "width", "viewport.width")?;
        let height = js_required_u32(&viewport, "height", "viewport.height")?;
        self.renderer.resize(width, height)
    }

    #[wasm_bindgen(js_name = tick)]
    pub fn tick(&mut self, frame: JsValue) -> Result<(), JsValue> {
        let frame_started_at_ms = perf_now_ms();
        let input_started_at_ms = perf_now_ms();
        let input = browser_game_input_from_js(&frame)?;
        let input_parse_ms = perf_now_ms() - input_started_at_ms;

        let game_state_started_at_ms = perf_now_ms();
        self.game_state.tick(input).map_err(js_error)?;
        let game_state_tick_ms = perf_now_ms() - game_state_started_at_ms;

        let player_character_started_at_ms = perf_now_ms();
        self.update_player_character_mesh(input)?;
        let player_character_update_ms = perf_now_ms() - player_character_started_at_ms;

        let terrain_stream_started_at_ms = perf_now_ms();
        self.update_terrain_stream()?;
        let terrain_stream_update_ms = perf_now_ms() - terrain_stream_started_at_ms;

        let render_frame_started_at_ms = perf_now_ms();
        let render_result = self.render_frame()?;
        let render_frame_ms = perf_now_ms() - render_frame_started_at_ms;

        self.perf_history.push(FramePerfSample {
            frame_index: self.renderer.frame_index(),
            rust_cpu: RustCpuFrameTimings {
                total_frame_ms: perf_now_ms() - frame_started_at_ms,
                input_parse_ms,
                game_state_tick_ms,
                player_character_update_ms,
                terrain_completion_ingest_ms: self.last_terrain_completion_stats.total_ms,
                terrain_stream_update_ms,
                terrain_stream_tick_ms: self.last_terrain_update_stats.stream_tick_ms,
                terrain_stream_sync_ms: self.last_terrain_update_stats.stream_sync_ms,
                terrain_stream_scheduler_ms: self.last_terrain_update_stats.stream_scheduler_ms,
                terrain_stream_worker_queue_ms: self
                    .last_terrain_update_stats
                    .stream_worker_queue_ms,
                terrain_stream_visibility_ms: self.last_terrain_update_stats.stream_visibility_ms,
                terrain_stream_visibility_select_ms: self
                    .last_terrain_update_stats
                    .stream_visibility_select_ms,
                terrain_stream_visibility_status_ms: self
                    .last_terrain_update_stats
                    .stream_visibility_status_ms,
                terrain_stream_visibility_apply_ms: self
                    .last_terrain_update_stats
                    .stream_visibility_apply_ms,
                terrain_mesh_destroy_ms: self.last_terrain_update_stats.mesh_destroy_ms,
                terrain_mesh_upload_ms: self.last_terrain_update_stats.mesh_upload_ms,
                render_frame_ms,
                render_packet_build_ms: render_result.cpu_timings.render_packet_build_ms,
                renderer_prepare_ms: render_result.cpu_timings.renderer_prepare_ms,
                renderer_shadow_cpu_ms: render_result.cpu_timings.renderer_shadow_cpu_ms,
                renderer_scene_cpu_ms: render_result.cpu_timings.renderer_scene_cpu_ms,
                renderer_post_cpu_ms: render_result.cpu_timings.renderer_post_cpu_ms,
                renderer_submit_ms: render_result.cpu_timings.renderer_submit_ms,
            },
            renderer_counters: render_result.counters,
            gpu_pass_timings: render_result.gpu_pass_timings,
        });

        Ok(())
    }

    #[wasm_bindgen(js_name = configureTerrainWorkers)]
    pub fn configure_terrain_workers(&mut self, options: JsValue) -> Result<(), JsValue> {
        let worker_count = js_required_u32(&options, "workerCount", "terrainWorkers.workerCount")?;
        self.terrain_stream
            .configure_worker_runtime(worker_count as usize)
            .map_err(js_error)
    }

    #[wasm_bindgen(js_name = takeTerrainBuildRequests)]
    pub fn take_terrain_build_requests(&mut self) -> Result<JsValue, JsValue> {
        let started_at_ms = perf_now_ms();
        let requests = self.terrain_stream.take_worker_build_requests();
        let request_count = requests.len().min(u32::MAX as usize) as u32;
        let result = terrain_build_requests_to_js(requests);
        self.last_terrain_request_stats = TerrainRequestStats {
            total_ms: perf_now_ms() - started_at_ms,
            request_count,
        };
        result
    }

    #[wasm_bindgen(js_name = completeTerrainBuilds)]
    pub fn complete_terrain_builds(&mut self, completions: JsValue) -> Result<u32, JsValue> {
        let started_at_ms = perf_now_ms();
        let array = js_sys::Array::from(&completions);
        let mut accepted_count = 0;
        let mut completion_count = 0_u32;
        let mut vertex_float_count = 0_u32;
        let mut index_count = 0_u32;
        for value in array.iter() {
            let completion = terrain_build_completion_from_js(&value)?;
            completion_count = completion_count.saturating_add(1);
            vertex_float_count = vertex_float_count
                .saturating_add(completion.vertices.len().min(u32::MAX as usize) as u32);
            index_count =
                index_count.saturating_add(completion.indices.len().min(u32::MAX as usize) as u32);
            if self.terrain_stream.complete_worker_build(completion) {
                accepted_count += 1;
            }
        }
        self.last_terrain_completion_stats = TerrainCompletionStats {
            total_ms: perf_now_ms() - started_at_ms,
            completion_count,
            accepted_count,
            vertex_float_count,
            index_count,
        };

        Ok(accepted_count)
    }

    #[wasm_bindgen(js_name = command)]
    pub fn command(&mut self, command: JsValue) -> Result<(), JsValue> {
        let command_type = js_required_string(&command, "type", "command.type")?;
        match command_type.as_str() {
            "resetGame" => {
                let terrain_seed = js_required_u32(&command, "terrainSeed", "command.terrainSeed")?;
                let terrain_preset =
                    js_required_u32(&command, "terrainPreset", "command.terrainPreset")?;
                self.game_state
                    .reset_game(terrain_seed, terrain_preset)
                    .map_err(js_error)?;
                let player_position = self.game_state.player_position().map_err(js_error)?;
                self.clear_terrain_meshes()?;
                self.terrain_stream
                    .reset_game(terrain_seed, terrain_preset, player_position);
            }
            "resetStreaming" => {
                let player_position = self.game_state.player_position().map_err(js_error)?;
                self.clear_terrain_meshes()?;
                self.terrain_stream.reset_around(player_position);
            }
            "resetPerfStats" => {
                self.perf_history.clear();
                self.renderer.reset_perf_stats();
            }
            "resetRenderDebugOptions" => {
                self.renderer.reset_render_debug_options();
            }
            "setRenderDebugOptions" => {
                let update = render_debug_options_update_from_js(&command)?;
                self.renderer
                    .set_render_debug_options(update)
                    .map_err(js_error)?;
            }
            "togglePlayerMode" => {
                self.game_state.toggle_player_mode().map_err(js_error)?;
            }
            "setPlayerMode" => {
                let mode_name = js_required_string(&command, "mode", "command.mode")?;
                let mode = player_mode_from_js_name(&mode_name).ok_or_else(|| {
                    js_error(format!(
                        "Rust browser game received unknown player mode '{mode_name}'."
                    ))
                })?;
                self.game_state.set_player_mode(mode).map_err(js_error)?;
            }
            "togglePlayerCharacter" => {
                self.set_player_character(self.active_player_character_id.toggled())?;
            }
            "setPlayerCharacter" => {
                let character_name =
                    js_required_string(&command, "character", "command.character")?;
                let character_id =
                    PlayerCharacterId::from_js_name(&character_name).ok_or_else(|| {
                        js_error(format!(
                            "Rust browser game received unknown player character '{character_name}'."
                        ))
                    })?;
                self.set_player_character(character_id)?;
            }
            "setPlayerAnimationTuning" => {
                self.set_player_animation_tuning(player_animation_tuning_from_js(&command)?)?;
            }
            "setPlayerPosition" => {
                let x = js_required_f32(&command, "x", "command.x")?;
                let z = js_required_f32(&command, "z", "command.z")?;
                self.game_state
                    .set_player_position_xz(x, z)
                    .map(|_| ())
                    .map_err(js_error)?;
            }
            "setDebugCamera" => {
                let position = Vec3::new(
                    js_required_f32(&command, "x", "command.x")?,
                    js_required_f32(&command, "y", "command.y")?,
                    js_required_f32(&command, "z", "command.z")?,
                );
                let yaw = js_required_f32(&command, "yaw", "command.yaw")?;
                let pitch = js_required_f32(&command, "pitch", "command.pitch")?;
                self.game_state
                    .set_debug_camera(position, yaw, pitch)
                    .map_err(js_error)?;
            }
            "setShadowDebugView" => {
                let view_name = js_required_string(&command, "view", "command.view")?;
                let view = ShadowDebugView::from_js_name(&view_name).ok_or_else(|| {
                    js_error(format!(
                        "Rust browser game received unknown shadow debug view '{view_name}'."
                    ))
                })?;
                self.renderer.set_shadow_debug_view(view);
            }
            "setPostProcessDebugView" => {
                let view_name = js_required_string(&command, "view", "command.view")?;
                self.renderer.set_post_process_debug_view(&view_name)?;
            }
            "setPostProcessToneMapping" => {
                let enabled = js_required_bool(&command, "enabled", "command.enabled")?;
                let exposure = js_required_f32(&command, "exposure", "command.exposure")?;
                self.renderer
                    .set_post_process_tone_mapping(enabled, exposure)?;
            }
            "setPostProcessBloom" => {
                let enabled = js_required_bool(&command, "enabled", "command.enabled")?;
                let threshold = js_required_f32(&command, "threshold", "command.threshold")?;
                let intensity = js_required_f32(&command, "intensity", "command.intensity")?;
                self.renderer
                    .set_post_process_bloom(enabled, threshold, intensity)?;
            }
            "setPostProcessDepthOfField" => {
                let enabled = js_required_bool(&command, "enabled", "command.enabled")?;
                let focus_distance =
                    js_required_f32(&command, "focusDistance", "command.focusDistance")?;
                let focus_range = js_required_f32(&command, "focusRange", "command.focusRange")?;
                let max_blur_pixels =
                    js_required_f32(&command, "maxBlurPixels", "command.maxBlurPixels")?;
                self.renderer.set_post_process_depth_of_field(
                    enabled,
                    focus_distance,
                    focus_range,
                    max_blur_pixels,
                )?;
            }
            _ => {
                return Err(js_error(format!(
                    "Rust browser game received unknown command '{command_type}'."
                )));
            }
        }

        Ok(())
    }

    #[wasm_bindgen(js_name = debugSnapshot)]
    pub fn debug_snapshot(&self) -> Result<JsValue, JsValue> {
        let player_mode = self.game_state.player_mode().map_err(js_error)?;
        let player_position = self.game_state.player_position().map_err(js_error)?;
        let player_character = self.active_player_character_slot()?;
        let terrain_status = self.terrain_stream.status();
        let position = js_sys::Object::new();
        set_js_property(&position, "x", JsValue::from_f64(player_position.x as f64))?;
        set_js_property(&position, "y", JsValue::from_f64(player_position.y as f64))?;
        set_js_property(&position, "z", JsValue::from_f64(player_position.z as f64))?;

        let snapshot = js_sys::Object::new();
        set_js_property(
            &snapshot,
            "playerMode",
            JsValue::from_str(player_mode_to_js_name(player_mode)),
        )?;
        set_js_property(&snapshot, "playerPosition", position.into())?;
        set_js_property(
            &snapshot,
            "loadedTerrainChunkKeys",
            string_vec_to_js_array(self.terrain_stream.loaded_chunk_keys()).into(),
        )?;
        set_js_property(
            &snapshot,
            "loadedTerrainNodeKeys",
            string_vec_to_js_array(self.terrain_stream.loaded_node_keys()).into(),
        )?;
        set_js_property(
            &snapshot,
            "terrainChunkKeys",
            string_vec_to_js_array(self.terrain_stream.render_chunk_keys()).into(),
        )?;
        set_js_property(
            &snapshot,
            "terrainNodeKeys",
            string_vec_to_js_array(self.terrain_stream.render_node_keys()).into(),
        )?;
        set_js_property(
            &snapshot,
            "terrainSeed",
            JsValue::from_f64(self.game_state.terrain_seed() as f64),
        )?;
        set_js_property(
            &snapshot,
            "terrainPreset",
            JsValue::from_str(terrain_preset_to_js_name(self.game_state.terrain_preset())),
        )?;
        set_js_property(
            &snapshot,
            "terrainStreamStatus",
            terrain_stream_status_to_js(terrain_status.clone())?,
        )?;
        set_js_property(
            &snapshot,
            "terrainStreamerRuntime",
            JsValue::from_str("rust"),
        )?;
        set_js_property(
            &snapshot,
            "terrainStreamSchedulerRuntime",
            JsValue::from_str("rust"),
        )?;
        set_js_property(
            &snapshot,
            "terrainDensityStoreRuntime",
            JsValue::from_str("rust"),
        )?;
        set_js_property(
            &snapshot,
            "terrainWorkerPoolRuntime",
            JsValue::from_str(terrain_status.terrain_worker_runtime),
        )?;
        set_js_property(&snapshot, "renderPacketRuntime", JsValue::from_str("rust"))?;
        set_js_property(
            &snapshot,
            "terrainRenderPacketRuntime",
            JsValue::from_str("rust"),
        )?;
        set_js_property(&snapshot, "rendererRuntime", JsValue::from_str("rust-wgpu"))?;
        set_js_property(
            &snapshot,
            "rendererStatus",
            renderer_status_to_js(self.renderer.status(
                self.last_terrain_update_stats,
                self.last_terrain_completion_stats,
                self.last_terrain_request_stats,
            ))?,
        )?;
        set_js_property(
            &snapshot,
            "rustPerfStats",
            frame_perf_report_to_js(self.perf_history.report(), self.renderer.gpu_timer_status())?,
        )?;
        set_js_property(
            &snapshot,
            "renderDebugOptions",
            render_debug_options_to_js(self.renderer.render_debug_options())?,
        )?;
        let sky_snapshot = self.game_state.sky_snapshot();
        set_js_property(
            &snapshot,
            "skyRuntime",
            JsValue::from_str(sky_snapshot.runtime),
        )?;
        set_js_property(
            &snapshot,
            "skyDayPhase",
            JsValue::from_f64(sky_snapshot.day_phase as f64),
        )?;
        set_js_property(
            &snapshot,
            "skySunElevation",
            JsValue::from_f64(sky_snapshot.sun_elevation as f64),
        )?;
        set_js_property(
            &snapshot,
            "skyCloudCoverage",
            JsValue::from_f64(sky_snapshot.cloud_coverage as f64),
        )?;
        set_js_property(
            &snapshot,
            "skyStarIntensity",
            JsValue::from_f64(sky_snapshot.star_intensity as f64),
        )?;
        set_js_property(
            &snapshot,
            "shadowDebugView",
            JsValue::from_str(self.renderer.shadow_debug_view_name()),
        )?;
        set_js_property(
            &snapshot,
            "terrainWorkerCount",
            JsValue::from_f64(terrain_status.terrain_worker_count as f64),
        )?;
        set_js_property(
            &snapshot,
            "playerControllerRuntime",
            JsValue::from_str("rust"),
        )?;
        set_js_property(
            &snapshot,
            "playerCharacterId",
            JsValue::from_str(player_character.descriptor.id.js_name()),
        )?;
        set_js_property(
            &snapshot,
            "playerCharacterLabel",
            JsValue::from_str(player_character.descriptor.label),
        )?;
        set_js_property(
            &snapshot,
            "modelPrimitiveCount",
            JsValue::from_f64(player_character.model.part_count() as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelMaterialCount",
            JsValue::from_f64(player_character.material_count as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelTextureCount",
            JsValue::from_f64(player_character.texture_count as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelNonFallbackAlbedoPartCount",
            JsValue::from_f64(player_character.non_fallback_albedo_part_count as f64),
        )?;
        if let Some(character_scene) = self
            .game_state
            .player_character_scene_snapshot()
            .map_err(js_error)?
        {
            set_js_property(
                &snapshot,
                "playerCharacterRuntime",
                JsValue::from_str(character_scene.runtime),
            )?;
            set_js_property(
                &snapshot,
                "playerCharacterVisible",
                JsValue::from_bool(character_scene.visible),
            )?;
            set_js_property(
                &snapshot,
                "playerCharacterFollowsPlayer",
                JsValue::from_bool(character_scene.follows_player),
            )?;
            set_js_property(
                &snapshot,
                "debugPlayerMarkerVisible",
                JsValue::from_bool(character_scene.debug_marker_visible),
            )?;
        }
        let animation = player_character.model.animation_snapshot();
        let animation_tuning = player_character.model.locomotion_tuning();
        set_js_property(
            &snapshot,
            "modelAnimationRuntime",
            JsValue::from_str(animation.runtime),
        )?;
        set_js_property(
            &snapshot,
            "activeModelAnimationClip",
            JsValue::from_str(&animation.active_clip_name),
        )?;
        if let Some(next_clip_name) = animation.next_clip_name {
            set_js_property(
                &snapshot,
                "nextModelAnimationClip",
                JsValue::from_str(&next_clip_name),
            )?;
        }
        set_js_property(
            &snapshot,
            "modelAnimationTimeSeconds",
            JsValue::from_f64(animation.time_seconds as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationDurationSeconds",
            JsValue::from_f64(animation.duration_seconds as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationBlendWeight",
            JsValue::from_f64(animation.blend_weight as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationWalkRunBlendWeight",
            JsValue::from_f64(animation.walk_run_blend_weight as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationPlaybackScale",
            JsValue::from_f64(animation.playback_scale as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationLocomotionSpeedMetersPerSecond",
            JsValue::from_f64(animation.locomotion_speed_meters_per_second as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationWalkSpeedMetersPerSecond",
            JsValue::from_f64(animation_tuning.walk_speed_meters_per_second as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationRunSpeedMetersPerSecond",
            JsValue::from_f64(animation_tuning.run_speed_meters_per_second as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationIdlePlaybackScale",
            JsValue::from_f64(animation_tuning.idle_playback_scale as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationWalkPlaybackScale",
            JsValue::from_f64(animation_tuning.walk_playback_scale as f64),
        )?;
        set_js_property(
            &snapshot,
            "modelAnimationRunPlaybackScale",
            JsValue::from_f64(animation_tuning.run_playback_scale as f64),
        )?;
        if let Some(runtime) = self.model_skinning_runtime {
            set_js_property(
                &snapshot,
                "modelSkinningRuntime",
                JsValue::from_str(runtime),
            )?;
            set_js_property(
                &snapshot,
                "modelSkinningJointCount",
                JsValue::from_f64(player_character.model.skin_joint_count() as f64),
            )?;
        }

        Ok(snapshot.into())
    }

    fn render_frame(&mut self) -> Result<RenderFrameResult, JsValue> {
        let packet_started_at_ms = perf_now_ms();
        let engine_snapshot = self.game_state.render_snapshot_values().map_err(js_error)?;
        let scene_mesh_items = self.game_state.render_mesh_items().map_err(js_error)?;
        let aspect = self.renderer.aspect_ratio();
        let render_debug_options = self.renderer.render_debug_options();
        let terrain_node_keys = sorted_terrain_node_keys(&self.terrain_mesh_handles_by_key);
        let visible_terrain_node_keys = terrain_node_keys
            .iter()
            .filter_map(|node_key| {
                let lod = terrain_lod_from_node_key(node_key).unwrap_or(0);
                render_debug_options
                    .terrain_lod_enabled(lod)
                    .then_some((node_key, lod))
            })
            .collect::<Vec<_>>();
        let item_count = visible_terrain_node_keys.len() + scene_mesh_items.len();

        let mut mesh_handles = Vec::with_capacity(item_count);
        let mut object_handles = Vec::with_capacity(item_count);
        let mut albedo_texture_handles = Vec::with_capacity(item_count);
        let mut normal_texture_handles = Vec::with_capacity(item_count);
        let mut material_texture_handles = Vec::with_capacity(item_count);
        let mut terrain_lods = Vec::with_capacity(item_count);
        let mut world_matrices = Vec::with_capacity(item_count * WORLD_MATRIX_FLOATS);
        let mut material_packets = Vec::with_capacity(item_count * MATERIAL_PACKET_FLOATS);
        let terrain_textures = self.terrain_textures.unwrap_or(TerrainTextureHandles {
            albedo: self.renderer.fallback_albedo,
            normal: self.renderer.fallback_normal,
            material: self.renderer.fallback_material,
        });

        for (terrain_node_key, lod) in visible_terrain_node_keys {
            let mesh_handle = *self
                .terrain_mesh_handles_by_key
                .get(terrain_node_key)
                .ok_or_else(|| {
                    js_error(format!(
                        "Rust browser game is missing terrain mesh '{terrain_node_key}'."
                    ))
                })?;
            let object_handle = self.object_handle_for_id(terrain_node_key)?;

            mesh_handles.push(handle_to_js(mesh_handle));
            object_handles.push(handle_to_js(object_handle));
            albedo_texture_handles.push(handle_to_js(terrain_textures.albedo));
            normal_texture_handles.push(handle_to_js(terrain_textures.normal));
            material_texture_handles.push(handle_to_js(terrain_textures.material));
            terrain_lods.push(i32::from(lod));
            world_matrices.extend_from_slice(&IDENTITY_WORLD_MATRIX);
            material_packets.extend_from_slice(&TERRAIN_MATERIAL_PACKET);
        }

        for item in scene_mesh_items {
            let mesh_handle = self.scene_mesh_handle(&item.mesh_label)?;
            let material = self.scene_material_resource(&item.material_label)?;
            let object_handle =
                self.object_handle_for_id(&format!("entity:{}", item.entity.to_raw()))?;

            mesh_handles.push(handle_to_js(mesh_handle));
            object_handles.push(handle_to_js(object_handle));
            albedo_texture_handles.push(handle_to_js(material.albedo_texture));
            normal_texture_handles.push(handle_to_js(material.normal_texture));
            material_texture_handles.push(handle_to_js(material.material_texture));
            terrain_lods.push(-1);
            world_matrices.extend_from_slice(&item.world_matrix);
            material_packets.extend_from_slice(&material.packet);
        }

        let render_packet_build_ms = perf_now_ms() - packet_started_at_ms;
        let mut result = self.renderer.render_engine_frame(
            &engine_snapshot,
            aspect,
            &mesh_handles,
            &object_handles,
            &albedo_texture_handles,
            &normal_texture_handles,
            &material_texture_handles,
            &terrain_lods,
            &world_matrices,
            &material_packets,
        )?;
        result.cpu_timings.render_packet_build_ms = render_packet_build_ms;
        Ok(result)
    }
}

impl RustBrowserGame {
    fn scene_mesh_handle(&self, label: &str) -> Result<ResourceHandle, JsValue> {
        self.scene_mesh_handles_by_label
            .get(label)
            .copied()
            .ok_or_else(|| {
                js_error(format!(
                    "Rust WebGPU renderer cannot resolve scene mesh '{label}'."
                ))
            })
    }

    fn scene_material_resource(&self, label: &str) -> Result<SceneMaterialResource, JsValue> {
        self.scene_material_resources_by_label
            .get(label)
            .copied()
            .ok_or_else(|| {
                js_error(format!(
                    "Rust WebGPU renderer cannot resolve scene material '{label}'."
                ))
            })
    }

    fn install_terrain_textures(&mut self, textures: TerrainTextureArrays) -> Result<(), JsValue> {
        if let Some(handles) = self.terrain_textures.take() {
            self.destroy_terrain_textures(handles)?;
        }

        let albedo = self.renderer.register_texture(
            textures.width,
            textures.height,
            textures.layers,
            textures.format_code,
            &textures.albedo.data,
        )?;
        let normal = match self.renderer.register_texture(
            textures.width,
            textures.height,
            textures.layers,
            textures.format_code,
            &textures.normal.data,
        ) {
            Ok(handle) => handle,
            Err(error) => {
                self.renderer.destroy_texture(albedo)?;
                return Err(error);
            }
        };
        let material = match self.renderer.register_texture(
            textures.width,
            textures.height,
            textures.layers,
            textures.format_code,
            &textures.material.data,
        ) {
            Ok(handle) => handle,
            Err(error) => {
                self.renderer.destroy_texture(albedo)?;
                self.renderer.destroy_texture(normal)?;
                return Err(error);
            }
        };

        self.terrain_textures = Some(TerrainTextureHandles {
            albedo,
            normal,
            material,
        });
        Ok(())
    }

    fn active_player_character_slot(&self) -> Result<&PlayerCharacterSlot, JsValue> {
        self.player_characters
            .iter()
            .find(|slot| slot.descriptor.id == self.active_player_character_id)
            .ok_or_else(|| {
                js_error(format!(
                    "Rust browser game cannot resolve active player character '{}'.",
                    self.active_player_character_id
                ))
            })
    }

    fn active_player_character_slot_mut(&mut self) -> Result<&mut PlayerCharacterSlot, JsValue> {
        let active_player_character_id = self.active_player_character_id;
        self.player_characters
            .iter_mut()
            .find(|slot| slot.descriptor.id == active_player_character_id)
            .ok_or_else(|| {
                js_error(format!(
                    "Rust browser game cannot resolve active player character '{active_player_character_id}'."
                ))
            })
    }

    fn set_player_character(&mut self, character_id: PlayerCharacterId) -> Result<(), JsValue> {
        let scene_parts = self
            .player_characters
            .iter()
            .find(|slot| slot.descriptor.id == character_id)
            .map(|slot| slot.scene_parts.clone())
            .ok_or_else(|| {
                js_error(format!(
                    "Rust browser game cannot select unavailable player character '{character_id}'."
                ))
            })?;
        self.active_player_character_id = character_id;
        self.game_state
            .configure_player_character_scene_parts(
                scene_parts,
                PLAYER_CHARACTER_SCENE_SCALE,
                PLAYER_CHARACTER_HEIGHT_OFFSET,
            )
            .map_err(js_error)
    }

    fn set_player_animation_tuning(
        &mut self,
        tuning: PlayerCharacterLocomotionTuning,
    ) -> Result<(), JsValue> {
        for player_character in &mut self.player_characters {
            player_character
                .model
                .set_locomotion_tuning(tuning)
                .map_err(js_error)?;
        }

        Ok(())
    }

    fn update_player_character_mesh(&mut self, input: BrowserGameInput) -> Result<(), JsValue> {
        let locomotion_speed_meters_per_second = player_locomotion_speed_meters_per_second(input);
        let (mesh_handles, part_vertices) = {
            let player_character = self.active_player_character_slot_mut()?;
            (
                player_character.mesh_handles.clone(),
                player_character
                    .model
                    .tick_part_vertices(input.delta_seconds, locomotion_speed_meters_per_second)
                    .map_err(js_error)?,
            )
        };
        if mesh_handles.len() != part_vertices.len() {
            return Err(js_error(
                "Rust browser game has mismatched player character mesh and primitive counts.",
            ));
        }
        for (mesh_handle, vertices) in mesh_handles.into_iter().zip(part_vertices.iter()) {
            self.renderer
                .update_mesh_vertices(mesh_handle, vertices, MODEL_VERTEX_FLOATS)?;
        }

        Ok(())
    }

    fn update_terrain_stream(&mut self) -> Result<(), JsValue> {
        let started_at_ms = terrain_update_now_ms();
        let mut removed_mesh_count = 0_u32;
        let mut upserted_mesh_count = 0_u32;
        let mut uploaded_vertex_float_count = 0_u32;
        let mut uploaded_index_count = 0_u32;
        let player_position = self.game_state.player_position().map_err(js_error)?;
        let stream_tick_started_at_ms = terrain_update_now_ms();
        let update = self.terrain_stream.tick_for_workers(player_position);
        let stream_tick_ms = terrain_update_now_ms() - stream_tick_started_at_ms;
        let stream_timings = update.timings;

        for key in update.removed_nodes {
            if !self.remove_pending_terrain_upload(key) {
                self.pending_terrain_removals.push_back(key);
            }
        }

        for mesh_update in update.upserted_meshes {
            self.remove_pending_terrain_upload(mesh_update.key);
            self.pending_terrain_uploads.push_back(mesh_update);
        }

        let upload_started_at_ms = terrain_update_now_ms();
        while let Some(mesh_update) = self.pending_terrain_uploads.front() {
            let next_vertex_float_count =
                mesh_update.mesh.vertices.len().min(u32::MAX as usize) as u32;
            let next_index_count = mesh_update.mesh.indices.len().min(u32::MAX as usize) as u32;
            let upload_count_budget_hit =
                upserted_mesh_count >= TERRAIN_UPLOAD_MAX_MESHES_PER_FRAME;
            let upload_vertex_budget_hit = upserted_mesh_count > 0
                && uploaded_vertex_float_count.saturating_add(next_vertex_float_count)
                    > TERRAIN_UPLOAD_MAX_VERTEX_FLOATS_PER_FRAME;
            if upload_count_budget_hit || upload_vertex_budget_hit {
                break;
            }

            let mesh_update = self
                .pending_terrain_uploads
                .pop_front()
                .expect("front terrain upload exists");
            uploaded_vertex_float_count =
                uploaded_vertex_float_count.saturating_add(next_vertex_float_count);
            uploaded_index_count = uploaded_index_count.saturating_add(next_index_count);
            self.upsert_terrain_mesh(
                mesh_update.key,
                &mesh_update.mesh.vertices,
                &mesh_update.mesh.indices,
            )?;
            upserted_mesh_count = upserted_mesh_count.saturating_add(1);
        }
        let mesh_upload_ms = terrain_update_now_ms() - upload_started_at_ms;

        let destroy_started_at_ms = terrain_update_now_ms();
        while removed_mesh_count < TERRAIN_REMOVAL_MAX_MESHES_PER_FRAME {
            let Some(key) = pop_ready_terrain_removal(
                &mut self.pending_terrain_removals,
                &self.pending_terrain_uploads,
            ) else {
                break;
            };
            self.destroy_terrain_mesh(key)?;
            removed_mesh_count = removed_mesh_count.saturating_add(1);
        }
        let mesh_destroy_ms = terrain_update_now_ms() - destroy_started_at_ms;

        self.last_terrain_update_stats = TerrainUpdateStats {
            total_ms: terrain_update_now_ms() - started_at_ms,
            stream_tick_ms,
            stream_sync_ms: stream_timings.sync_around_ms,
            stream_scheduler_ms: stream_timings.scheduler_tick_ms,
            stream_worker_queue_ms: stream_timings.worker_request_queue_ms,
            stream_visibility_ms: stream_timings.visibility_sync_ms,
            stream_visibility_select_ms: stream_timings.visibility_select_ms,
            stream_visibility_status_ms: stream_timings.visibility_status_ms,
            stream_visibility_apply_ms: stream_timings.visibility_apply_ms,
            mesh_destroy_ms,
            mesh_upload_ms,
            upserted_mesh_count,
            removed_mesh_count,
            uploaded_vertex_float_count,
            uploaded_index_count,
            deferred_upload_count: self.pending_terrain_uploads.len().min(u32::MAX as usize) as u32,
            deferred_removal_count: self.pending_terrain_removals.len().min(u32::MAX as usize)
                as u32,
            upload_budget_hit: !self.pending_terrain_uploads.is_empty(),
            removal_budget_hit: !self.pending_terrain_removals.is_empty(),
        };

        Ok(())
    }

    fn remove_pending_terrain_upload(&mut self, key: TerrainNodeKey) -> bool {
        let original_len = self.pending_terrain_uploads.len();
        self.pending_terrain_uploads
            .retain(|mesh_update| mesh_update.key != key);
        original_len != self.pending_terrain_uploads.len()
    }

    fn upsert_terrain_mesh(
        &mut self,
        key: TerrainNodeKey,
        vertices: &[f32],
        indices: &[u32],
    ) -> Result<(), JsValue> {
        let node_key = terrain_node_key(key);
        if let Some(handle) = self.terrain_mesh_handles_by_key.remove(&node_key) {
            self.renderer.destroy_mesh(handle)?;
        }

        let handle = self
            .renderer
            .register_mesh(vertices, indices, TERRAIN_VERTEX_FLOATS)?;
        self.terrain_mesh_handles_by_key.insert(node_key, handle);
        Ok(())
    }

    fn destroy_terrain_mesh(&mut self, key: TerrainNodeKey) -> Result<(), JsValue> {
        self.destroy_terrain_mesh_by_key(&terrain_node_key(key))
    }

    fn destroy_terrain_mesh_by_key(&mut self, node_key: &str) -> Result<(), JsValue> {
        let Some(handle) = self.terrain_mesh_handles_by_key.remove(node_key) else {
            return Ok(());
        };
        self.renderer.destroy_mesh(handle)?;

        if let Some(object_handle) = self.object_handles_by_id.remove(node_key) {
            self.renderer.destroy_object(object_handle)?;
        }

        Ok(())
    }

    fn clear_terrain_meshes(&mut self) -> Result<(), JsValue> {
        self.pending_terrain_uploads.clear();
        self.pending_terrain_removals.clear();
        let node_keys = sorted_terrain_node_keys(&self.terrain_mesh_handles_by_key);
        for node_key in node_keys {
            self.destroy_terrain_mesh_by_key(&node_key)?;
        }

        Ok(())
    }

    fn object_handle_for_id(&mut self, id: &str) -> Result<ResourceHandle, JsValue> {
        if let Some(handle) = self.object_handles_by_id.get(id) {
            return Ok(*handle);
        }

        let handle = self.renderer.register_object()?;
        self.object_handles_by_id.insert(id.to_string(), handle);
        Ok(handle)
    }

    fn destroy_terrain_textures(&mut self, handles: TerrainTextureHandles) -> Result<(), JsValue> {
        self.renderer.destroy_texture(handles.albedo)?;
        self.renderer.destroy_texture(handles.normal)?;
        self.renderer.destroy_texture(handles.material)?;
        Ok(())
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
        let adapter_features = adapter.features();
        let timestamp_query_supported = adapter_features.contains(wgpu::Features::TIMESTAMP_QUERY);
        let required_features = if timestamp_query_supported {
            wgpu::Features::TIMESTAMP_QUERY
        } else {
            wgpu::Features::empty()
        };

        let mut limits =
            wgpu::Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits());
        limits.max_texture_array_layers = limits
            .max_texture_array_layers
            .max(REQUIRED_TEXTURE_ARRAY_LAYERS);
        let (device, queue) = adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    label: Some("ofg rust webgpu device"),
                    required_features,
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
        let shadow_resources = create_shadow_resources(&device);
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
            mipmap_filter: wgpu::FilterMode::Linear,
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
            bind_group_layouts: &[
                &camera_bind_group_layout,
                &object_bind_group_layout,
                &shadow_resources.bind_group_layout,
            ],
            push_constant_ranges: &[],
        });
        let sky_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("sky pipeline layout"),
            bind_group_layouts: &[&camera_bind_group_layout],
            push_constant_ranges: &[],
        });
        let shadow_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("shadow pipeline layout"),
                bind_group_layouts: &[
                    &camera_bind_group_layout,
                    &object_bind_group_layout,
                    &shadow_resources.depth_bind_group_layout,
                ],
                push_constant_ranges: &[],
            });
        let pipeline = create_main_pipeline(&device, &pipeline_layout, &shader);
        let model_pipeline = create_model_pipeline(&device, &pipeline_layout, &shader);
        let sky_pipeline = create_sky_pipeline(&device, &sky_pipeline_layout, &shader);
        let shadow_pipelines = create_shadow_pipelines(&device, &shadow_pipeline_layout, &shader);
        let post_process =
            PostProcessResources::new(&device, format, display_width, display_height);
        let gpu_timer_status = GpuTimerStatus {
            available: timestamp_query_supported,
            unavailable_reason: if timestamp_query_supported {
                ""
            } else {
                "adapter does not expose TIMESTAMP_QUERY"
            },
            timestamp_period_ns: if timestamp_query_supported {
                queue.get_timestamp_period() as f64
            } else {
                0.0
            },
            pending_readback_count: 0,
        };
        let gpu_timers = if timestamp_query_supported {
            Some(create_gpu_timer_resources(&device, &queue))
        } else {
            None
        };
        let mut renderer = Self {
            canvas,
            surface,
            device,
            queue,
            config,
            depth_texture,
            shadow_resources,
            camera_uniform_buffer,
            camera_bind_group,
            object_bind_group_layout,
            sampler,
            sky_pipeline,
            pipeline,
            model_pipeline,
            shadow_pipelines,
            shadow_debug_view: ShadowDebugView::Off,
            post_process,
            post_process_settings: PostProcessSettings::default(),
            max_texture_array_layers,
            meshes: ResourceStore::new(),
            textures: ResourceStore::new(),
            objects: ResourceStore::new(),
            fallback_albedo: ResourceHandle::INVALID,
            fallback_normal: ResourceHandle::INVALID,
            fallback_material: ResourceHandle::INVALID,
            frame_index: 0,
            frame_draw_count: 0,
            frame_visible_draw_count: 0,
            frame_shadow_draw_count: 0,
            render_debug_options: RenderDebugOptions::default(),
            last_shadow_runtime: ShadowRuntimeState::default(),
            last_render_counters: RenderCounterSample::default(),
            last_gpu_pass_timings: GpuPassTimings::default(),
            gpu_timer_status,
            gpu_timers,
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
        self.post_process.resize(&self.device, width, height);
        Ok(())
    }

    fn aspect_ratio(&self) -> f32 {
        self.config.width as f32 / self.config.height as f32
    }

    fn register_mesh(
        &mut self,
        vertices: &[f32],
        indices: &[u32],
        floats_per_vertex: u32,
    ) -> Result<ResourceHandle, JsValue> {
        let Some(vertex_layout) = MeshVertexLayout::from_floats_per_vertex(floats_per_vertex)
        else {
            return Err(js_error("Rust WebGPU renderer rejected an invalid mesh."));
        };
        if vertices.is_empty()
            || indices.is_empty()
            || vertices.len() % floats_per_vertex as usize != 0
            || indices.len() % 3 != 0
        {
            return Err(js_error("Rust WebGPU renderer rejected an invalid mesh."));
        }
        let local_bounds = aabb_from_vertex_positions(vertices, floats_per_vertex, 0)
            .ok_or_else(|| js_error("Rust WebGPU renderer rejected an invalid mesh."))?;

        let vertex_buffer = self
            .device
            .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("mesh vertices"),
                contents: f32_as_bytes(vertices),
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
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
            vertex_float_count: vertices.len(),
            vertex_layout,
            local_bounds,
        }))
    }

    fn update_mesh_vertices(
        &mut self,
        handle: ResourceHandle,
        vertices: &[f32],
        floats_per_vertex: u32,
    ) -> Result<(), JsValue> {
        let Some(vertex_layout) = MeshVertexLayout::from_floats_per_vertex(floats_per_vertex)
        else {
            return Err(js_error(
                "Rust WebGPU renderer rejected an invalid mesh update.",
            ));
        };
        if vertices.is_empty() || vertices.len() % floats_per_vertex as usize != 0 {
            return Err(js_error(
                "Rust WebGPU renderer rejected an invalid mesh update.",
            ));
        }

        let local_bounds = aabb_from_vertex_positions(vertices, floats_per_vertex, 0)
            .ok_or_else(|| js_error("Rust WebGPU renderer rejected an invalid mesh update."))?;

        let mesh = self
            .meshes
            .get_mut(handle)
            .ok_or_else(|| js_error("Rust WebGPU renderer rejected a stale mesh handle."))?;
        if mesh.vertex_layout != vertex_layout || mesh.vertex_float_count != vertices.len() {
            return Err(js_error(
                "Rust WebGPU renderer rejected a mismatched mesh vertex update.",
            ));
        }

        self.queue
            .write_buffer(&mesh.vertex_buffer, 0, f32_as_bytes(vertices));
        mesh.local_bounds = local_bounds;
        Ok(())
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

    fn set_post_process_debug_view(&mut self, view_name: &str) -> Result<(), JsValue> {
        let debug_view = PostProcessDebugView::from_browser_name(view_name).ok_or_else(|| {
            js_error(format!(
                "Rust WebGPU renderer received unknown post-process debug view '{view_name}'."
            ))
        })?;
        self.post_process_settings.set_debug_view(debug_view);
        Ok(())
    }

    fn set_post_process_tone_mapping(
        &mut self,
        enabled: bool,
        exposure: f32,
    ) -> Result<(), JsValue> {
        if !(0.0..=16.0).contains(&exposure) {
            return Err(js_error(
                "Rust WebGPU renderer expected post-process exposure in the range 0.0..=16.0.",
            ));
        }

        self.post_process_settings
            .set_tone_mapping(enabled, exposure);
        Ok(())
    }

    fn set_post_process_bloom(
        &mut self,
        enabled: bool,
        threshold: f32,
        intensity: f32,
    ) -> Result<(), JsValue> {
        if !(0.0..=64.0).contains(&threshold) {
            return Err(js_error(
                "Rust WebGPU renderer expected bloom threshold in the range 0.0..=64.0.",
            ));
        }
        if !(0.0..=4.0).contains(&intensity) {
            return Err(js_error(
                "Rust WebGPU renderer expected bloom intensity in the range 0.0..=4.0.",
            ));
        }

        self.post_process_settings
            .set_bloom(enabled, threshold, intensity);
        Ok(())
    }

    fn set_post_process_depth_of_field(
        &mut self,
        enabled: bool,
        focus_distance: f32,
        focus_range: f32,
        max_blur_pixels: f32,
    ) -> Result<(), JsValue> {
        if !(0.1..=512.0).contains(&focus_distance) {
            return Err(js_error(
                "Rust WebGPU renderer expected DoF focus distance in the range 0.1..=512.0.",
            ));
        }
        if !(0.1..=256.0).contains(&focus_range) {
            return Err(js_error(
                "Rust WebGPU renderer expected DoF focus range in the range 0.1..=256.0.",
            ));
        }
        if !(0.0..=32.0).contains(&max_blur_pixels) {
            return Err(js_error(
                "Rust WebGPU renderer expected DoF max blur pixels in the range 0.0..=32.0.",
            ));
        }

        self.post_process_settings.set_depth_of_field(
            enabled,
            focus_distance,
            focus_range,
            max_blur_pixels,
        );
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    fn render(
        &mut self,
        frame_packet: &[f32],
        shadow_cascades: &ShadowCascadeSet,
        shadow_runtime: ShadowRuntimeState,
        mesh_handles: &[f64],
        object_handles: &[f64],
        albedo_texture_handles: &[f64],
        normal_texture_handles: &[f64],
        material_texture_handles: &[f64],
        terrain_lods: &[i32],
        world_matrices: &[f32],
        material_packets: &[f32],
    ) -> Result<RenderFrameResult, JsValue> {
        self.collect_completed_gpu_readbacks();
        let mut cpu_timings = RenderFrameCpuTimings::default();
        let mut counters = RenderCounterSample::default();
        let mut gpu_timestamp_plan = GpuFrameTimestampPlan::new(self.gpu_timers.is_some());

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
            || terrain_lods.len() != item_count
            || world_matrices.len() != item_count * WORLD_MATRIX_FLOATS
            || material_packets.len() != item_count * MATERIAL_PACKET_FLOATS
        {
            return Err(js_error(
                "Rust WebGPU renderer received mismatched render packet arrays.",
            ));
        }
        let render_prepare_started_at_ms = perf_now_ms();
        let mut view_projection = [0.0; WORLD_MATRIX_FLOATS];
        view_projection.copy_from_slice(&frame_packet[0..WORLD_MATRIX_FLOATS]);
        let camera_frustum = frustum_from_view_projection(&view_projection)
            .ok_or_else(|| js_error("Rust WebGPU renderer received an invalid camera frustum."))?;
        let mut frame_uniforms = build_frame_uniform_values(frame_packet).map_err(js_error)?;
        if !self.render_debug_options.sky_cloud_noise_enabled {
            frame_uniforms[FRAME_UNIFORM_SKY_CLOUD_COVERAGE_OFFSET] = 0.0;
        }

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
            let terrain_lod = match terrain_lods[index] {
                -1 => None,
                lod if (0..=u8::MAX as i32).contains(&lod) => Some(lod as u8),
                _ => {
                    return Err(js_error(
                        "Rust WebGPU renderer received an invalid terrain LOD marker.",
                    ))
                }
            };
            let local_bounds = self
                .meshes
                .get(mesh_handle)
                .ok_or_else(|| js_error("Rust WebGPU renderer received a stale mesh handle."))?
                .local_bounds;
            let mut world_matrix = [0.0; WORLD_MATRIX_FLOATS];
            world_matrix.copy_from_slice(
                &world_matrices[index * WORLD_MATRIX_FLOATS..(index + 1) * WORLD_MATRIX_FLOATS],
            );
            let object_uniforms = build_object_uniform_values(
                &world_matrix,
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
            render_items.push(PreparedRenderItem {
                mesh_handle,
                object_handle,
                world_bounds: transform_aabb(local_bounds, &world_matrix),
                terrain_lod,
            });
        }
        counters.set_main_camera_candidates(item_count as u64);
        cpu_timings.renderer_prepare_ms = perf_now_ms() - render_prepare_started_at_ms;

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("rust webgpu frame encoder"),
            });
        let shadow_started_at_ms = perf_now_ms();
        let shadow_draw_count = self.render_shadow_passes(
            &mut encoder,
            &render_items,
            shadow_cascades,
            shadow_runtime,
            &mut counters,
            &mut gpu_timestamp_plan,
        )?;
        cpu_timings.renderer_shadow_cpu_ms = perf_now_ms() - shadow_started_at_ms;
        let scene_started_at_ms = perf_now_ms();
        {
            let scene_query = gpu_timestamp_plan.reserve_pass(GpuTimedPass::Scene);
            let scene_timestamp_writes =
                render_pass_timestamp_writes(self.gpu_timers.as_ref(), scene_query);
            let color_attachments = [
                Some(wgpu::RenderPassColorAttachment {
                    view: self.post_process.scene_color_view(),
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
                    view: self.post_process.linear_depth_view(),
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                }),
            ];
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
                timestamp_writes: scene_timestamp_writes,
                occlusion_query_set: None,
            });
            pass.set_bind_group(0, &self.camera_bind_group, &[]);
            pass.set_bind_group(2, &self.shadow_resources.bind_group, &[]);

            let mut active_vertex_layout = None;
            let mut visible_draw_count = 0_u32;
            for item in &render_items {
                if !frustum_intersects_aabb(camera_frustum, item.world_bounds) {
                    counters.record_main_camera_cull();
                    continue;
                }
                let mesh = self.meshes.get(item.mesh_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale mesh handle.")
                })?;
                let object = self.objects.get(item.object_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale object handle.")
                })?;
                let bind_group = object.bind_group.as_ref().ok_or_else(|| {
                    js_error("Rust WebGPU renderer object bind group was not prepared.")
                })?;

                if active_vertex_layout != Some(mesh.vertex_layout) {
                    match mesh.vertex_layout {
                        MeshVertexLayout::Terrain => pass.set_pipeline(&self.pipeline),
                        MeshVertexLayout::Model => pass.set_pipeline(&self.model_pipeline),
                    }
                    active_vertex_layout = Some(mesh.vertex_layout);
                }
                pass.set_bind_group(1, bind_group, &[]);
                pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                pass.draw_indexed(0..mesh.index_count, 0, 0..1);
                counters.record_scene_draw(
                    mesh.vertex_count() as u64,
                    u64::from(mesh.index_count),
                    item.terrain_lod,
                );
                visible_draw_count = visible_draw_count.saturating_add(1);
            }
            // Draw sky after opaque scene items so depth rejects covered pixels.
            if self.render_debug_options.sky_enabled {
                pass.set_pipeline(&self.sky_pipeline);
                pass.draw(0..3, 0..1);
                counters.record_sky_draw();
            }
            self.frame_visible_draw_count = visible_draw_count;
            self.frame_shadow_draw_count = shadow_draw_count;
        }
        cpu_timings.renderer_scene_cpu_ms = perf_now_ms() - scene_started_at_ms;
        let post_started_at_ms = perf_now_ms();
        let bloom_query = gpu_timestamp_plan.reserve_pass(GpuTimedPass::Bloom);
        let post_query = gpu_timestamp_plan.reserve_pass(GpuTimedPass::PostProcess);
        let bloom_timestamp_writes =
            render_pass_timestamp_writes(self.gpu_timers.as_ref(), bloom_query);
        let post_timestamp_writes =
            render_pass_timestamp_writes(self.gpu_timers.as_ref(), post_query);
        self.post_process.render(
            &self.queue,
            &mut encoder,
            &view,
            self.post_process_settings,
            bloom_timestamp_writes,
            post_timestamp_writes,
        );
        counters.record_post_process_draw();
        counters.record_post_process_draw();
        cpu_timings.renderer_post_cpu_ms = perf_now_ms() - post_started_at_ms;

        let pending_gpu_readback =
            self.prepare_gpu_timestamp_readback(&mut encoder, gpu_timestamp_plan);
        let submit_started_at_ms = perf_now_ms();
        self.queue.submit(Some(encoder.finish()));
        if let Some(pending_gpu_readback) = pending_gpu_readback {
            self.enqueue_gpu_timestamp_readback(pending_gpu_readback);
        }
        frame.present();
        cpu_timings.renderer_submit_ms = perf_now_ms() - submit_started_at_ms;
        self.frame_index = self.frame_index.saturating_add(1);
        self.frame_draw_count = item_count as u32;
        self.last_render_counters = counters.clone();
        self.last_gpu_pass_timings = self.latest_gpu_pass_timings();
        Ok(RenderFrameResult {
            cpu_timings,
            counters,
            gpu_pass_timings: self.last_gpu_pass_timings,
        })
    }

    fn render_shadow_passes(
        &mut self,
        encoder: &mut wgpu::CommandEncoder,
        render_items: &[PreparedRenderItem],
        shadow_cascades: &ShadowCascadeSet,
        shadow_runtime: ShadowRuntimeState,
        counters: &mut RenderCounterSample,
        gpu_timestamp_plan: &mut GpuFrameTimestampPlan,
    ) -> Result<u32, JsValue> {
        let mut base_uniforms = build_shadow_uniform_values(
            shadow_cascades,
            self.render_debug_options
                .effective_shadow_sampling_enabled()
                && shadow_runtime.strength > 0.0,
            SHADOW_CONSTANT_BIAS,
            SHADOW_NORMAL_BIAS,
            1.0 / SHADOW_MAP_SIZE as f32,
        )
        .map_err(js_error)?;
        base_uniforms[SHADOW_DEBUG_MODE_OFFSET] = self.shadow_debug_view.uniform_code();
        base_uniforms[SHADOW_DEBUG_MATERIAL_MODE_OFFSET] =
            self.render_debug_options.material_mode_code();
        base_uniforms[SHADOW_DEBUG_WHITE_TEXTURES_OFFSET] =
            if self.render_debug_options.white_textures_enabled {
                1.0
            } else {
                0.0
            };
        base_uniforms[SHADOW_STRENGTH_OFFSET] = shadow_runtime.strength;
        self.queue.write_buffer(
            &self.shadow_resources.uniform_buffer,
            0,
            f32_as_bytes(&base_uniforms),
        );

        let mut shadow_draw_count = 0_u32;
        for cascade_index in 0..SHADOW_CASCADE_COUNT {
            let cascade_enabled = self.render_debug_options.shadow_pass_enabled
                && shadow_runtime.strength > 0.0
                && self
                    .render_debug_options
                    .shadow_cascade_enabled(cascade_index);
            counters.set_shadow_cascade_candidates(
                cascade_index,
                cascade_enabled,
                render_items.len() as u64,
            );
            if !cascade_enabled {
                continue;
            }

            let mut cascade_uniforms = base_uniforms;
            cascade_uniforms[0..WORLD_MATRIX_FLOATS]
                .copy_from_slice(&shadow_cascades.cascades[cascade_index].light_view_projection);
            self.queue.write_buffer(
                &self.shadow_resources.cascade_uniform_buffers[cascade_index],
                0,
                f32_as_bytes(&cascade_uniforms),
            );

            let depth_attachment = Some(wgpu::RenderPassDepthStencilAttachment {
                view: &self.shadow_resources.layer_views[cascade_index],
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            });
            let shadow_query =
                gpu_timestamp_plan.reserve_pass(GpuTimedPass::ShadowCascade(cascade_index));
            let shadow_timestamp_writes =
                render_pass_timestamp_writes(self.gpu_timers.as_ref(), shadow_query);
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("shadow map render pass"),
                color_attachments: &[],
                depth_stencil_attachment: depth_attachment,
                timestamp_writes: shadow_timestamp_writes,
                occlusion_query_set: None,
            });
            pass.set_bind_group(0, &self.camera_bind_group, &[]);
            pass.set_bind_group(
                2,
                &self.shadow_resources.cascade_bind_groups[cascade_index],
                &[],
            );

            let mut active_vertex_layout = None;
            for item in render_items {
                if !shadow_caster_intersects_cascade(
                    shadow_cascades.cascades[cascade_index],
                    item.world_bounds,
                ) {
                    counters.record_shadow_cull(cascade_index);
                    continue;
                }
                let mesh = self.meshes.get(item.mesh_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale mesh handle.")
                })?;
                let object = self.objects.get(item.object_handle).ok_or_else(|| {
                    js_error("Rust WebGPU renderer received a stale object handle.")
                })?;
                let bind_group = object.bind_group.as_ref().ok_or_else(|| {
                    js_error("Rust WebGPU renderer object bind group was not prepared.")
                })?;

                if active_vertex_layout != Some(mesh.vertex_layout) {
                    match mesh.vertex_layout {
                        MeshVertexLayout::Terrain => {
                            pass.set_pipeline(&self.shadow_pipelines.terrain)
                        }
                        MeshVertexLayout::Model => pass.set_pipeline(&self.shadow_pipelines.model),
                    }
                    active_vertex_layout = Some(mesh.vertex_layout);
                }
                pass.set_bind_group(1, bind_group, &[]);
                pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                pass.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                pass.draw_indexed(0..mesh.index_count, 0, 0..1);
                counters.record_shadow_draw(
                    cascade_index,
                    mesh.vertex_count() as u64,
                    u64::from(mesh.index_count),
                );
                shadow_draw_count = shadow_draw_count.saturating_add(1);
            }
        }

        Ok(shadow_draw_count)
    }

    fn render_engine_frame(
        &mut self,
        engine_snapshot: &[f32],
        aspect: f32,
        mesh_handles: &[f64],
        object_handles: &[f64],
        albedo_texture_handles: &[f64],
        normal_texture_handles: &[f64],
        material_texture_handles: &[f64],
        terrain_lods: &[i32],
        world_matrices: &[f32],
        material_packets: &[f32],
    ) -> Result<RenderFrameResult, JsValue> {
        let shadow_runtime =
            shadow_runtime_state_from_engine_snapshot(engine_snapshot, self.render_debug_options)?;
        let effective_snapshot = engine_snapshot_with_shadow_debug_light(
            engine_snapshot,
            self.render_debug_options,
            shadow_runtime,
        )?;
        let frame_packet = build_frame_packet_from_engine_snapshot(&effective_snapshot, aspect)
            .map_err(js_error)?;
        let shadow_cascades = build_shadow_cascades_from_engine_snapshot(
            &effective_snapshot,
            aspect,
            shadow_runtime.cascade_light_direction,
            shadow_runtime.max_distance_meters,
        )?;
        self.last_shadow_runtime = shadow_runtime;

        self.render(
            &frame_packet,
            &shadow_cascades,
            shadow_runtime,
            mesh_handles,
            object_handles,
            albedo_texture_handles,
            normal_texture_handles,
            material_texture_handles,
            terrain_lods,
            world_matrices,
            material_packets,
        )
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
        let mip_chain = build_rgba8_mip_chain(width, height, layers, data).map_err(js_error)?;
        let mip_level_count = mip_chain.len() as u32;
        let texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("renderer texture array"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: layers,
            },
            mip_level_count,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        for (mip_level, mip) in mip_chain.iter().enumerate() {
            let bytes_per_layer = mip.width as usize * mip.height as usize * 4;
            for layer in 0..layers {
                let layer_start = layer as usize * bytes_per_layer;
                let layer_end = layer_start + bytes_per_layer;
                self.queue.write_texture(
                    wgpu::ImageCopyTexture {
                        texture: &texture,
                        mip_level: mip_level as u32,
                        origin: wgpu::Origin3d {
                            x: 0,
                            y: 0,
                            z: layer,
                        },
                        aspect: wgpu::TextureAspect::All,
                    },
                    &mip.data[layer_start..layer_end],
                    wgpu::ImageDataLayout {
                        offset: 0,
                        bytes_per_row: Some(mip.width * 4),
                        rows_per_image: Some(mip.height),
                    },
                    wgpu::Extent3d {
                        width: mip.width,
                        height: mip.height,
                        depth_or_array_layers: 1,
                    },
                );
            }
        }
        let view = texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some("renderer texture array view"),
            dimension: Some(wgpu::TextureViewDimension::D2Array),
            base_array_layer: 0,
            array_layer_count: Some(layers),
            base_mip_level: 0,
            mip_level_count: Some(mip_level_count),
            ..Default::default()
        });

        Ok(self.textures.insert(GpuTexture { view }))
    }

    fn set_shadow_debug_view(&mut self, view: ShadowDebugView) {
        self.shadow_debug_view = view;
    }

    fn shadow_debug_view_name(&self) -> &'static str {
        self.shadow_debug_view.js_name()
    }

    fn frame_index(&self) -> u32 {
        self.frame_index
    }

    fn render_debug_options(&self) -> RenderDebugOptions {
        self.render_debug_options
    }

    fn gpu_timer_status(&self) -> GpuTimerStatus {
        self.gpu_timer_status
    }

    fn set_render_debug_options(
        &mut self,
        update: RenderDebugOptionsUpdate,
    ) -> Result<(), RenderDebugOptionsError> {
        self.render_debug_options = self.render_debug_options.apply_update(update)?;
        Ok(())
    }

    fn reset_render_debug_options(&mut self) {
        self.render_debug_options = RenderDebugOptions::default();
    }

    fn reset_perf_stats(&mut self) {
        self.last_render_counters = RenderCounterSample::default();
        self.last_gpu_pass_timings = GpuPassTimings::default();
        if let Some(gpu_timers) = &mut self.gpu_timers {
            gpu_timers.pending_readbacks.clear();
            gpu_timers.latest_timings = GpuPassTimings::default();
        }
        self.gpu_timer_status.pending_readback_count = 0;
    }

    fn latest_gpu_pass_timings(&self) -> GpuPassTimings {
        self.gpu_timers
            .as_ref()
            .map(|timers| timers.latest_timings)
            .unwrap_or_default()
    }

    fn collect_completed_gpu_readbacks(&mut self) {
        if self.gpu_timers.is_none() {
            return;
        }

        self.device.poll(wgpu::Maintain::Poll);
        let Some(gpu_timers) = &mut self.gpu_timers else {
            return;
        };
        let mut remaining = Vec::with_capacity(gpu_timers.pending_readbacks.len());
        for pending in gpu_timers.pending_readbacks.drain(..) {
            let completion = pending.completion.borrow_mut().take();
            match completion {
                Some(Ok(())) => {
                    let byte_len = u64::from(pending.query_count * wgpu::QUERY_SIZE);
                    let mapped = pending.buffer.slice(0..byte_len).get_mapped_range();
                    let timestamps = mapped
                        .chunks_exact(std::mem::size_of::<u64>())
                        .map(|chunk| {
                            let bytes: [u8; 8] = chunk.try_into().unwrap_or([0; 8]);
                            u64::from_le_bytes(bytes)
                        })
                        .collect::<Vec<_>>();
                    drop(mapped);
                    pending.buffer.unmap();
                    gpu_timers.latest_timings = GpuPassTimings::from_timestamp_pairs(
                        gpu_timers.timestamp_period_ns,
                        &timestamps,
                        &pending.pairs,
                    );
                }
                Some(Err(_error)) => {
                    pending.buffer.unmap();
                }
                None => remaining.push(pending),
            }
        }
        gpu_timers.pending_readbacks = remaining;
        self.last_gpu_pass_timings = gpu_timers.latest_timings;
        self.gpu_timer_status.pending_readback_count =
            gpu_timers.pending_readbacks.len().min(u32::MAX as usize) as u32;
    }

    fn prepare_gpu_timestamp_readback(
        &self,
        encoder: &mut wgpu::CommandEncoder,
        plan: GpuFrameTimestampPlan,
    ) -> Option<PendingGpuReadback> {
        let gpu_timers = self.gpu_timers.as_ref()?;
        if plan.next_query_index == 0 {
            return None;
        }

        let byte_len = u64::from(plan.next_query_index * wgpu::QUERY_SIZE);
        let readback_buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("perf timestamp readback buffer"),
            size: byte_len,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        encoder.resolve_query_set(
            &gpu_timers.query_set,
            0..plan.next_query_index,
            &gpu_timers.resolve_buffer,
            0,
        );
        encoder.copy_buffer_to_buffer(&gpu_timers.resolve_buffer, 0, &readback_buffer, 0, byte_len);
        Some(PendingGpuReadback {
            buffer: readback_buffer,
            query_count: plan.next_query_index,
            pairs: plan.pairs,
            completion: Rc::new(RefCell::new(None)),
        })
    }

    fn enqueue_gpu_timestamp_readback(&mut self, pending: PendingGpuReadback) {
        let Some(gpu_timers) = &mut self.gpu_timers else {
            return;
        };
        let completion = Rc::clone(&pending.completion);
        pending
            .buffer
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                *completion.borrow_mut() = Some(result.map_err(|error| error.to_string()));
            });
        gpu_timers.pending_readbacks.push(pending);
        self.gpu_timer_status.pending_readback_count =
            gpu_timers.pending_readbacks.len().min(u32::MAX as usize) as u32;
    }

    fn status(
        &self,
        terrain_update_stats: TerrainUpdateStats,
        terrain_completion_stats: TerrainCompletionStats,
        terrain_request_stats: TerrainRequestStats,
    ) -> RustBrowserGameStatus {
        let shadow_cascade_count = self
            .shadow_resources
            .layer_views
            .len()
            .min(u32::MAX as usize) as u32;
        debug_assert_eq!(shadow_cascade_count, SHADOW_CASCADE_COUNT as u32);

        RustBrowserGameStatus {
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
            frame_visible_draw_count: self.frame_visible_draw_count,
            frame_shadow_draw_count: self.frame_shadow_draw_count,
            terrain_update_total_ms: terrain_update_stats.total_ms,
            terrain_completion_ingest_ms: terrain_completion_stats.total_ms,
            terrain_worker_request_drain_ms: terrain_request_stats.total_ms,
            terrain_stream_tick_ms: terrain_update_stats.stream_tick_ms,
            terrain_stream_sync_ms: terrain_update_stats.stream_sync_ms,
            terrain_stream_scheduler_ms: terrain_update_stats.stream_scheduler_ms,
            terrain_stream_worker_queue_ms: terrain_update_stats.stream_worker_queue_ms,
            terrain_stream_visibility_ms: terrain_update_stats.stream_visibility_ms,
            terrain_stream_visibility_select_ms: terrain_update_stats.stream_visibility_select_ms,
            terrain_stream_visibility_status_ms: terrain_update_stats.stream_visibility_status_ms,
            terrain_stream_visibility_apply_ms: terrain_update_stats.stream_visibility_apply_ms,
            terrain_mesh_destroy_ms: terrain_update_stats.mesh_destroy_ms,
            terrain_mesh_upload_ms: terrain_update_stats.mesh_upload_ms,
            terrain_completion_count: terrain_completion_stats.completion_count,
            terrain_completion_accepted_count: terrain_completion_stats.accepted_count,
            terrain_completion_vertex_float_count: terrain_completion_stats.vertex_float_count,
            terrain_completion_index_count: terrain_completion_stats.index_count,
            terrain_worker_request_count: terrain_request_stats.request_count,
            terrain_update_upserted_mesh_count: terrain_update_stats.upserted_mesh_count,
            terrain_update_removed_mesh_count: terrain_update_stats.removed_mesh_count,
            terrain_update_uploaded_vertex_float_count: terrain_update_stats
                .uploaded_vertex_float_count,
            terrain_update_uploaded_index_count: terrain_update_stats.uploaded_index_count,
            terrain_update_deferred_upload_count: terrain_update_stats.deferred_upload_count,
            terrain_update_deferred_removal_count: terrain_update_stats.deferred_removal_count,
            terrain_update_upload_budget_hit: terrain_update_stats.upload_budget_hit,
            terrain_update_removal_budget_hit: terrain_update_stats.removal_budget_hit,
            shadow_cascade_count,
            shadow_map_size: SHADOW_MAP_SIZE,
            shadow_max_distance_meters: self.last_shadow_runtime.max_distance_meters,
            shadow_strength: self.last_shadow_runtime.strength,
            shadow_effective_sun_elevation: self.last_shadow_runtime.sun_elevation,
            shadow_effective_sun_direction: self.last_shadow_runtime.cascade_light_direction,
            post_process_debug_view: self.post_process_settings.debug_view(),
            post_process_exposure: self.post_process_settings.exposure(),
            post_process_tone_mapping_enabled: self.post_process_settings.tone_mapping_enabled(),
            post_process_bloom_enabled: self.post_process_settings.bloom_enabled(),
            post_process_bloom_threshold: self.post_process_settings.bloom_threshold(),
            post_process_bloom_intensity: self.post_process_settings.bloom_intensity(),
            post_process_dof_enabled: self.post_process_settings.dof_enabled(),
            post_process_dof_focus_distance: self.post_process_settings.dof_focus_distance(),
            post_process_dof_focus_range: self.post_process_settings.dof_focus_range(),
            post_process_dof_max_blur_pixels: self.post_process_settings.dof_max_blur_pixels(),
            gpu_timer_status: self.gpu_timer_status,
            render_debug_options: self.render_debug_options,
            last_render_counters: self.last_render_counters.clone(),
            last_gpu_pass_timings: self.last_gpu_pass_timings,
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

fn build_shadow_cascades_from_engine_snapshot(
    snapshot: &[f32],
    aspect: f32,
    light_direction: RenderVec3,
    max_shadow_distance: f32,
) -> Result<ShadowCascadeSet, JsValue> {
    if snapshot.len() != ENGINE_RENDER_SNAPSHOT_FLOATS {
        return Err(js_error(
            "Rust WebGPU renderer received an invalid engine render snapshot for shadows.",
        ));
    }

    build_shadow_cascades_with_max_distance(
        RenderVec3::new(snapshot[0], snapshot[1], snapshot[2]),
        RenderVec3::new(snapshot[3], snapshot[4], snapshot[5]),
        snapshot[8],
        aspect,
        snapshot[9],
        snapshot[10],
        max_shadow_distance,
        light_direction,
    )
    .ok_or_else(|| js_error("Rust WebGPU renderer could not build shadow cascades."))
}

fn shadow_runtime_state_from_engine_snapshot(
    snapshot: &[f32],
    options: RenderDebugOptions,
) -> Result<ShadowRuntimeState, JsValue> {
    if snapshot.len() != ENGINE_RENDER_SNAPSHOT_FLOATS {
        return Err(js_error(
            "Rust WebGPU renderer received an invalid engine render snapshot for shadow state.",
        ));
    }

    let production_direction = RenderVec3::new(snapshot[11], snapshot[12], snapshot[13]);
    let light_direction = shadow_sun_mode_direction(options.shadow_sun_mode, production_direction)
        .ok_or_else(|| {
            js_error("Rust WebGPU renderer received an invalid shadow sun direction.")
        })?;
    let sun_elevation = if options.shadow_sun_mode == ShadowSunMode::Production {
        snapshot[21]
    } else {
        light_direction.y
    };
    let strength = shadow_strength_for_sun_elevation(sun_elevation);
    let cascade_light_direction =
        clamp_shadow_light_direction(light_direction).ok_or_else(|| {
            js_error("Rust WebGPU renderer could not build a bounded shadow light direction.")
        })?;

    Ok(ShadowRuntimeState {
        light_direction,
        cascade_light_direction,
        sun_elevation,
        strength,
        max_distance_meters: SHADOW_MAX_DISTANCE,
    })
}

fn engine_snapshot_with_shadow_debug_light(
    snapshot: &[f32],
    options: RenderDebugOptions,
    shadow_runtime: ShadowRuntimeState,
) -> Result<[f32; ENGINE_RENDER_SNAPSHOT_FLOATS], JsValue> {
    if snapshot.len() != ENGINE_RENDER_SNAPSHOT_FLOATS {
        return Err(js_error(
            "Rust WebGPU renderer received an invalid engine render snapshot for shadow debug.",
        ));
    }

    let mut effective = [0.0; ENGINE_RENDER_SNAPSHOT_FLOATS];
    effective.copy_from_slice(snapshot);
    if options.shadow_sun_mode != ShadowSunMode::Production {
        effective[11] = shadow_runtime.light_direction.x;
        effective[12] = shadow_runtime.light_direction.y;
        effective[13] = shadow_runtime.light_direction.z;
        effective[21] = shadow_runtime.sun_elevation;
    }

    Ok(effective)
}

fn create_main_pipeline(
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
            targets: &scene_render_targets(),
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

fn create_model_pipeline(
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
        array_stride: MODEL_VERTEX_FLOATS as wgpu::BufferAddress * 4,
        step_mode: wgpu::VertexStepMode::Vertex,
        attributes: &ATTRIBUTES,
    }];

    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("static model pipeline"),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module: shader,
            entry_point: "modelVertexMain",
            compilation_options: Default::default(),
            buffers: &vertex_buffers,
        },
        fragment: Some(wgpu::FragmentState {
            module: shader,
            entry_point: "fragmentMain",
            compilation_options: Default::default(),
            targets: &scene_render_targets(),
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
            targets: &scene_render_targets(),
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            cull_mode: None,
            ..Default::default()
        },
        depth_stencil: Some(wgpu::DepthStencilState {
            format: DEPTH_FORMAT,
            depth_write_enabled: false,
            depth_compare: wgpu::CompareFunction::LessEqual,
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        multiview: None,
    })
}

fn scene_render_targets() -> [Option<wgpu::ColorTargetState>; 2] {
    [
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
    ]
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

fn create_gpu_timer_resources(device: &wgpu::Device, queue: &wgpu::Queue) -> GpuTimerResources {
    let query_set = device.create_query_set(&wgpu::QuerySetDescriptor {
        label: Some("perf timestamp query set"),
        ty: wgpu::QueryType::Timestamp,
        count: GPU_TIMESTAMP_QUERY_COUNT,
    });
    let resolve_buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("perf timestamp resolve buffer"),
        size: u64::from(GPU_TIMESTAMP_QUERY_COUNT * wgpu::QUERY_SIZE),
        usage: wgpu::BufferUsages::QUERY_RESOLVE | wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: false,
    });

    GpuTimerResources {
        query_set,
        resolve_buffer,
        timestamp_period_ns: queue.get_timestamp_period() as f64,
        pending_readbacks: Vec::new(),
        latest_timings: GpuPassTimings::default(),
    }
}

fn render_pass_timestamp_writes<'a>(
    gpu_timers: Option<&'a GpuTimerResources>,
    query_pair: Option<(u32, u32)>,
) -> Option<wgpu::RenderPassTimestampWrites<'a>> {
    let timers = gpu_timers?;
    let (beginning_of_pass_write_index, end_of_pass_write_index) = query_pair?;
    Some(wgpu::RenderPassTimestampWrites {
        query_set: &timers.query_set,
        beginning_of_pass_write_index: Some(beginning_of_pass_write_index),
        end_of_pass_write_index: Some(end_of_pass_write_index),
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

fn browser_game_input_from_js(frame: &JsValue) -> Result<BrowserGameInput, JsValue> {
    let movement = js_required_property(frame, "movement", "frame.movement")?;
    let look = js_required_property(frame, "look", "frame.look")?;

    Ok(BrowserGameInput {
        delta_seconds: js_required_f32(frame, "deltaSeconds", "frame.deltaSeconds")?,
        forward: js_required_f32(&movement, "forward", "frame.movement.forward")?,
        right: js_required_f32(&movement, "right", "frame.movement.right")?,
        up: js_required_f32(&movement, "up", "frame.movement.up")?,
        fast: js_required_bool(&movement, "fast", "frame.movement.fast")?,
        look_delta_x: js_required_f32(&look, "deltaX", "frame.look.deltaX")?,
        look_delta_y: js_required_f32(&look, "deltaY", "frame.look.deltaY")?,
    })
}

fn player_locomotion_speed_meters_per_second(input: BrowserGameInput) -> f32 {
    let horizontal_magnitude = input
        .forward
        .mul_add(input.forward, input.right * input.right)
        .sqrt()
        .min(1.0);
    if horizontal_magnitude <= 0.0 {
        return 0.0;
    }

    let speed_multiplier = if input.fast { 3.0 } else { 1.0 };
    PlayerConfig::default().move_speed * speed_multiplier * horizontal_magnitude
}

fn register_static_model_scene_item(
    renderer: &mut BrowserWgpuRenderer,
    scene_mesh_handles_by_label: &mut HashMap<String, ResourceHandle>,
    scene_material_resources_by_label: &mut HashMap<String, SceneMaterialResource>,
    model: &ModelAsset,
    mesh_label: &str,
    material_label: &str,
) -> Result<(), JsValue> {
    let primitive = model.primitives.first().ok_or_else(|| {
        js_error("Rust WebGPU renderer cannot register a static glTF model without primitives.")
    })?;
    let vertices = model_primitive_vertex_floats(primitive);
    let mesh_handle = renderer.register_mesh(&vertices, &primitive.indices, MODEL_VERTEX_FLOATS)?;
    let material = primitive
        .material
        .and_then(|material_index| model.materials.get(material_index));
    let packet = model_material_packet(material).map_err(js_error)?;
    let mut texture_handles_by_index = HashMap::new();
    let material_resource = model_scene_material_resource(
        renderer,
        model,
        primitive.material,
        packet,
        &mut texture_handles_by_index,
    )?;

    scene_mesh_handles_by_label.insert(mesh_label.to_string(), mesh_handle);
    scene_material_resources_by_label.insert(material_label.to_string(), material_resource);
    Ok(())
}

fn player_character_part_mesh_label(
    descriptor: PlayerCharacterDescriptor,
    part_index: usize,
) -> String {
    if part_index == 0 {
        descriptor.mesh_label.to_string()
    } else {
        format!("{}.primitive{part_index}.mesh", descriptor.model_id)
    }
}

fn player_character_part_material_label(
    descriptor: PlayerCharacterDescriptor,
    part_index: usize,
    material_index: Option<usize>,
) -> String {
    if part_index == 0 {
        descriptor.material_label.to_string()
    } else if let Some(material_index) = material_index {
        format!(
            "{}.primitive{part_index}.material{material_index}",
            descriptor.model_id
        )
    } else {
        format!("{}.primitive{part_index}.material", descriptor.model_id)
    }
}

fn model_scene_material_resource(
    renderer: &mut BrowserWgpuRenderer,
    model: &ModelAsset,
    material_index: Option<usize>,
    packet: [f32; MATERIAL_PACKET_FLOATS],
    texture_handles_by_index: &mut HashMap<usize, ResourceHandle>,
) -> Result<SceneMaterialResource, JsValue> {
    let material = match material_index {
        Some(index) => Some(model.materials.get(index).ok_or_else(|| {
            js_error(format!(
                "Rust WebGPU renderer cannot resolve glTF material {index}."
            ))
        })?),
        None => None,
    };
    let albedo_texture = match material.and_then(model_material_albedo_texture) {
        Some(texture) => {
            register_model_texture(renderer, model, texture.texture, texture_handles_by_index)?
        }
        None => renderer.fallback_albedo,
    };
    let normal_texture = match material.and_then(|material| material.normal_texture) {
        Some(texture) => {
            register_model_texture(renderer, model, texture.texture, texture_handles_by_index)?
        }
        None => renderer.fallback_normal,
    };
    let material_texture = match material.and_then(model_material_workflow_texture) {
        Some(texture) => {
            register_model_texture(renderer, model, texture.texture, texture_handles_by_index)?
        }
        None if material.is_some_and(model_material_is_specular_glossiness) => {
            renderer.fallback_albedo
        }
        None => renderer.fallback_material,
    };

    Ok(SceneMaterialResource {
        packet,
        albedo_texture,
        normal_texture,
        material_texture,
    })
}

fn register_model_texture(
    renderer: &mut BrowserWgpuRenderer,
    model: &ModelAsset,
    texture_index: usize,
    texture_handles_by_index: &mut HashMap<usize, ResourceHandle>,
) -> Result<ResourceHandle, JsValue> {
    if let Some(handle) = texture_handles_by_index.get(&texture_index) {
        return Ok(*handle);
    }

    let texture = decode_model_texture(model, texture_index).map_err(js_error)?;
    let handle = renderer.register_texture(
        texture.width,
        texture.height,
        1,
        TEXTURE_FORMAT_RGBA8_UNORM,
        &texture.data,
    )?;
    texture_handles_by_index.insert(texture_index, handle);
    Ok(handle)
}

fn model_material_albedo_texture(material: &ModelMaterial) -> Option<ModelTextureInfo> {
    match &material.workflow {
        ModelMaterialWorkflow::MetallicRoughness {
            base_color_texture, ..
        } => *base_color_texture,
        ModelMaterialWorkflow::SpecularGlossiness {
            diffuse_texture, ..
        } => *diffuse_texture,
    }
}

fn model_material_workflow_texture(material: &ModelMaterial) -> Option<ModelTextureInfo> {
    match &material.workflow {
        ModelMaterialWorkflow::MetallicRoughness {
            metallic_roughness_texture,
            ..
        } => *metallic_roughness_texture,
        ModelMaterialWorkflow::SpecularGlossiness {
            specular_glossiness_texture,
            ..
        } => *specular_glossiness_texture,
    }
}

fn model_material_is_specular_glossiness(material: &ModelMaterial) -> bool {
    matches!(
        material.workflow,
        ModelMaterialWorkflow::SpecularGlossiness { .. }
    )
}

fn player_animation_tuning_from_js(
    command: &JsValue,
) -> Result<PlayerCharacterLocomotionTuning, JsValue> {
    Ok(PlayerCharacterLocomotionTuning {
        walk_speed_meters_per_second: js_required_f32(
            command,
            "walkSpeedMetersPerSecond",
            "command.walkSpeedMetersPerSecond",
        )?,
        run_speed_meters_per_second: js_required_f32(
            command,
            "runSpeedMetersPerSecond",
            "command.runSpeedMetersPerSecond",
        )?,
        idle_playback_scale: js_required_f32(
            command,
            "idlePlaybackScale",
            "command.idlePlaybackScale",
        )?,
        walk_playback_scale: js_required_f32(
            command,
            "walkPlaybackScale",
            "command.walkPlaybackScale",
        )?,
        run_playback_scale: js_required_f32(
            command,
            "runPlaybackScale",
            "command.runPlaybackScale",
        )?,
    })
}

fn render_debug_options_update_from_js(
    command: &JsValue,
) -> Result<RenderDebugOptionsUpdate, JsValue> {
    let material_mode = match js_optional_string(command, "materialMode", "command.materialMode")? {
        Some(mode_name) => Some(render_material_debug_mode_from_js_name(&mode_name)?),
        None => None,
    };
    let shadow_sun_mode =
        match js_optional_string(command, "shadowSunMode", "command.shadowSunMode")? {
            Some(mode_name) => Some(shadow_sun_mode_from_js_name(&mode_name)?),
            None => None,
        };

    Ok(RenderDebugOptionsUpdate {
        terrain_lod_mask: js_optional_u32(command, "terrainLodMask", "command.terrainLodMask")?,
        sky_enabled: js_optional_bool(command, "skyEnabled", "command.skyEnabled")?,
        sky_cloud_noise_enabled: js_optional_bool(
            command,
            "skyCloudNoiseEnabled",
            "command.skyCloudNoiseEnabled",
        )?,
        shadow_pass_enabled: js_optional_bool(
            command,
            "shadowPassEnabled",
            "command.shadowPassEnabled",
        )?,
        shadow_cascade_mask: js_optional_u32(
            command,
            "shadowCascadeMask",
            "command.shadowCascadeMask",
        )?,
        shadow_sampling_enabled: js_optional_bool(
            command,
            "shadowSamplingEnabled",
            "command.shadowSamplingEnabled",
        )?,
        shadow_sun_mode,
        white_textures_enabled: js_optional_bool(
            command,
            "whiteTexturesEnabled",
            "command.whiteTexturesEnabled",
        )?,
        material_mode,
    })
}

fn render_material_debug_mode_from_js_name(
    mode_name: &str,
) -> Result<RenderMaterialDebugMode, JsValue> {
    match mode_name {
        "full" => Ok(RenderMaterialDebugMode::Full),
        "lambert" => Ok(RenderMaterialDebugMode::Lambert),
        _ => Err(js_error(format!(
            "Rust WebGPU renderer received unknown material debug mode '{mode_name}'."
        ))),
    }
}

fn shadow_sun_mode_from_js_name(mode_name: &str) -> Result<ShadowSunMode, JsValue> {
    match mode_name {
        "production" => Ok(ShadowSunMode::Production),
        "overhead" => Ok(ShadowSunMode::Overhead),
        "angled" => Ok(ShadowSunMode::Angled),
        "low" => Ok(ShadowSunMode::Low),
        _ => Err(js_error(format!(
            "Rust WebGPU renderer received unknown shadow sun mode '{mode_name}'."
        ))),
    }
}

fn js_required_property(object: &JsValue, property: &str, path: &str) -> Result<JsValue, JsValue> {
    let value = js_sys::Reflect::get(object, &JsValue::from_str(property))
        .map_err(|_| js_error(format!("Rust browser game could not read {path}.")))?;
    if value.is_null() || value.is_undefined() {
        return Err(js_error(format!("Rust browser game expected {path}.")));
    }

    Ok(value)
}

fn js_optional_property(
    object: &JsValue,
    property: &str,
    path: &str,
) -> Result<Option<JsValue>, JsValue> {
    let value = js_sys::Reflect::get(object, &JsValue::from_str(property))
        .map_err(|_| js_error(format!("Rust browser game could not read {path}.")))?;
    if value.is_null() || value.is_undefined() {
        return Ok(None);
    }

    Ok(Some(value))
}

fn js_required_f32(object: &JsValue, property: &str, path: &str) -> Result<f32, JsValue> {
    let value = js_required_property(object, property, path)?;
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite() || number < f32::MIN as f64 || number > f32::MAX as f64 {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a finite f32."
        )));
    }

    Ok(number as f32)
}

fn js_required_u32(object: &JsValue, property: &str, path: &str) -> Result<u32, JsValue> {
    let value = js_required_property(object, property, path)?;
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite() || number.fract() != 0.0 || number < 0.0 || number > u32::MAX as f64 {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a u32."
        )));
    }

    Ok(number as u32)
}

fn js_optional_u32(object: &JsValue, property: &str, path: &str) -> Result<Option<u32>, JsValue> {
    let Some(value) = js_optional_property(object, property, path)? else {
        return Ok(None);
    };
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite() || number.fract() != 0.0 || number < 0.0 || number > u32::MAX as f64 {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a u32."
        )));
    }

    Ok(Some(number as u32))
}

fn js_required_u64(object: &JsValue, property: &str, path: &str) -> Result<u64, JsValue> {
    let value = js_required_property(object, property, path)?;
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite()
        || number.fract() != 0.0
        || number < 0.0
        || number > MAX_SAFE_TERRAIN_WORKER_REQUEST_ID as f64
    {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a JavaScript safe u64."
        )));
    }

    Ok(number as u64)
}

fn js_required_i32(object: &JsValue, property: &str, path: &str) -> Result<i32, JsValue> {
    let value = js_required_property(object, property, path)?;
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite()
        || number.fract() != 0.0
        || number < i32::MIN as f64
        || number > i32::MAX as f64
    {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be an i32."
        )));
    }

    Ok(number as i32)
}

fn js_required_bool(object: &JsValue, property: &str, path: &str) -> Result<bool, JsValue> {
    let value = js_required_property(object, property, path)?;
    value.as_bool().ok_or_else(|| {
        js_error(format!(
            "Rust browser game expected {path} to be a boolean."
        ))
    })
}

fn js_optional_bool(object: &JsValue, property: &str, path: &str) -> Result<Option<bool>, JsValue> {
    let Some(value) = js_optional_property(object, property, path)? else {
        return Ok(None);
    };
    value.as_bool().map(Some).ok_or_else(|| {
        js_error(format!(
            "Rust browser game expected {path} to be a boolean."
        ))
    })
}

fn js_required_string(object: &JsValue, property: &str, path: &str) -> Result<String, JsValue> {
    let value = js_required_property(object, property, path)?;
    value
        .as_string()
        .ok_or_else(|| js_error(format!("Rust browser game expected {path} to be a string.")))
}

fn js_optional_string(
    object: &JsValue,
    property: &str,
    path: &str,
) -> Result<Option<String>, JsValue> {
    let Some(value) = js_optional_property(object, property, path)? else {
        return Ok(None);
    };
    value
        .as_string()
        .map(Some)
        .ok_or_else(|| js_error(format!("Rust browser game expected {path} to be a string.")))
}

fn set_js_property(object: &js_sys::Object, property: &str, value: JsValue) -> Result<(), JsValue> {
    js_sys::Reflect::set(object, &JsValue::from_str(property), &value)
        .map_err(|_| js_error(format!("Rust browser game could not set '{property}'.")))?;
    Ok(())
}

fn renderer_status_to_js(status: RustBrowserGameStatus) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(&object, "version", JsValue::from_f64(status.version as f64))?;
    set_js_property(&object, "runtime", JsValue::from_str("rust-wgpu"))?;
    set_js_property(&object, "configured", JsValue::from_bool(status.configured))?;
    set_js_property(
        &object,
        "canvasWidth",
        JsValue::from_f64(status.canvas_width as f64),
    )?;
    set_js_property(
        &object,
        "canvasHeight",
        JsValue::from_f64(status.canvas_height as f64),
    )?;
    set_js_property(
        &object,
        "requiredTextureArrayLayers",
        JsValue::from_f64(status.required_texture_array_layers as f64),
    )?;
    set_js_property(
        &object,
        "maxTextureArrayLayers",
        JsValue::from_f64(status.max_texture_array_layers as f64),
    )?;
    set_js_property(
        &object,
        "meshCount",
        JsValue::from_f64(status.mesh_count as f64),
    )?;
    set_js_property(
        &object,
        "textureCount",
        JsValue::from_f64(status.texture_count as f64),
    )?;
    set_js_property(
        &object,
        "objectCount",
        JsValue::from_f64(status.object_count as f64),
    )?;
    set_js_property(
        &object,
        "frameIndex",
        JsValue::from_f64(status.frame_index as f64),
    )?;
    set_js_property(
        &object,
        "frameDrawCount",
        JsValue::from_f64(status.frame_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "frameVisibleDrawCount",
        JsValue::from_f64(status.frame_visible_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "frameShadowDrawCount",
        JsValue::from_f64(status.frame_shadow_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "frameCulledDrawCount",
        JsValue::from_f64(status.last_render_counters.frame_culled_count as f64),
    )?;
    set_js_property(
        &object,
        "frameSubmittedVertexCount",
        JsValue::from_f64(status.last_render_counters.submitted_vertex_count as f64),
    )?;
    set_js_property(
        &object,
        "frameSubmittedIndexCount",
        JsValue::from_f64(status.last_render_counters.submitted_index_count as f64),
    )?;
    set_js_property(
        &object,
        "frameSubmittedTriangleCount",
        JsValue::from_f64(status.last_render_counters.submitted_triangle_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateTotalMs",
        JsValue::from_f64(status.terrain_update_total_ms),
    )?;
    set_js_property(
        &object,
        "terrainCompletionIngestMs",
        JsValue::from_f64(status.terrain_completion_ingest_ms),
    )?;
    set_js_property(
        &object,
        "terrainWorkerRequestDrainMs",
        JsValue::from_f64(status.terrain_worker_request_drain_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamTickMs",
        JsValue::from_f64(status.terrain_stream_tick_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamSyncMs",
        JsValue::from_f64(status.terrain_stream_sync_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamSchedulerMs",
        JsValue::from_f64(status.terrain_stream_scheduler_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamWorkerQueueMs",
        JsValue::from_f64(status.terrain_stream_worker_queue_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityMs",
        JsValue::from_f64(status.terrain_stream_visibility_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilitySelectMs",
        JsValue::from_f64(status.terrain_stream_visibility_select_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityStatusMs",
        JsValue::from_f64(status.terrain_stream_visibility_status_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityApplyMs",
        JsValue::from_f64(status.terrain_stream_visibility_apply_ms),
    )?;
    set_js_property(
        &object,
        "terrainMeshDestroyMs",
        JsValue::from_f64(status.terrain_mesh_destroy_ms),
    )?;
    set_js_property(
        &object,
        "terrainMeshUploadMs",
        JsValue::from_f64(status.terrain_mesh_upload_ms),
    )?;
    set_js_property(
        &object,
        "terrainCompletionCount",
        JsValue::from_f64(status.terrain_completion_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainCompletionAcceptedCount",
        JsValue::from_f64(status.terrain_completion_accepted_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainCompletionVertexFloatCount",
        JsValue::from_f64(status.terrain_completion_vertex_float_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainCompletionIndexCount",
        JsValue::from_f64(status.terrain_completion_index_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerRequestCount",
        JsValue::from_f64(status.terrain_worker_request_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateUpsertedMeshCount",
        JsValue::from_f64(status.terrain_update_upserted_mesh_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateRemovedMeshCount",
        JsValue::from_f64(status.terrain_update_removed_mesh_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateUploadedVertexFloatCount",
        JsValue::from_f64(status.terrain_update_uploaded_vertex_float_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateUploadedIndexCount",
        JsValue::from_f64(status.terrain_update_uploaded_index_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateDeferredUploadCount",
        JsValue::from_f64(status.terrain_update_deferred_upload_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateDeferredRemovalCount",
        JsValue::from_f64(status.terrain_update_deferred_removal_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainUpdateUploadBudgetHit",
        JsValue::from_bool(status.terrain_update_upload_budget_hit),
    )?;
    set_js_property(
        &object,
        "terrainUpdateRemovalBudgetHit",
        JsValue::from_bool(status.terrain_update_removal_budget_hit),
    )?;
    set_js_property(
        &object,
        "shadowCascadeCount",
        JsValue::from_f64(status.shadow_cascade_count as f64),
    )?;
    set_js_property(
        &object,
        "shadowMapSize",
        JsValue::from_f64(status.shadow_map_size as f64),
    )?;
    set_js_property(
        &object,
        "shadowMaxDistanceMeters",
        JsValue::from_f64(status.shadow_max_distance_meters as f64),
    )?;
    set_js_property(
        &object,
        "shadowStrength",
        JsValue::from_f64(status.shadow_strength as f64),
    )?;
    set_js_property(
        &object,
        "shadowEffectiveSunElevation",
        JsValue::from_f64(status.shadow_effective_sun_elevation as f64),
    )?;
    let shadow_direction = js_sys::Object::new();
    set_js_property(
        &shadow_direction,
        "x",
        JsValue::from_f64(status.shadow_effective_sun_direction.x as f64),
    )?;
    set_js_property(
        &shadow_direction,
        "y",
        JsValue::from_f64(status.shadow_effective_sun_direction.y as f64),
    )?;
    set_js_property(
        &shadow_direction,
        "z",
        JsValue::from_f64(status.shadow_effective_sun_direction.z as f64),
    )?;
    set_js_property(
        &object,
        "shadowEffectiveSunDirection",
        shadow_direction.into(),
    )?;
    set_js_property(
        &object,
        "gpuTimerAvailable",
        JsValue::from_bool(status.gpu_timer_status.available),
    )?;
    set_js_property(
        &object,
        "gpuTimerUnavailableReason",
        JsValue::from_str(status.gpu_timer_status.unavailable_reason),
    )?;
    set_js_property(
        &object,
        "gpuTimestampPeriodNs",
        JsValue::from_f64(status.gpu_timer_status.timestamp_period_ns),
    )?;
    set_js_property(
        &object,
        "gpuTimerPendingReadbackCount",
        JsValue::from_f64(status.gpu_timer_status.pending_readback_count as f64),
    )?;
    set_js_property(
        &object,
        "renderDebugOptions",
        render_debug_options_to_js(status.render_debug_options)?,
    )?;
    set_js_property(
        &object,
        "lastRenderCounters",
        render_counter_sample_to_js(&status.last_render_counters)?,
    )?;
    set_js_property(
        &object,
        "lastGpuPassTimings",
        gpu_pass_timings_to_js(status.last_gpu_pass_timings)?,
    )?;
    set_js_property(
        &object,
        "postProcessRuntime",
        JsValue::from_str("rust-wgpu"),
    )?;
    set_js_property(
        &object,
        "postProcessDebugView",
        JsValue::from_str(status.post_process_debug_view.browser_name()),
    )?;
    set_js_property(
        &object,
        "postProcessExposure",
        JsValue::from_f64(status.post_process_exposure as f64),
    )?;
    set_js_property(
        &object,
        "postProcessToneMappingEnabled",
        JsValue::from_bool(status.post_process_tone_mapping_enabled),
    )?;
    set_js_property(
        &object,
        "postProcessBloomEnabled",
        JsValue::from_bool(status.post_process_bloom_enabled),
    )?;
    set_js_property(
        &object,
        "postProcessBloomThreshold",
        JsValue::from_f64(status.post_process_bloom_threshold as f64),
    )?;
    set_js_property(
        &object,
        "postProcessBloomIntensity",
        JsValue::from_f64(status.post_process_bloom_intensity as f64),
    )?;
    set_js_property(
        &object,
        "postProcessDofEnabled",
        JsValue::from_bool(status.post_process_dof_enabled),
    )?;
    set_js_property(
        &object,
        "postProcessDofFocusDistance",
        JsValue::from_f64(status.post_process_dof_focus_distance as f64),
    )?;
    set_js_property(
        &object,
        "postProcessDofFocusRange",
        JsValue::from_f64(status.post_process_dof_focus_range as f64),
    )?;
    set_js_property(
        &object,
        "postProcessDofMaxBlurPixels",
        JsValue::from_f64(status.post_process_dof_max_blur_pixels as f64),
    )?;

    Ok(object.into())
}

fn frame_perf_report_to_js(
    report: FramePerfReport,
    gpu_timer_status: GpuTimerStatus,
) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "sampleCount",
        JsValue::from_f64(report.sample_count as f64),
    )?;
    set_js_property(
        &object,
        "capacity",
        JsValue::from_f64(report.capacity as f64),
    )?;
    set_js_property(
        &object,
        "gpuTimerStatus",
        gpu_timer_status_to_js(gpu_timer_status)?,
    )?;
    set_js_property(
        &object,
        "rustCpu",
        rust_cpu_summary_to_js(&report.rust_cpu)?,
    )?;
    set_js_property(
        &object,
        "rendererCounters",
        render_counter_summary_to_js(&report.renderer_counters)?,
    )?;
    set_js_property(&object, "gpu", gpu_pass_timing_summary_to_js(&report.gpu)?)?;
    if let Some(latest) = report.latest {
        set_js_property(&object, "latest", frame_perf_sample_to_js(&latest)?)?;
    }
    set_js_property(
        &object,
        "terrainLodCounters",
        terrain_lod_counters_to_js(&report.terrain_lod_counters).into(),
    )?;
    set_js_property(
        &object,
        "shadowCascadeCounters",
        shadow_cascade_counters_to_js(&report.shadow_cascade_counters).into(),
    )?;

    Ok(object.into())
}

fn frame_perf_sample_to_js(sample: &FramePerfSample) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "frameIndex",
        JsValue::from_f64(sample.frame_index as f64),
    )?;
    set_js_property(
        &object,
        "rustCpu",
        rust_cpu_timings_to_js(&sample.rust_cpu)?,
    )?;
    set_js_property(
        &object,
        "rendererCounters",
        render_counter_sample_to_js(&sample.renderer_counters)?,
    )?;
    set_js_property(
        &object,
        "gpuPassTimings",
        gpu_pass_timings_to_js(sample.gpu_pass_timings)?,
    )?;
    Ok(object.into())
}

fn gpu_timer_status_to_js(status: GpuTimerStatus) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(&object, "available", JsValue::from_bool(status.available))?;
    set_js_property(
        &object,
        "unavailableReason",
        JsValue::from_str(status.unavailable_reason),
    )?;
    set_js_property(
        &object,
        "timestampPeriodNs",
        JsValue::from_f64(status.timestamp_period_ns),
    )?;
    set_js_property(
        &object,
        "pendingReadbackCount",
        JsValue::from_f64(status.pending_readback_count as f64),
    )?;
    Ok(object.into())
}

fn rust_cpu_summary_to_js(summary: &crate::perf::RustCpuFrameSummary) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "totalFrameMs",
        numeric_summary_to_js(summary.total_frame_ms)?,
    )?;
    set_js_property(
        &object,
        "inputParseMs",
        numeric_summary_to_js(summary.input_parse_ms)?,
    )?;
    set_js_property(
        &object,
        "gameStateTickMs",
        numeric_summary_to_js(summary.game_state_tick_ms)?,
    )?;
    set_js_property(
        &object,
        "playerCharacterUpdateMs",
        numeric_summary_to_js(summary.player_character_update_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainCompletionIngestMs",
        numeric_summary_to_js(summary.terrain_completion_ingest_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamUpdateMs",
        numeric_summary_to_js(summary.terrain_stream_update_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamTickMs",
        numeric_summary_to_js(summary.terrain_stream_tick_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamSyncMs",
        numeric_summary_to_js(summary.terrain_stream_sync_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamSchedulerMs",
        numeric_summary_to_js(summary.terrain_stream_scheduler_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamWorkerQueueMs",
        numeric_summary_to_js(summary.terrain_stream_worker_queue_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityMs",
        numeric_summary_to_js(summary.terrain_stream_visibility_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilitySelectMs",
        numeric_summary_to_js(summary.terrain_stream_visibility_select_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityStatusMs",
        numeric_summary_to_js(summary.terrain_stream_visibility_status_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityApplyMs",
        numeric_summary_to_js(summary.terrain_stream_visibility_apply_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainMeshDestroyMs",
        numeric_summary_to_js(summary.terrain_mesh_destroy_ms)?,
    )?;
    set_js_property(
        &object,
        "terrainMeshUploadMs",
        numeric_summary_to_js(summary.terrain_mesh_upload_ms)?,
    )?;
    set_js_property(
        &object,
        "renderFrameMs",
        numeric_summary_to_js(summary.render_frame_ms)?,
    )?;
    set_js_property(
        &object,
        "renderPacketBuildMs",
        numeric_summary_to_js(summary.render_packet_build_ms)?,
    )?;
    set_js_property(
        &object,
        "rendererPrepareMs",
        numeric_summary_to_js(summary.renderer_prepare_ms)?,
    )?;
    set_js_property(
        &object,
        "rendererShadowCpuMs",
        numeric_summary_to_js(summary.renderer_shadow_cpu_ms)?,
    )?;
    set_js_property(
        &object,
        "rendererSceneCpuMs",
        numeric_summary_to_js(summary.renderer_scene_cpu_ms)?,
    )?;
    set_js_property(
        &object,
        "rendererPostCpuMs",
        numeric_summary_to_js(summary.renderer_post_cpu_ms)?,
    )?;
    set_js_property(
        &object,
        "rendererSubmitMs",
        numeric_summary_to_js(summary.renderer_submit_ms)?,
    )?;
    Ok(object.into())
}

fn rust_cpu_timings_to_js(timings: &RustCpuFrameTimings) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "totalFrameMs",
        JsValue::from_f64(timings.total_frame_ms),
    )?;
    set_js_property(
        &object,
        "inputParseMs",
        JsValue::from_f64(timings.input_parse_ms),
    )?;
    set_js_property(
        &object,
        "gameStateTickMs",
        JsValue::from_f64(timings.game_state_tick_ms),
    )?;
    set_js_property(
        &object,
        "playerCharacterUpdateMs",
        JsValue::from_f64(timings.player_character_update_ms),
    )?;
    set_js_property(
        &object,
        "terrainCompletionIngestMs",
        JsValue::from_f64(timings.terrain_completion_ingest_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamUpdateMs",
        JsValue::from_f64(timings.terrain_stream_update_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamTickMs",
        JsValue::from_f64(timings.terrain_stream_tick_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamSyncMs",
        JsValue::from_f64(timings.terrain_stream_sync_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamSchedulerMs",
        JsValue::from_f64(timings.terrain_stream_scheduler_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamWorkerQueueMs",
        JsValue::from_f64(timings.terrain_stream_worker_queue_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityMs",
        JsValue::from_f64(timings.terrain_stream_visibility_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilitySelectMs",
        JsValue::from_f64(timings.terrain_stream_visibility_select_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityStatusMs",
        JsValue::from_f64(timings.terrain_stream_visibility_status_ms),
    )?;
    set_js_property(
        &object,
        "terrainStreamVisibilityApplyMs",
        JsValue::from_f64(timings.terrain_stream_visibility_apply_ms),
    )?;
    set_js_property(
        &object,
        "terrainMeshDestroyMs",
        JsValue::from_f64(timings.terrain_mesh_destroy_ms),
    )?;
    set_js_property(
        &object,
        "terrainMeshUploadMs",
        JsValue::from_f64(timings.terrain_mesh_upload_ms),
    )?;
    set_js_property(
        &object,
        "renderFrameMs",
        JsValue::from_f64(timings.render_frame_ms),
    )?;
    set_js_property(
        &object,
        "renderPacketBuildMs",
        JsValue::from_f64(timings.render_packet_build_ms),
    )?;
    set_js_property(
        &object,
        "rendererPrepareMs",
        JsValue::from_f64(timings.renderer_prepare_ms),
    )?;
    set_js_property(
        &object,
        "rendererShadowCpuMs",
        JsValue::from_f64(timings.renderer_shadow_cpu_ms),
    )?;
    set_js_property(
        &object,
        "rendererSceneCpuMs",
        JsValue::from_f64(timings.renderer_scene_cpu_ms),
    )?;
    set_js_property(
        &object,
        "rendererPostCpuMs",
        JsValue::from_f64(timings.renderer_post_cpu_ms),
    )?;
    set_js_property(
        &object,
        "rendererSubmitMs",
        JsValue::from_f64(timings.renderer_submit_ms),
    )?;
    Ok(object.into())
}

fn render_counter_summary_to_js(summary: &RenderCounterSummary) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "frameCandidateCount",
        numeric_summary_to_js(summary.frame_candidate_count)?,
    )?;
    set_js_property(
        &object,
        "frameVisibleDrawCount",
        numeric_summary_to_js(summary.frame_visible_draw_count)?,
    )?;
    set_js_property(
        &object,
        "frameCulledCount",
        numeric_summary_to_js(summary.frame_culled_count)?,
    )?;
    set_js_property(
        &object,
        "frameShadowDrawCount",
        numeric_summary_to_js(summary.frame_shadow_draw_count)?,
    )?;
    set_js_property(
        &object,
        "terrainDrawCount",
        numeric_summary_to_js(summary.terrain_draw_count)?,
    )?;
    set_js_property(
        &object,
        "modelDrawCount",
        numeric_summary_to_js(summary.model_draw_count)?,
    )?;
    set_js_property(
        &object,
        "skyDrawCount",
        numeric_summary_to_js(summary.sky_draw_count)?,
    )?;
    set_js_property(
        &object,
        "postProcessDrawCount",
        numeric_summary_to_js(summary.post_process_draw_count)?,
    )?;
    set_js_property(
        &object,
        "submittedVertexCount",
        numeric_summary_to_js(summary.submitted_vertex_count)?,
    )?;
    set_js_property(
        &object,
        "submittedIndexCount",
        numeric_summary_to_js(summary.submitted_index_count)?,
    )?;
    set_js_property(
        &object,
        "submittedTriangleCount",
        numeric_summary_to_js(summary.submitted_triangle_count)?,
    )?;
    Ok(object.into())
}

fn render_counter_sample_to_js(sample: &RenderCounterSample) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "frameCandidateCount",
        JsValue::from_f64(sample.frame_candidate_count as f64),
    )?;
    set_js_property(
        &object,
        "frameVisibleDrawCount",
        JsValue::from_f64(sample.frame_visible_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "frameCulledCount",
        JsValue::from_f64(sample.frame_culled_count as f64),
    )?;
    set_js_property(
        &object,
        "frameShadowDrawCount",
        JsValue::from_f64(sample.frame_shadow_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainDrawCount",
        JsValue::from_f64(sample.terrain_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "modelDrawCount",
        JsValue::from_f64(sample.model_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "skyDrawCount",
        JsValue::from_f64(sample.sky_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "postProcessDrawCount",
        JsValue::from_f64(sample.post_process_draw_count as f64),
    )?;
    set_js_property(
        &object,
        "submittedVertexCount",
        JsValue::from_f64(sample.submitted_vertex_count as f64),
    )?;
    set_js_property(
        &object,
        "submittedIndexCount",
        JsValue::from_f64(sample.submitted_index_count as f64),
    )?;
    set_js_property(
        &object,
        "submittedTriangleCount",
        JsValue::from_f64(sample.submitted_triangle_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainLodCounters",
        terrain_lod_counters_to_js(&sample.terrain_lod_counters).into(),
    )?;
    set_js_property(
        &object,
        "shadowCascadeCounters",
        shadow_cascade_counters_to_js(&sample.shadow_cascade_counters).into(),
    )?;
    Ok(object.into())
}

fn gpu_pass_timing_summary_to_js(
    summary: &crate::perf::GpuPassTimingSummary,
) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    let shadow_array = js_sys::Array::new();
    for cascade_summary in summary.shadow_cascade_ms {
        shadow_array.push(&numeric_summary_to_js(cascade_summary)?);
    }
    set_js_property(&object, "shadowCascadeMs", shadow_array.into())?;
    set_js_property(&object, "sceneMs", numeric_summary_to_js(summary.scene_ms)?)?;
    set_js_property(&object, "bloomMs", numeric_summary_to_js(summary.bloom_ms)?)?;
    set_js_property(
        &object,
        "postProcessMs",
        numeric_summary_to_js(summary.post_process_ms)?,
    )?;
    set_js_property(
        &object,
        "totalMeasuredMs",
        numeric_summary_to_js(summary.total_measured_ms)?,
    )?;
    Ok(object.into())
}

fn gpu_pass_timings_to_js(timings: GpuPassTimings) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    let shadow_array = js_sys::Array::new();
    for timing in timings.shadow_cascade_ms {
        shadow_array.push(&optional_f64_to_js(timing));
    }
    set_js_property(&object, "shadowCascadeMs", shadow_array.into())?;
    set_js_property(&object, "sceneMs", optional_f64_to_js(timings.scene_ms))?;
    set_js_property(&object, "bloomMs", optional_f64_to_js(timings.bloom_ms))?;
    set_js_property(
        &object,
        "postProcessMs",
        optional_f64_to_js(timings.post_process_ms),
    )?;
    set_js_property(
        &object,
        "totalMeasuredMs",
        optional_f64_to_js(timings.total_measured_ms),
    )?;
    Ok(object.into())
}

fn terrain_lod_counters_to_js(counters: &[TerrainLodCounter]) -> js_sys::Array {
    let array = js_sys::Array::new();
    for counter in counters {
        let object = js_sys::Object::new();
        let _ = set_js_property(&object, "lod", JsValue::from_f64(counter.lod as f64));
        let _ = set_js_property(
            &object,
            "drawCount",
            JsValue::from_f64(counter.draw_count as f64),
        );
        let _ = set_js_property(
            &object,
            "vertexCount",
            JsValue::from_f64(counter.vertex_count as f64),
        );
        let _ = set_js_property(
            &object,
            "indexCount",
            JsValue::from_f64(counter.index_count as f64),
        );
        let _ = set_js_property(
            &object,
            "triangleCount",
            JsValue::from_f64(counter.triangle_count as f64),
        );
        array.push(&object);
    }
    array
}

fn shadow_cascade_counters_to_js(counters: &[ShadowCascadeCounter]) -> js_sys::Array {
    let array = js_sys::Array::new();
    for counter in counters {
        let object = js_sys::Object::new();
        let _ = set_js_property(
            &object,
            "cascadeIndex",
            JsValue::from_f64(counter.cascade_index as f64),
        );
        let _ = set_js_property(&object, "enabled", JsValue::from_bool(counter.enabled));
        let _ = set_js_property(
            &object,
            "candidateCount",
            JsValue::from_f64(counter.candidate_count as f64),
        );
        let _ = set_js_property(
            &object,
            "visibleCount",
            JsValue::from_f64(counter.visible_count as f64),
        );
        let _ = set_js_property(
            &object,
            "culledCount",
            JsValue::from_f64(counter.culled_count as f64),
        );
        let _ = set_js_property(
            &object,
            "drawCount",
            JsValue::from_f64(counter.draw_count as f64),
        );
        let _ = set_js_property(
            &object,
            "vertexCount",
            JsValue::from_f64(counter.vertex_count as f64),
        );
        let _ = set_js_property(
            &object,
            "indexCount",
            JsValue::from_f64(counter.index_count as f64),
        );
        let _ = set_js_property(
            &object,
            "triangleCount",
            JsValue::from_f64(counter.triangle_count as f64),
        );
        array.push(&object);
    }
    array
}

fn render_debug_options_to_js(options: RenderDebugOptions) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "terrainLodMask",
        JsValue::from_f64(options.terrain_lod_mask as f64),
    )?;
    set_js_property(
        &object,
        "skyEnabled",
        JsValue::from_bool(options.sky_enabled),
    )?;
    set_js_property(
        &object,
        "skyCloudNoiseEnabled",
        JsValue::from_bool(options.sky_cloud_noise_enabled),
    )?;
    set_js_property(
        &object,
        "shadowPassEnabled",
        JsValue::from_bool(options.shadow_pass_enabled),
    )?;
    set_js_property(
        &object,
        "shadowCascadeMask",
        JsValue::from_f64(options.shadow_cascade_mask as f64),
    )?;
    set_js_property(
        &object,
        "shadowSamplingEnabled",
        JsValue::from_bool(options.shadow_sampling_enabled),
    )?;
    set_js_property(
        &object,
        "shadowSunMode",
        JsValue::from_str(shadow_sun_mode_to_js_name(options.shadow_sun_mode)),
    )?;
    set_js_property(
        &object,
        "whiteTexturesEnabled",
        JsValue::from_bool(options.white_textures_enabled),
    )?;
    set_js_property(
        &object,
        "materialMode",
        JsValue::from_str(render_material_debug_mode_to_js_name(options.material_mode)),
    )?;
    Ok(object.into())
}

fn render_material_debug_mode_to_js_name(mode: RenderMaterialDebugMode) -> &'static str {
    match mode {
        RenderMaterialDebugMode::Full => "full",
        RenderMaterialDebugMode::Lambert => "lambert",
    }
}

fn shadow_sun_mode_to_js_name(mode: ShadowSunMode) -> &'static str {
    match mode {
        ShadowSunMode::Production => "production",
        ShadowSunMode::Overhead => "overhead",
        ShadowSunMode::Angled => "angled",
        ShadowSunMode::Low => "low",
    }
}

fn numeric_summary_to_js(summary: crate::perf::NumericSummary) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(&object, "latest", JsValue::from_f64(summary.latest))?;
    set_js_property(&object, "min", JsValue::from_f64(summary.min))?;
    set_js_property(&object, "max", JsValue::from_f64(summary.max))?;
    set_js_property(&object, "average", JsValue::from_f64(summary.average))?;
    set_js_property(&object, "p95", JsValue::from_f64(summary.p95))?;
    Ok(object.into())
}

fn optional_f64_to_js(value: Option<f64>) -> JsValue {
    value
        .filter(|value| value.is_finite())
        .map(JsValue::from_f64)
        .unwrap_or(JsValue::NULL)
}

#[cfg(target_arch = "wasm32")]
fn terrain_update_now_ms() -> f64 {
    perf_now_ms()
}

#[cfg(not(target_arch = "wasm32"))]
fn terrain_update_now_ms() -> f64 {
    perf_now_ms()
}

#[cfg(target_arch = "wasm32")]
fn perf_now_ms() -> f64 {
    js_sys::Date::now()
}

#[cfg(not(target_arch = "wasm32"))]
fn perf_now_ms() -> f64 {
    static START: OnceLock<Instant> = OnceLock::new();
    START.get_or_init(Instant::now).elapsed().as_secs_f64() * 1000.0
}

fn terrain_build_requests_to_js(
    requests: Vec<BrowserTerrainBuildRequest>,
) -> Result<JsValue, JsValue> {
    let array = js_sys::Array::new();
    for request in requests {
        let object = js_sys::Object::new();
        set_js_property(
            &object,
            "requestId",
            JsValue::from_f64(request.request_id as f64),
        )?;
        set_js_property(
            &object,
            "generation",
            JsValue::from_f64(request.generation as f64),
        )?;
        set_js_property(&object, "lod", JsValue::from_f64(request.key.lod as f64))?;
        set_js_property(&object, "x", JsValue::from_f64(request.key.coord.x as f64))?;
        set_js_property(&object, "y", JsValue::from_f64(request.key.coord.y as f64))?;
        set_js_property(&object, "z", JsValue::from_f64(request.key.coord.z as f64))?;
        set_js_property(&object, "seed", JsValue::from_f64(request.seed as f64))?;
        set_js_property(&object, "preset", JsValue::from_f64(request.preset as f64))?;
        set_js_property(&object, "cellSize", JsValue::from_f64(request.cell_size))?;
        array.push(&object);
    }

    Ok(array.into())
}

fn terrain_build_completion_from_js(
    value: &JsValue,
) -> Result<BrowserTerrainBuildCompletion, JsValue> {
    let request_id = js_required_u64(value, "requestId", "terrainBuild.requestId")?;
    let generation = js_required_u64(value, "generation", "terrainBuild.generation")?;
    let lod = js_required_u32(value, "lod", "terrainBuild.lod")?;
    if lod > u8::MAX as u32 {
        return Err(js_error(
            "Rust browser game expected terrainBuild.lod to fit u8.",
        ));
    }
    let key = TerrainNodeKey {
        lod: lod as u8,
        coord: TerrainChunkCoord {
            x: js_required_i32(value, "x", "terrainBuild.x")?,
            y: js_required_i32(value, "y", "terrainBuild.y")?,
            z: js_required_i32(value, "z", "terrainBuild.z")?,
        },
    };
    let failed = js_required_bool(value, "failed", "terrainBuild.failed")?;
    let vertices_value = js_required_property(value, "vertices", "terrainBuild.vertices")?;
    let indices_value = js_required_property(value, "indices", "terrainBuild.indices")?;
    let vertices = js_sys::Float32Array::new(&vertices_value).to_vec();
    let indices = js_sys::Uint32Array::new(&indices_value).to_vec();

    Ok(BrowserTerrainBuildCompletion {
        request_id,
        generation,
        key,
        vertices,
        indices,
        failed,
    })
}

fn terrain_stream_status_to_js(status: BrowserTerrainStreamStatus) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(
        &object,
        "generation",
        JsValue::from_f64(status.generation as f64),
    )?;
    set_js_property(&object, "pending", JsValue::from_bool(status.pending))?;
    set_js_property(
        &object,
        "loadedChunkCount",
        JsValue::from_f64(status.loaded_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "densityReadyChunkCount",
        JsValue::from_f64(status.density_ready_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "sharedDensityChunkCount",
        JsValue::from_f64(status.shared_density_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "inFlightDensityCount",
        JsValue::from_f64(status.in_flight_density_count as f64),
    )?;
    set_js_property(
        &object,
        "missingDensityCount",
        JsValue::from_f64(status.missing_density_count as f64),
    )?;
    set_js_property(
        &object,
        "desiredRenderChunkCount",
        JsValue::from_f64(status.desired_render_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "renderedChunkCount",
        JsValue::from_f64(status.rendered_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "emptyChunkCount",
        JsValue::from_f64(status.empty_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "inFlightChunkCount",
        JsValue::from_f64(status.in_flight_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "missingChunkCount",
        JsValue::from_f64(status.missing_chunk_count as f64),
    )?;
    set_js_property(
        &object,
        "loadedNodeCount",
        JsValue::from_f64(status.loaded_node_count as f64),
    )?;
    set_js_property(
        &object,
        "desiredRenderNodeCount",
        JsValue::from_f64(status.desired_render_node_count as f64),
    )?;
    set_js_property(
        &object,
        "renderedNodeCount",
        JsValue::from_f64(status.rendered_node_count as f64),
    )?;
    set_js_property(
        &object,
        "emptyNodeCount",
        JsValue::from_f64(status.empty_node_count as f64),
    )?;
    set_js_property(
        &object,
        "missingNodeCount",
        JsValue::from_f64(status.missing_node_count as f64),
    )?;
    set_js_property(
        &object,
        "maxRenderedLod",
        JsValue::from_f64(status.max_rendered_lod as f64),
    )?;
    set_js_property(
        &object,
        "visibleWorldSpanXMeters",
        JsValue::from_f64(status.visible_world_span_x_meters),
    )?;
    set_js_property(
        &object,
        "visibleWorldSpanZMeters",
        JsValue::from_f64(status.visible_world_span_z_meters),
    )?;
    set_js_property(
        &object,
        "terrainLodSummary",
        terrain_lod_summary_to_js(status.lod_summaries)?,
    )?;
    set_js_property(
        &object,
        "maxConcurrentChunkJobs",
        JsValue::from_f64(status.max_concurrent_chunk_jobs as f64),
    )?;
    set_js_property(
        &object,
        "workerPoolRuntime",
        JsValue::from_str(status.terrain_worker_runtime),
    )?;
    set_js_property(
        &object,
        "terrainWorkerCount",
        JsValue::from_f64(status.terrain_worker_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerInFlightCount",
        JsValue::from_f64(status.terrain_worker_in_flight_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerQueuedRequestCount",
        JsValue::from_f64(status.terrain_worker_queued_request_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerCompletedCount",
        JsValue::from_f64(status.terrain_worker_completed_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerStaleCompletionCount",
        JsValue::from_f64(status.terrain_worker_stale_completion_count as f64),
    )?;
    set_js_property(
        &object,
        "terrainWorkerFailedCount",
        JsValue::from_f64(status.terrain_worker_failed_count as f64),
    )?;
    set_js_property(
        &object,
        "synchronousBuildCount",
        JsValue::from_f64(status.synchronous_build_count as f64),
    )?;
    if let Some(stats) = status.last_density_job_stats {
        set_js_property(
            &object,
            "lastDensityJobStats",
            terrain_job_stats_to_js(stats)?,
        )?;
    }
    if let Some(stats) = status.last_chunk_job_stats {
        set_js_property(
            &object,
            "lastChunkJobStats",
            terrain_job_stats_to_js(stats)?,
        )?;
    }

    Ok(object.into())
}

fn terrain_lod_summary_to_js(
    statuses: Vec<crate::terrain_stream::BrowserTerrainLodStatus>,
) -> Result<JsValue, JsValue> {
    let array = js_sys::Array::new();
    for status in statuses {
        let object = js_sys::Object::new();
        set_js_property(&object, "lod", JsValue::from_f64(status.lod as f64))?;
        set_js_property(
            &object,
            "desiredNodeCount",
            JsValue::from_f64(status.desired_node_count as f64),
        )?;
        set_js_property(
            &object,
            "densityReadyNodeCount",
            JsValue::from_f64(status.density_ready_node_count as f64),
        )?;
        set_js_property(
            &object,
            "renderedNodeCount",
            JsValue::from_f64(status.rendered_node_count as f64),
        )?;
        set_js_property(
            &object,
            "emptyNodeCount",
            JsValue::from_f64(status.empty_node_count as f64),
        )?;
        set_js_property(
            &object,
            "missingNodeCount",
            JsValue::from_f64(status.missing_node_count as f64),
        )?;
        array.push(&object);
    }

    Ok(array.into())
}

fn terrain_job_stats_to_js(stats: TerrainJobStats) -> Result<JsValue, JsValue> {
    let object = js_sys::Object::new();
    set_js_property(&object, "totalMs", JsValue::from_f64(stats.total_ms))?;
    set_js_property(
        &object,
        "vertexCount",
        JsValue::from_f64(stats.vertex_count as f64),
    )?;
    set_js_property(
        &object,
        "indexCount",
        JsValue::from_f64(stats.index_count as f64),
    )?;

    Ok(object.into())
}

fn string_vec_to_js_array(values: Vec<String>) -> js_sys::Array {
    values
        .into_iter()
        .map(|value| JsValue::from_str(&value))
        .collect()
}

fn player_mode_to_js_name(mode: PlayerMode) -> &'static str {
    match mode {
        PlayerMode::FirstPerson => "firstPerson",
        PlayerMode::ThirdPerson => "thirdPerson",
        PlayerMode::DebugFly => "debugFly",
    }
}

fn player_mode_from_js_name(mode: &str) -> Option<PlayerMode> {
    match mode {
        "firstPerson" => Some(PlayerMode::FirstPerson),
        "thirdPerson" => Some(PlayerMode::ThirdPerson),
        "debugFly" => Some(PlayerMode::DebugFly),
        _ => None,
    }
}

fn terrain_preset_to_js_name(preset: u32) -> &'static str {
    match preset {
        0 => "seed",
        1 => "rollingHills",
        2 => "mountainValley",
        3 => "rockyHighland",
        _ => "rollingHills",
    }
}

fn sorted_terrain_node_keys(handles: &HashMap<String, ResourceHandle>) -> Vec<String> {
    let mut keys = handles.keys().cloned().collect::<Vec<_>>();
    keys.sort();
    keys
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
