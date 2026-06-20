# Build mutable render assets and an opaque draw-list renderer

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Replace the current bootstrap triangle renderer with the first reusable rendering slice for OFG. After this work, browser and native smoke should draw a simple 3D scene: a large ground plane and several cubes at different depths, with the cubes rotating and bobbing over time. The scene should be submitted through a draw list rather than hard-coded draw calls.

The renderer should use a standard asset-handle model. The `ofg_resources` crate owns mutable asset records in generic typed stores. Materials, meshes, and draw commands refer to assets by typed handles, not by `Arc` graphs and not through a giant manager object with every possible method.

The first asset types are:

Texture: pixel data loaded from image files/bytes or created from pixels, with linear or sRGB RGBA8 formats and explicit mip-map policy. Textures can update their pixels after creation and keep their WebGPU texture/view/sampler in sync for the single active device.

Shader: WGSL source plus explicit parameter and pipeline schemas. Shaders own their shader module for the active device and can be replaced during development.

Material: a shader handle plus a property bag containing named shader parameters, including texture handles. Materials own material uniform and bind-group state when GPU-ready.

Mesh: vertices, indices, and submesh ranges, where each submesh has a default material handle. Meshes can update vertices or indices after creation and keep their vertex/index buffers in sync.

PropertyBag: a shared named-value structure used by materials and draw commands. A draw command uses this for per-command properties such as `model`, with room for object id, instance tint, animation phase, or later render parameters.

This is not intended to become a full asset database yet. The goal is a concrete, mutable, testable resource interface that feels like a game engine: assets live in stores, callers hold typed handles, and the renderer consumes handles and property bags.

## Progress

- [x] (2026-06-20 07:04Z) Read C:\dev\ofg\PLANS.md, C:\dev\ofg\docs\GUIDES.md, C:\dev\ofg\docs\API_CONTRACTS.md, C:\dev\ofg\docs\SYSTEMS.md, and current renderer/runtime/smoke files.
- [x] (2026-06-20 07:04Z) Confirmed the current renderer is a single hard-coded bootstrap triangle in C:\dev\ofg\crates\ofg_render\src\renderer.rs.
- [x] (2026-06-20 07:04Z) Drafted the first implementation plan in C:\dev\ofg\docs\plans\renderer-resources-pipeline-plan.md.
- [x] (2026-06-20 07:33Z) Revised the plan to use one public Texture, Shader, Material, and Mesh type, remove bootstrap backwards-compatibility requirements, and add per-command property bags to draw commands.
- [x] (2026-06-20 07:33Z) Reviewed the plan with five sub-agents through the review-plan skill; accepted the main suggested changes around ownership, per-draw data, validation, and scope.
- [x] (2026-06-20 09:41Z) Reworked the plan around the standard asset-handle model: generic typed stores, mutable assets, one active device, GPU state held by assets, and pipeline caching kept in the renderer.
- [ ] Review and refine the plan before implementation if the user changes resource ownership, shader variant, or scope expectations.
- [ ] Milestone 1: add `ofg_resources` with generic `Store<T>`, typed handles, asset records, property bags, validation, and unit tests.
- [ ] Milestone 2: add single-device GPU state and mutation/update methods to Texture, Shader, Material, and Mesh.
- [ ] Milestone 3: replace the BootstrapRenderer path with an opaque-pass renderer that consumes DrawList and asset stores.
- [ ] Milestone 4: build the animated plane-and-cubes demo scene in Rust and integrate it into browser and native smoke.
- [ ] Milestone 5: update docs, API contracts, visual smoke contracts, screenshots, and coverage records.

## Surprises & Discoveries

- Observation: The root instructions mention GUIDES.md, but the committed guide file currently lives at C:\dev\ofg\docs\GUIDES.md.
  Evidence: Get-Content GUIDES.md failed, while C:\dev\ofg\docs\GUIDES.md exists and contains the active guiding principles.
- Observation: C:\dev\ofg\docs\plans had no active plan files before this one.
  Evidence: Get-ChildItem C:\dev\ofg\docs\plans returned no entries.
- Observation: Existing engine patterns support the asset-handle model. Bevy uses handles into `Assets<T>` collections rather than direct shared ownership of assets.
  Evidence: The Bevy docs at https://docs.rs/bevy/latest/bevy/index.html are now recorded in C:\dev\ofg\AGENTS.md as a useful Rust engine reference.

