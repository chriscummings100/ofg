// Browser-facing Rust/wgpu game facade. It owns WebGPU resources, terrain draw
// submission, and render-facing GLTF model resources for the playable browser path.
use std::borrow::Cow;
use std::collections::HashMap;

use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use wgpu::util::DeviceExt;

use crate::config::{
    MODEL_VERTEX_FLOATS, REQUIRED_TEXTURE_ARRAY_LAYERS, TERRAIN_VERTEX_FLOATS,
    TEXTURE_FORMAT_RGBA8_UNORM,
};
use crate::game_state::{BrowserGameInput, BrowserGameState};
use crate::materials::TERRAIN_MATERIAL_PACKET;
use crate::model_asset_loader::load_model_asset_bytes;
use crate::model_assets::{
    import_gltf_model_from_slice, PLAYER_QUATERNIUS_UAL2_MATERIAL_LABEL,
    PLAYER_QUATERNIUS_UAL2_MESH_LABEL, PLAYER_QUATERNIUS_UAL2_MODEL_ID,
    PLAYER_QUATERNIUS_UAL2_MODEL_URL,
};
use crate::model_locomotion::PlayerCharacterModel;
use crate::render_packets::build_frame_packet_from_engine_snapshot;
use crate::render_uniforms::{
    build_frame_uniform_values, build_object_uniform_values, FRAME_PACKET_FLOATS,
    FRAME_UNIFORM_FLOATS, MATERIAL_PACKET_FLOATS, OBJECT_UNIFORM_FLOATS, WORLD_MATRIX_FLOATS,
};
use crate::resources::{ResourceHandle, ResourceStore};
use crate::terrain_stream::{BrowserTerrainStream, BrowserTerrainStreamStatus, TerrainJobStats};
use crate::terrain_textures::{load_terrain_texture_arrays, TerrainTextureArrays};
use crate::ENGINE_WEB_VERSION;
use engine_core::{PlayerMode, Vec3};
use terrain_core::{terrain_chunk_key, TerrainChunkCoord, DEFAULT_TERRAIN_PRESET};

const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth24Plus;
const SHADER_SOURCE: &str = include_str!("../../../src/engine/render/shaders/uber.wgsl");