## Decision Log

- Decision: Create a new Rust crate named `ofg_resources` at C:\dev\ofg\crates\ofg_resources.
  Rationale: The user explicitly asked for a resources crate, and asset stores are a reusable boundary for future loading, editing, and scene systems.
  Date/Author: 2026-06-20 / Codex

- Decision: Use typed handles into generic stores instead of `Arc<Texture>`, `Arc<Mesh>`, bespoke stores, or one large `Resources` API.
  Rationale: Assets need to remain mutable after creation. Generic `Store<T>` gives stable typed handles, `get` and `get_mut`, without multiplying manager functions across every future asset type.
  Date/Author: 2026-06-20 / Codex

- Decision: Assume one active `wgpu::Device` for this milestone and allow assets to hold GPU state for that device.
  Rationale: OFG currently creates one browser or native smoke device. Storing GPU state on the asset is simpler than a separate prepared-resource cache. Future multi-device or device-loss support can split prepared GPU resources out later if needed.
  Date/Author: 2026-06-20 / Codex

- Decision: Do not make the device global.
  Rationale: The runtime owns `wgpu::Device` and `wgpu::Queue`; asset mutation methods receive `&Device` and `&Queue` only when GPU work is needed. This keeps dependencies explicit while avoiding global mutable state.
  Date/Author: 2026-06-20 / Codex

- Decision: Keep pipeline caching in the renderer, not in generic asset stores.
  Rationale: Render pipelines depend on shader, variant, target color/depth formats, vertex layout, primitive state, depth state, sample count, and bind group layouts. They are render-state combinations rather than standalone assets.
  Date/Author: 2026-06-20 / Codex

- Decision: Do not parse WGSL to discover parameter offsets in this milestone. Define parameter schemas explicitly beside each Shader.
  Rationale: WGSL reflection is not built into wgpu, and a hand-written parser would be fragile. Explicit schemas give deterministic name-to-offset behavior and are easy to test.
  Date/Author: 2026-06-20 / Codex

- Decision: DrawCommand includes a shared PropertyBag for per-command shader parameters rather than a dedicated transform field as the only per-draw data.
  Rationale: Transform is just the first draw-scope parameter. Reusing the material property structure keeps shader parameter population consistent for material scope and draw scope.
  Date/Author: 2026-06-20 / Codex

- Decision: Use a generated checker texture from pixels for the first visible scene, while still implementing basic image-file or image-byte import tests.
  Rationale: This proves dynamic texture creation and image import without making the smoke scene depend on external asset files.
  Date/Author: 2026-06-20 / Codex

## Outcomes & Retrospective

Implementation has not started. At completion, record whether the asset-handle model was sufficient for the animated demo, which interfaces needed simplification, which validation artifacts were produced, and whether any coverage or browser-smoke exceptions remain.

## Contract and Quality Baseline

This plan must preserve or intentionally update the active contracts in C:\dev\ofg\docs\API_CONTRACTS.md.

OFG-BOOT-001 TypeScript Host Ownership is preserved. TypeScript may keep creating the canvas, resizing it, loading WASM, and displaying errors. It must not own asset stores, mesh/material/texture/shader mutation, GPU pipeline setup, scene data, or draw submission.

OFG-BOOT-002 Rust Runtime Ownership is preserved and expanded. Rust continues to own frame state, scene data, WebGPU resources, renderer setup, and draw submission. The hard-coded bootstrap scene becomes a Rust demo scene built from mutable asset stores.

OFG-BOOT-003 WASM Facade is preserved. The browser facade should still expose create, resize, frame, debug_status_json, and dispose. The existing `frame(time_ms)` input already supplies time to Rust; this plan does not require a new TypeScript render API.

OFG-BOOT-004 Renderer Compatibility must be rewritten in the milestone that replaces BootstrapRenderer. The old shared-triangle contract should become a shared opaque-renderer contract: browser and native smoke must use the same resource crate, shader source, demo scene builder, draw-list renderer, clear color, and visual smoke expectations. Allowed differences remain the final output target and adapter/surface format.

OFG-BOOT-005 WebGPU Baseline must be intentionally updated. The old "one render pipeline for the bootstrap scene" rule no longer fits materials and shader variants. The new baseline should still request no optional GPU features and no manual limits above the existing downlevel defaults, but it may create durable pipelines, bind groups, buffers, textures, samplers, and a depth texture during initialization, explicit asset mutation, renderer construction, or resize.

OFG-BOOT-006 Resource Lifetime remains important. Shaders, buffers, textures, samplers, bind groups, and pipelines must not be recreated on ordinary frames unless a caller explicitly mutates an asset or the render target is resized/reconfigured. Ordinary frames may update frame and draw uniform buffer contents and submit draw calls.

OFG-BOOT-007 Generated Artifacts is preserved. New screenshots and local smoke output should live under C:\dev\ofg\artifacts, not in source-controlled generated directories.

OFG-BOOT-009 Coverage is preserved. Modified implementation files should meet the coverage gate unless this plan records a specific exception with rationale. Browser-only WebGPU code continues to be validated by WASM tests and browser smoke.

Quality constraints from C:\dev\ofg\docs\GUIDES.md apply: public interfaces should be documented, files approaching 500 lines should be considered for splitting, and module contracts should stay explicit.

## Context and Orientation

The repository root is C:\dev\ofg. It is a Rust workspace with a TypeScript browser host.

C:\dev\ofg\crates\ofg_render currently contains the shared bootstrap renderer. C:\dev\ofg\crates\ofg_render\src\renderer.rs creates one WGSL shader module, one empty pipeline layout, one render pipeline, and one vertex buffer for the triangle. C:\dev\ofg\crates\ofg_render\src\bootstrap_scene.rs owns the triangle vertices and clear color. C:\dev\ofg\crates\ofg_render\src\shaders\bootstrap.wgsl is the shared WGSL source.

C:\dev\ofg\crates\ofg_web\src\browser.rs owns browser WebGPU setup, surface configuration, the frame loop entrypoint, and a BootstrapRenderer instance. It reports status through C:\dev\ofg\crates\ofg_web\src\status.rs.

C:\dev\ofg\crates\ofg_test_harness\src\bin\ofg-render-frame.rs owns the native render smoke. It creates a native wgpu device, renders into an offscreen Rgba8Unorm texture, reads pixels back, writes C:\dev\ofg\artifacts\render-smoke\bootstrap.png, and writes report.json.

Definitions used in this plan:

Asset: a mutable game/render resource such as Texture, Shader, Material, or Mesh.

Handle: a small typed id into a `Store<T>`, such as `Handle<Texture>`. Handles are copied into materials, meshes, and draw commands. They are not the asset itself.

Store: a generic container that owns assets of one type and exposes `insert`, `get`, `get_mut`, and `remove`.

Assets: a lightweight aggregate of stores, such as `textures: Store<Texture>` and `meshes: Store<Mesh>`. It should not forward every resource-specific method.

GpuContext: a simple borrowed context containing the active `wgpu::Device` and `wgpu::Queue`, or equivalent method arguments. It is not global.

PropertyBag: a named collection of shader parameter values. Materials and draw commands both use it.

Draw list: an ordered or sortable collection of draw commands passed to the renderer for one frame.

Draw command: one mesh instance to render, with a mesh handle, per-command properties such as world transform, optional material override handles, and sort metadata.

Opaque pass: the first render pass for non-transparent geometry. It should clear color and depth, sort front-to-back or use an explicitly documented stable-order policy, bind each command's shader, material, mesh, and draw uniforms, then issue indexed draw calls.

Uber shader: one shader source that supports multiple compile-time or pipeline-time variants, such as textured vs. untextured material rendering.

## Plan of Work

Milestone 1 adds the `ofg_resources` crate and CPU-side asset model. Add C:\dev\ofg\crates\ofg_resources to the workspace in C:\dev\ofg\Cargo.toml. Implement `Handle<T>`, generic `Store<T>`, `Assets`, `Texture`, `Shader`, `Material`, `Mesh`, `SubMesh`, `PropertyBag`, `PropertyValue`, and validation errors. In this milestone, assets can be constructed and mutated as CPU data; GPU state can be absent or stubbed behind methods that are added in Milestone 2. This milestone should not require a GPU adapter to test.

Milestone 2 adds single-device GPU state to the asset types. Texture creation/update methods upload or rewrite texture data. Shader creation/replacement creates a shader module. Material creation/update validates its property bag against its shader and creates or refreshes material uniform and bind-group state. Mesh creation/update creates or refreshes vertex and index buffers. These methods take `&wgpu::Device` and `&wgpu::Queue` or a borrowed `GpuContext`; the device is not stored globally. Resource mutation is eager: when a method changes pixels, vertices, shader source, or material properties, it updates the related GPU state in the same call.