#[wasm_bindgen]
pub struct RustBrowserGame {
    game_state: BrowserGameState,
    terrain_stream: BrowserTerrainStream,
    renderer: BrowserWgpuRenderer,
    terrain_mesh_handles_by_key: HashMap<String, ResourceHandle>,
    terrain_textures: Option<TerrainTextureHandles>,
    object_handles_by_id: HashMap<String, ResourceHandle>,
    scene_mesh_handles_by_label: HashMap<String, ResourceHandle>,
    scene_material_packets_by_label: HashMap<String, [f32; MATERIAL_PACKET_FLOATS]>,
    player_character: PlayerCharacterModel,
    model_skinning_runtime: Option<&'static str>,
    model_skinning_joint_count: usize,
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
    model_pipeline: wgpu::RenderPipeline,
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
    vertex_float_count: usize,
    vertex_layout: MeshVertexLayout,
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
struct TerrainTextureHandles {
    albedo: ResourceHandle,
    normal: ResourceHandle,
    material: ResourceHandle,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MeshVertexLayout {
    Terrain,
    Model,
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

const IDENTITY_WORLD_MATRIX: [f32; WORLD_MATRIX_FLOATS] = [
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
];
const PLAYER_CHARACTER_SCENE_SCALE: f32 = 1.0;
const PLAYER_CHARACTER_HEIGHT_OFFSET: f32 = 0.0;

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
        let player_model_bytes = load_model_asset_bytes(
            &asset_loader,
            PLAYER_QUATERNIUS_UAL2_MODEL_ID,
            PLAYER_QUATERNIUS_UAL2_MODEL_URL,
        )
        .await?;
        let player_model = import_gltf_model_from_slice(&player_model_bytes).map_err(js_error)?;
        let player_character = PlayerCharacterModel::from_model(player_model).map_err(js_error)?;
        let player_character_vertices = player_character.current_vertices().map_err(js_error)?;
        let player_character_mesh = renderer.register_mesh(
            &player_character_vertices,
            player_character.indices(),
            MODEL_VERTEX_FLOATS,
        )?;

        let mut scene_mesh_handles_by_label = HashMap::new();
        scene_mesh_handles_by_label.insert(
            PLAYER_QUATERNIUS_UAL2_MESH_LABEL.to_string(),
            player_character_mesh,
        );

        let mut scene_material_packets_by_label = HashMap::new();
        scene_material_packets_by_label.insert(
            PLAYER_QUATERNIUS_UAL2_MATERIAL_LABEL.to_string(),
            player_character.material_packet(),
        );

        let mut game_state = BrowserGameState::new();
        game_state
            .configure_player_character_scene(
                PLAYER_QUATERNIUS_UAL2_MESH_LABEL,
                PLAYER_QUATERNIUS_UAL2_MATERIAL_LABEL,
                PLAYER_CHARACTER_SCENE_SCALE,
                PLAYER_CHARACTER_HEIGHT_OFFSET,
            )
            .map_err(js_error)?;
        let model_skinning_joint_count = player_character.skin_joint_count();

        let mut game = Self {
            game_state,
            terrain_stream: BrowserTerrainStream::new(0, DEFAULT_TERRAIN_PRESET)
                .map_err(js_error)?,
            renderer,
            terrain_mesh_handles_by_key: HashMap::new(),
            terrain_textures: None,
            object_handles_by_id: HashMap::new(),
            scene_mesh_handles_by_label,
            scene_material_packets_by_label,
            player_character,
            model_skinning_runtime: Some("rust-cpu"),
            model_skinning_joint_count,
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
        let input = browser_game_input_from_js(&frame)?;
        self.game_state.tick(input).map_err(js_error)?;
        self.update_player_character_mesh(input)?;
        self.update_terrain_stream()?;
        self.render_frame()
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
            "terrainChunkKeys",
            string_vec_to_js_array(self.terrain_stream.render_chunk_keys()).into(),
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
            terrain_stream_status_to_js(self.terrain_stream.status())?,
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
            JsValue::from_str("rust"),
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
            renderer_status_to_js(self.renderer.status())?,
        )?;
        set_js_property(
            &snapshot,
            "terrainWorkerCount",
            JsValue::from_f64(self.terrain_stream.worker_count() as f64),
        )?;
        set_js_property(
            &snapshot,
            "playerControllerRuntime",
            JsValue::from_str("rust"),
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
        let animation = self.player_character.animation_snapshot();
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
        if let Some(runtime) = self.model_skinning_runtime {
            set_js_property(
                &snapshot,
                "modelSkinningRuntime",
                JsValue::from_str(runtime),
            )?;
            set_js_property(
                &snapshot,
                "modelSkinningJointCount",
                JsValue::from_f64(self.model_skinning_joint_count as f64),
            )?;
        }

        Ok(snapshot.into())
    }

    fn render_frame(&mut self) -> Result<(), JsValue> {
        let engine_snapshot = self.game_state.render_snapshot_values().map_err(js_error)?;
        let scene_mesh_items = self.game_state.render_mesh_items().map_err(js_error)?;
        let aspect = self.renderer.aspect_ratio();
        let chunk_keys = sorted_terrain_chunk_keys(&self.terrain_mesh_handles_by_key);
        let chunk_count = chunk_keys.len();
        let item_count = chunk_count + scene_mesh_items.len();

        let mut mesh_handles = Vec::with_capacity(item_count);
        let mut object_handles = Vec::with_capacity(item_count);
        let mut albedo_texture_handles = Vec::with_capacity(item_count);
        let mut normal_texture_handles = Vec::with_capacity(item_count);
        let mut material_texture_handles = Vec::with_capacity(item_count);
        let mut world_matrices = Vec::with_capacity(item_count * WORLD_MATRIX_FLOATS);
        let mut material_packets = Vec::with_capacity(item_count * MATERIAL_PACKET_FLOATS);
        let terrain_textures = self.terrain_textures.unwrap_or(TerrainTextureHandles {
            albedo: self.renderer.fallback_albedo,
            normal: self.renderer.fallback_normal,
            material: self.renderer.fallback_material,
        });

        for index in 0..chunk_count {
            let chunk_key = &chunk_keys[index];
            let mesh_handle =
                *self
                    .terrain_mesh_handles_by_key
                    .get(chunk_key)
                    .ok_or_else(|| {
                        js_error(format!(
                            "Rust browser game is missing terrain mesh '{chunk_key}'."
                        ))
                    })?;
            let object_handle = self.object_handle_for_id(chunk_key)?;

            mesh_handles.push(handle_to_js(mesh_handle));
            object_handles.push(handle_to_js(object_handle));
            albedo_texture_handles.push(handle_to_js(terrain_textures.albedo));
            normal_texture_handles.push(handle_to_js(terrain_textures.normal));
            material_texture_handles.push(handle_to_js(terrain_textures.material));
            world_matrices.extend_from_slice(&IDENTITY_WORLD_MATRIX);
            material_packets.extend_from_slice(&TERRAIN_MATERIAL_PACKET);
        }

        for item in scene_mesh_items {
            let mesh_handle = self.scene_mesh_handle(&item.mesh_label)?;
            let material_packet = self.scene_material_packet(&item.material_label)?;
            let object_handle =
                self.object_handle_for_id(&format!("entity:{}", item.entity.to_raw()))?;

            mesh_handles.push(handle_to_js(mesh_handle));
            object_handles.push(handle_to_js(object_handle));
            albedo_texture_handles.push(handle_to_js(self.renderer.fallback_albedo));
            normal_texture_handles.push(handle_to_js(self.renderer.fallback_normal));
            material_texture_handles.push(handle_to_js(self.renderer.fallback_material));
            world_matrices.extend_from_slice(&item.world_matrix);
            material_packets.extend_from_slice(&material_packet);
        }

        self.renderer.render_engine_frame(
            &engine_snapshot,
            aspect,
            &mesh_handles,
            &object_handles,
            &albedo_texture_handles,
            &normal_texture_handles,
            &material_texture_handles,
            &world_matrices,
            &material_packets,
        )?;
        Ok(())
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

    fn scene_material_packet(&self, label: &str) -> Result<[f32; MATERIAL_PACKET_FLOATS], JsValue> {
        self.scene_material_packets_by_label
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

    fn update_player_character_mesh(&mut self, input: BrowserGameInput) -> Result<(), JsValue> {
        let vertices = self
            .player_character
            .tick_vertices(input.delta_seconds, [input.forward, input.right])
            .map_err(js_error)?;
        let mesh_handle = self.scene_mesh_handle(PLAYER_QUATERNIUS_UAL2_MESH_LABEL)?;
        self.renderer
            .update_mesh_vertices(mesh_handle, &vertices, MODEL_VERTEX_FLOATS)
    }

    fn update_terrain_stream(&mut self) -> Result<(), JsValue> {
        let player_position = self.game_state.player_position().map_err(js_error)?;
        let update = self.terrain_stream.tick(player_position);

        for coord in update.removed_coords {
            self.destroy_terrain_mesh(coord)?;
        }

        for mesh_update in update.upserted_meshes {
            self.upsert_terrain_mesh(
                mesh_update.coord,
                &mesh_update.mesh.vertices,
                &mesh_update.mesh.indices,
            )?;
        }

        Ok(())
    }

    fn upsert_terrain_mesh(
        &mut self,
        coord: TerrainChunkCoord,
        vertices: &[f32],
        indices: &[u32],
    ) -> Result<(), JsValue> {
        let chunk_key = terrain_chunk_key(coord);
        if let Some(handle) = self.terrain_mesh_handles_by_key.remove(&chunk_key) {
            self.renderer.destroy_mesh(handle)?;
        }

        let handle = self
            .renderer
            .register_mesh(vertices, indices, TERRAIN_VERTEX_FLOATS)?;
        self.terrain_mesh_handles_by_key.insert(chunk_key, handle);
        Ok(())
    }

    fn destroy_terrain_mesh(&mut self, coord: TerrainChunkCoord) -> Result<(), JsValue> {
        self.destroy_terrain_mesh_by_key(&terrain_chunk_key(coord))
    }

    fn destroy_terrain_mesh_by_key(&mut self, chunk_key: &str) -> Result<(), JsValue> {
        let Some(handle) = self.terrain_mesh_handles_by_key.remove(chunk_key) else {
            return Ok(());
        };
        self.renderer.destroy_mesh(handle)?;

        if let Some(object_handle) = self.object_handles_by_id.remove(chunk_key) {
            self.renderer.destroy_object(object_handle)?;
        }

        Ok(())
    }

    fn clear_terrain_meshes(&mut self) -> Result<(), JsValue> {
        let chunk_keys = sorted_terrain_chunk_keys(&self.terrain_mesh_handles_by_key);
        for chunk_key in chunk_keys {
            self.destroy_terrain_mesh_by_key(&chunk_key)?;
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
        let model_pipeline = create_model_pipeline(&device, &pipeline_layout, &shader, format);
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
            model_pipeline,
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

        let mesh = self
            .meshes
            .get(handle)
            .ok_or_else(|| js_error("Rust WebGPU renderer rejected a stale mesh handle."))?;
        if mesh.vertex_layout != vertex_layout || mesh.vertex_float_count != vertices.len() {
            return Err(js_error(
                "Rust WebGPU renderer rejected a mismatched mesh vertex update.",
            ));
        }

        self.queue
            .write_buffer(&mesh.vertex_buffer, 0, f32_as_bytes(vertices));
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

            let mut active_vertex_layout = None;
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
            }
        }
        self.queue.submit(Some(encoder.finish()));
        frame.present();
        self.frame_index = self.frame_index.saturating_add(1);
        self.frame_draw_count = item_count as u32;
        Ok(())
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
        world_matrices: &[f32],
        material_packets: &[f32],
    ) -> Result<(), JsValue> {
        let frame_packet =
            build_frame_packet_from_engine_snapshot(engine_snapshot, aspect).map_err(js_error)?;

        self.render(
            &frame_packet,
            mesh_handles,
            object_handles,
            albedo_texture_handles,
            normal_texture_handles,
            material_texture_handles,
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

    fn status(&self) -> RustBrowserGameStatus {
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

fn create_model_pipeline(
    device: &wgpu::Device,
    layout: &wgpu::PipelineLayout,
    shader: &wgpu::ShaderModule,
    format: wgpu::TextureFormat,
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

fn js_required_property(object: &JsValue, property: &str, path: &str) -> Result<JsValue, JsValue> {
    let value = js_sys::Reflect::get(object, &JsValue::from_str(property))
        .map_err(|_| js_error(format!("Rust browser game could not read {path}.")))?;
    if value.is_null() || value.is_undefined() {
        return Err(js_error(format!("Rust browser game expected {path}.")));
    }

    Ok(value)
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

fn js_required_bool(object: &JsValue, property: &str, path: &str) -> Result<bool, JsValue> {
    let value = js_required_property(object, property, path)?;
    value.as_bool().ok_or_else(|| {
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

    Ok(object.into())
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
        "maxConcurrentChunkJobs",
        JsValue::from_f64(status.max_concurrent_chunk_jobs as f64),
    )?;
    set_js_property(&object, "workerPoolRuntime", JsValue::from_str("rust"))?;
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

fn sorted_terrain_chunk_keys(handles: &HashMap<String, ResourceHandle>) -> Vec<String> {
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