Milestone 3 introduces reusable renderer interfaces inside C:\dev\ofg\crates\ofg_render. Add draw_list.rs, camera.rs, opaque_renderer.rs, pipeline_cache.rs, and demo_scene.rs or similarly named modules. Replace BootstrapRenderer rather than preserving backwards compatibility. The renderer should own pass-level resources: frame uniform buffer, dynamic draw uniform arena, depth texture, and pipeline cache. The draw list should reference handles into `Assets`. Per-draw uniforms must use dynamic offsets, a per-frame uniform arena, storage-buffer indexing, or another explicit strategy that prevents all draws from seeing the last model matrix.

Milestone 4 replaces the visible bootstrap triangle with the animated demo scene. Build a ground plane mesh, a cube mesh, a generated checker texture, a basic opaque uber shader, and several draw commands whose per-command PropertyBag includes model transforms derived from frame time. Use deterministic demo constants for camera pose, FOV, near/far planes, ground size, cube sizes, cube positions, cube colors, and the native smoke frame time.

Milestone 5 updates remaining docs, smoke expectations, coverage artifacts, and screenshots. Update C:\dev\ofg\docs\API_CONTRACTS.md and C:\dev\ofg\docs\SYSTEMS.md in the milestone where each contract changes, then do a final consistency pass here for `ofg_resources`, the asset-handle model, `OpaqueRenderer`, and DemoScene. Update C:\dev\ofg\tools\smoke-contract.json and smoke pixel classification so a blank frame, missing depth, missing ground, or missing cube colors fail clearly.

After each milestone, run the repo-local milestone-review skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

From C:\dev\ofg, create the resources crate and wire it into the workspace:

    cargo new crates/ofg_resources --lib

Normalize C:\dev\ofg\crates\ofg_resources\Cargo.toml to match repo crate conventions:

    edition = "2021"
    publish = false

Edit C:\dev\ofg\Cargo.toml so workspace members include:

    "crates/ofg_resources",

Add dependencies conservatively:

    C:\dev\ofg\crates\ofg_resources\Cargo.toml:
    bytemuck = { version = "=1.25.0", features = ["derive"] }
    image = { version = "=0.25.5", default-features = false, features = ["png", "jpeg"] }
    thiserror = "=2.0.12"
    wgpu = "=29.0.3"

    C:\dev\ofg\crates\ofg_render\Cargo.toml:
    glam = "=0.29.2"
    ofg_resources = { path = "../ofg_resources" }

Milestone 1 validation:

    cargo test -p ofg_resources
    cargo check -p ofg_resources --target wasm32-unknown-unknown
    cargo test -p ofg_render
    cargo test -p ofg_web status_json_contains_browser_contract_fields

Milestone 2 validation:

    cargo test -p ofg_resources
    cargo test -p ofg_render
    cargo check -p ofg_resources --target wasm32-unknown-unknown

Milestone 3 validation:

    cargo test -p ofg_resources
    cargo test -p ofg_render
    npm run smoke:render

Milestone 4 validation:

    npm run test:rust
    npm run test:wasm
    npm run smoke:render
    npm run smoke:browser

Milestone 5 final validation:

    npm test
    npm run smoke
    npm run coverage

For browser or visual work, keep a dev server available for human review:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, use the alternate URL printed by the tool. Take and share screenshots after the first 3D render appears, after animation/material changes, and before finalizing.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the milestone-review skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Milestone 1 is accepted when `ofg_resources` exposes documented CPU-side `Handle<T>`, `Store<T>`, `Assets`, `Texture`, `Shader`, `Material`, `Mesh`, `PropertyBag`, and `PropertyValue` types. Unit tests cover typed handle allocation, stale handle rejection after removal, `get` and `get_mut`, texture format mapping, sRGB vs. linear selection, pixel-array validation, mip count or CPU mip generation, image byte/file import, shader parameter lookup, property value type validation, material property validation, draw-scope property validation, and mesh submesh range validation.

Milestone 2 is accepted when asset methods can eagerly create and update GPU state for the single active device. Unit or native integration tests should cover texture upload/update, shader module creation/replacement, material uniform/bind-group creation, mesh buffer creation/update, and that explicit asset mutation changes GPU resources while ordinary read-only frames do not recreate durable assets.

Milestone 3 is accepted when `ofg_render` exposes DrawList, DrawCommand, Camera or RenderView, OpaqueRenderer, PipelineCache, and RendererCounters or equivalent diagnostics. Tests must cover front-to-back sort order or an explicitly deferred stable-order policy, material override resolution, draw-list command validation, draw PropertyBag validation, draw-scope uniform packing, dynamic uniform-buffer offsets or the chosen equivalent per-draw data strategy, pipeline cache key separation, depth state, and cleanup of old BootstrapRenderer exports/imports. Native smoke should render through DrawList.

Milestone 4 is accepted when browser and native smoke render a large ground plane plus multiple cubes at different depths. The browser frame loop should animate cube rotation and vertical sine-wave motion. The native smoke should render a deterministic time sample, such as 1250 ms. Browser screenshots and native PNGs should visibly show the plane and cubes with depth testing.

Milestone 5 is accepted when docs and contracts describe the asset-handle resource model and renderer ownership, smoke expectations are updated, and coverage passes. The coverage command must confirm changed implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

Visual acceptance:

The first viewport should show the actual running render surface, not a marketing or explanatory page. The rendered image should not be blank. The ground plane should be visibly large relative to the cubes. At least three cubes should appear at distinct depths, with perspective and depth ordering visible. Browser screenshots should be stored under C:\dev\ofg\artifacts\browser-smoke or a clearly named subdirectory under C:\dev\ofg\artifacts.

## Idempotence and Recovery

Source edits may replace the bootstrap renderer path outright once the new opaque renderer is ready for the same milestone's tests. There is no requirement to keep BootstrapRenderer, bootstrap_scene.rs, or bootstrap.wgsl as public compatibility interfaces after the new renderer is wired in.

Generated directories C:\dev\ofg\dist, C:\dev\ofg\dist-test, C:\dev\ofg\target, C:\dev\ofg\.deploy, C:\dev\ofg\artifacts, and C:\dev\ofg\assets\wasm\ofg_web can be regenerated by the existing npm scripts. Do not manually preserve generated files as source of truth.

If a GPU smoke command fails because no adapter is available, record the adapter/environment error in Surprises & Discoveries and continue with CPU tests only if the user agrees or the environment limitation is clear. Do not weaken smoke expectations for real rendering failures.

If shader variants become too complex for WGSL override constants, fall back to a minimal deterministic variant source builder inside `ofg_resources` that only substitutes declared boolean or numeric constants at known markers. Record that decision here before implementing it.

If the single-device assumption stops holding, revise this plan before coding the affected feature. That future revision can split asset CPU data from prepared GPU resources, but that complexity is intentionally out of scope for this milestone.

## Artifacts and Notes

Expected durable implementation artifacts:

    C:\dev\ofg\crates\ofg_resources\src\lib.rs
    C:\dev\ofg\crates\ofg_resources\src\store.rs
    C:\dev\ofg\crates\ofg_resources\src\texture.rs
    C:\dev\ofg\crates\ofg_resources\src\shader.rs
    C:\dev\ofg\crates\ofg_resources\src\material.rs
    C:\dev\ofg\crates\ofg_resources\src\mesh.rs
    C:\dev\ofg\crates\ofg_resources\src\properties.rs
    C:\dev\ofg\crates\ofg_resources\src\error.rs
    C:\dev\ofg\crates\ofg_render\src\draw_list.rs
    C:\dev\ofg\crates\ofg_render\src\camera.rs
    C:\dev\ofg\crates\ofg_render\src\opaque_renderer.rs
    C:\dev\ofg\crates\ofg_render\src\pipeline_cache.rs
    C:\dev\ofg\crates\ofg_render\src\demo_scene.rs
    C:\dev\ofg\crates\ofg_render\src\shaders\opaque_uber.wgsl

Expected visual artifacts:

    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke\*.png

Record final command transcripts here in concise form as milestones complete.

## Interfaces and Dependencies

The exact names may adjust during implementation, but the final public shape after Milestone 2 should remain close to these interfaces. Milestone 1 may omit or stub GPU fields while CPU ownership and validation are established. The important constraint is that assets are mutable through typed stores, and methods that need GPU work receive the active device/queue explicitly.

Generic handles and stores:

    #[derive(Debug, PartialEq, Eq, Hash)]
    pub struct Handle<T> {
        index: u32,
        generation: u32,
        marker: std::marker::PhantomData<fn() -> T>,
    }

    impl<T> Clone for Handle<T>;
    impl<T> Copy for Handle<T>;

    pub type TextureId = Handle<Texture>;
    pub type ShaderId = Handle<Shader>;
    pub type MaterialId = Handle<Material>;
    pub type MeshId = Handle<Mesh>;

    pub struct Store<T> {
        entries: Vec<StoreEntry<T>>,
        free: Vec<u32>,
    }

    impl<T> Store<T> {
        pub fn insert(&mut self, value: T) -> Handle<T>;
        pub fn get(&self, handle: Handle<T>) -> Option<&T>;
        pub fn get_mut(&mut self, handle: Handle<T>) -> Option<&mut T>;
        pub fn remove(&mut self, handle: Handle<T>) -> Option<T>;
        pub fn len(&self) -> usize;
        pub fn is_empty(&self) -> bool;
    }

    pub struct Assets {
        pub textures: Store<Texture>,
        pub shaders: Store<Shader>,
        pub materials: Store<Material>,
        pub meshes: Store<Mesh>,
    }

GPU context:

    pub struct GpuContext<'a> {
        pub device: &'a wgpu::Device,
        pub queue: &'a wgpu::Queue,
    }

Shared property data:

    pub enum PropertyValue {
        Float(f32),
        Vec2([f32; 2]),
        Vec3([f32; 3]),
        Vec4([f32; 4]),
        Mat4([[f32; 4]; 4]),
        Texture(TextureId),
    }

    pub struct PropertyBag {
        values: std::collections::BTreeMap<String, PropertyValue>,
    }

    impl PropertyBag {
        pub fn new() -> Self;
        pub fn set(&mut self, name: impl Into<String>, value: PropertyValue);
        pub fn get(&self, name: &str) -> Option<&PropertyValue>;
        pub fn validate_for_scope(&self, shader: &Shader, scope: ShaderParameterScope) -> Result<(), ResourceError>;
        pub fn pack_uniforms_for_scope(&self, shader: &Shader, scope: ShaderParameterScope) -> Result<Vec<u8>, ResourceError>;
    }

Texture resource:

    pub enum TextureColorSpace {
        Srgb,
        Linear,
    }

    pub enum TexturePixelFormat {
        Rgba8,
        Rgba8Srgb,
    }

    pub enum MipMapPolicy {
        None,
        GenerateCpuFullChain,
        Explicit(u32),
    }

    pub struct Texture {
        label: String,
        width: u32,
        height: u32,
        format: TexturePixelFormat,
        color_space: TextureColorSpace,
        mip_map_policy: MipMapPolicy,
        pixels: Vec<u8>,
        mip_levels: Vec<TextureMipLevel>,
        gpu: TextureGpuState,
    }

    impl Texture {
        pub fn from_rgba8_pixels(gpu: &GpuContext<'_>, label: impl Into<String>, width: u32, height: u32, color_space: TextureColorSpace, pixels: Vec<u8>, mip_map_policy: MipMapPolicy) -> Result<Self, ResourceError>;
        pub fn from_image_bytes(gpu: &GpuContext<'_>, label: impl Into<String>, bytes: &[u8], color_space: TextureColorSpace, mip_map_policy: MipMapPolicy) -> Result<Self, ResourceError>;
        #[cfg(not(target_arch = "wasm32"))]
        pub fn from_image_file(gpu: &GpuContext<'_>, path: impl AsRef<std::path::Path>, color_space: TextureColorSpace, mip_map_policy: MipMapPolicy) -> Result<Self, ResourceError>;
        pub fn update_pixels(&mut self, gpu: &GpuContext<'_>, pixels: Vec<u8>) -> Result<(), ResourceError>;
        pub fn view(&self) -> &wgpu::TextureView;
        pub fn sampler(&self) -> &wgpu::Sampler;
    }

Shader and material interfaces:

    pub struct Shader {
        label: String,
        wgsl_source: String,
        parameter_layout: ShaderParameterLayout,
        pipelines: Vec<PipelineDefinition>,
        module: wgpu::ShaderModule,
        revision: u64,
    }

    impl Shader {
        pub fn new(gpu: &GpuContext<'_>, label: impl Into<String>, wgsl_source: impl Into<String>, parameter_layout: ShaderParameterLayout, pipelines: Vec<PipelineDefinition>) -> Result<Self, ResourceError>;
        pub fn replace_source(&mut self, gpu: &GpuContext<'_>, wgsl_source: impl Into<String>) -> Result<(), ResourceError>;
        pub fn parameter(&self, name: &str) -> Option<&ShaderParameter>;
        pub fn module(&self) -> &wgpu::ShaderModule;
        pub fn revision(&self) -> u64;
    }

    pub struct Material {
        label: String,
        shader: ShaderId,
        properties: PropertyBag,
        variant_key: ShaderVariantKey,
        uniform_buffer: wgpu::Buffer,
        bind_group: wgpu::BindGroup,
        revision: u64,
    }

    impl Material {
        pub fn new(gpu: &GpuContext<'_>, shaders: &Store<Shader>, textures: &Store<Texture>, label: impl Into<String>, shader: ShaderId, properties: PropertyBag) -> Result<Self, ResourceError>;
        pub fn set_property(&mut self, gpu: &GpuContext<'_>, shaders: &Store<Shader>, textures: &Store<Texture>, name: impl Into<String>, value: PropertyValue) -> Result<(), ResourceError>;
        pub fn shader(&self) -> ShaderId;
        pub fn bind_group(&self) -> &wgpu::BindGroup;
        pub fn revision(&self) -> u64;
    }

Mesh interface:

    #[repr(C)]
    pub struct MeshVertex {
        pub position: [f32; 3],
        pub normal: [f32; 3],
        pub uv: [f32; 2],
    }

    pub struct SubMesh {
        pub label: String,
        pub index_start: u32,
        pub index_count: u32,
        pub default_material: MaterialId,
    }

    pub struct Mesh {
        label: String,
        vertices: Vec<MeshVertex>,
        indices: Vec<u32>,
        submeshes: Vec<SubMesh>,
        vertex_buffer: wgpu::Buffer,
        index_buffer: wgpu::Buffer,
        revision: u64,
    }

    impl Mesh {
        pub fn new(gpu: &GpuContext<'_>, label: impl Into<String>, vertices: Vec<MeshVertex>, indices: Vec<u32>, submeshes: Vec<SubMesh>) -> Result<Self, ResourceError>;
        pub fn replace_vertices(&mut self, gpu: &GpuContext<'_>, vertices: Vec<MeshVertex>) -> Result<(), ResourceError>;
        pub fn replace_indices(&mut self, gpu: &GpuContext<'_>, indices: Vec<u32>, submeshes: Vec<SubMesh>) -> Result<(), ResourceError>;
        pub fn vertex_buffer(&self) -> &wgpu::Buffer;
        pub fn index_buffer(&self) -> &wgpu::Buffer;
        pub fn submeshes(&self) -> &[SubMesh];
        pub fn revision(&self) -> u64;
    }

Renderer interface:

    pub struct DrawCommand {
        pub mesh: MeshId,
        pub properties: PropertyBag,
        pub material_overrides: Vec<MaterialOverride>,
        pub sort_origin: glam::Vec3,
    }

    pub enum MaterialOverride {
        All(MaterialId),
        SubMesh { index: usize, material: MaterialId },
    }

    pub struct OpaqueRenderer {
        pipeline_cache: PipelineCache,
        frame_uniform_buffer: wgpu::Buffer,
        draw_uniform_arena: DynamicUniformArena,
        depth_texture: Option<wgpu::Texture>,
        counters: RendererCounters,
    }

    impl OpaqueRenderer {
        pub fn new(device: &wgpu::Device, color_format: wgpu::TextureFormat) -> Self;
        pub fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32);
        pub fn render_to_view(&mut self, gpu: &GpuContext<'_>, encoder: &mut wgpu::CommandEncoder, target: &wgpu::TextureView, assets: &Assets, view: &RenderView, draw_list: &DrawList) -> Result<(), RenderError>;
        pub fn counters(&self) -> RendererCounters;
    }

Initial shader parameters:

Frame scope, bind group 0:

    view_projection: mat4x4<f32>

Draw scope, bind group 1:

    model: mat4x4<f32>

Material scope, bind group 2:

    base_color_factor: vec4<f32>
    base_color_texture: texture_2d<f32>
    base_color_sampler: sampler derived from base_color_texture

Initial shader variants:

    ShaderVariantKey { base_color_texture: false }
    ShaderVariantKey { base_color_texture: true }

The second variant should be used by the generated checker ground material. The first variant can be used by simple colored cube materials.
