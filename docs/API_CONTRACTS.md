# API Contracts

This document is the living source of truth for API contracts between OFG
systems. It describes the supported boundaries that future milestones must
preserve, the unsupported surfaces that currently leak through generated
artifacts, and the known contract risks reviewers should watch.

The completed Rust conversion plan is archived at
`docs/archived/RUST_CONVERSION_PLAN.md`. Use that file for historical migration
context only; use this document and `docs/ARCHITECTURE.md` for current boundary
decisions.

## Status Terms

- Active: supported by the current playable browser runtime.
- Fixture: kept for tests, benchmarks, or compatibility checks, not the playable
  runtime path.
- Unsupported: visible in generated output or source but not a supported app
  boundary.
- Future: intended shape once the related feature exists.
- Forbidden: must not be reintroduced without an accepted replacement plan.

## Contract Index

| ID | Boundary | Status | Source of truth |
|---|---|---|---|
| OFG-API-001 | Browser shell to Rust browser game | Active | `src/engine/web/browserGameTypes.ts`, `src/engine/web/engineWebWasm.ts`, `crates/engine_web/src/perf.rs`, `crates/engine_web/src/wgpu_renderer.rs`, `crates/engine_web/src/post_process.rs` |
| OFG-API-002 | Rust browser game to browser asset loader | Active | `src/engine/browser/textureAssetLoader.ts`, `crates/engine_web/src/terrain_textures.rs` |
| OFG-API-003 | Debug and smoke-test hooks | Active | `src/app/game.ts`, `src/app/perfDebug.ts`, `src/engine/web/browserGameTypes.ts`, `tools/browser-smoke.mjs`, `tools/browser-perf-debug-capture.mjs` |
| OFG-API-004 | Terrain vertex and material layout | Active | `crates/terrain_core/src/constants.rs`, `crates/engine_web/src/config.rs`, `crates/engine_web/src/wgpu_renderer.rs`, `src/engine/render/shaders/UberShader.test.ts`, `src/engine/render/shaders/PostShader.test.ts` |
| OFG-API-005 | Terrain presets and world descriptor codes | Active | `src/engine/world/terrainDescriptor.ts`, `src/engine/web/rustBrowserGameRuntime.ts`, `crates/terrain_core/src/presets.rs` |
| OFG-API-006 | Standalone `terrain_core.wasm` artifact | Fixture | `tools/build-terrain-wasm.mjs`, `crates/terrain_core/src/facade.rs` |
| OFG-API-007 | Raw linked WASM exports in `engine_web` | Unsupported | `assets/wasm/engine_web/engine_web.d.ts`, `crates/*/src/facade.rs` |
| OFG-API-008 | Future game lifecycle and tuning surface | Future | This document until real behavior exists |
| OFG-API-009 | Forbidden TypeScript ownership | Forbidden | This document and `docs/ARCHITECTURE.md` |
| OFG-API-010 | GLTF model, animation, and skinning loading | Active | `docs/archived/GLTF_CHARACTER_PLAN.md`, `crates/engine_web/src/model_assets.rs`, `crates/engine_web/src/model_animation.rs`, `crates/engine_web/src/model_skinning.rs`, `crates/engine_web/src/model_render_assets.rs`, `crates/engine_web/src/wgpu_renderer.rs` |

## OFG-API-001: Browser Shell To Rust Browser Game

The supported browser runtime API is the `RustBrowserGame` class loaded through
`src/engine/web/engineWebWasm.ts`. Browser app code must go through the
TypeScript wrapper and runtime facade, not raw wasm exports.

Current supported facade:

    RustBrowserGame.create(canvas, assetLoader)
    game.resize(viewport)
    game.tick(frame)
    game.command(command)
    game.debugSnapshot()

`create(canvas, assetLoader)` initializes the Rust-owned game, terrain stream,
texture requests, renderer, player state, and debug marker state. The
TypeScript shell supplies the canvas and a generic asset loader.

`resize(viewport)` forwards browser canvas dimensions:

    export type BrowserViewport = {
      readonly width: number;
      readonly height: number;
    };

`tick(frame)` is the only normal per-frame call. It advances player/camera
state, advances Rust terrain streaming, uploads/prunes terrain meshes, and
submits rendering.

The wasm-bindgen game object also exposes internal terrain worker methods used
only by `RustBrowserGameAdapter`: `configureTerrainWorkers(options)`,
`takeTerrainBuildRequests()`, and `completeTerrainBuilds(completions)`.
Rust owns the request ids, generation numbers, node keys, retry semantics, and
completion validation. TypeScript may route these opaque packets through browser
workers, but it must not decide desired terrain, LOD visibility, fallback cover,
or whether a returned mesh is current.

    export type BrowserFrameInput = {
      readonly deltaSeconds: number;
      readonly movement: {
        readonly forward: number;
        readonly right: number;
        readonly up: number;
        readonly fast: boolean;
      };
      readonly look: {
        readonly deltaX: number;
        readonly deltaY: number;
      };
    };

`command(command)` is the single control lane for browser UI, debug hooks, and
smoke tests. Current commands are:

    { type: "togglePlayerMode" }
    { type: "setPlayerMode", mode: "firstPerson" | "thirdPerson" | "debugFly" }
    { type: "togglePlayerCharacter" }
    { type: "setPlayerCharacter", character: "male" | "female" }
    { type: "setPlayerAnimationTuning", walkSpeedMetersPerSecond,
      runSpeedMetersPerSecond, idlePlaybackScale, walkPlaybackScale,
      runPlaybackScale }
    { type: "setPlayerPosition", x, y?, z }
    { type: "setDebugCamera", x, y, z, yaw, pitch }
    { type: "setPostProcessDebugView", view: "final" | "sceneColor" |
      "linearDepth" | "postToneMap" | "bloom" | "dofCoc" |
      "dofBlurred" }
    { type: "setPostProcessToneMapping", enabled, exposure }
    { type: "setPostProcessBloom", enabled, threshold, intensity }
    { type: "setPostProcessDepthOfField", enabled, focusDistance,
      focusRange, maxBlurPixels }
    { type: "setRenderDebugOptions", terrainLodMask?, skyEnabled?,
      skyCloudNoiseEnabled?, shadowPassEnabled?, shadowCascadeMask?,
      shadowSamplingEnabled?,
      shadowSunMode?: "production" | "overhead" | "angled" | "low",
      whiteTexturesEnabled?, materialMode?: "full" | "lambert" }
    { type: "resetRenderDebugOptions" }
    { type: "resetPerfStats" }
    { type: "resetStreaming" }

The TypeScript runtime also sends the internal create-time reset command:

    { type: "resetGame", terrainSeed, terrainPreset }

This reset command is part of the current browser runtime handshake, not a
general public UI command.

`debugSnapshot()` returns the Rust-assembled game/debug state. TypeScript may
validate and copy values, but it must not derive terrain stream, renderer,
player, or chunk state itself.
Renderer status currently includes the Rust/wgpu post-process runtime sentinel
and the selected post-process debug view:

    rendererStatus.postProcessRuntime === "rust-wgpu"
    rendererStatus.postProcessDebugView === "final" | "sceneColor" |
      "linearDepth" | "postToneMap" | "bloom" | "dofCoc" |
      "dofBlurred"
    rendererStatus.postProcessExposure: number
    rendererStatus.postProcessToneMappingEnabled: boolean
    rendererStatus.postProcessBloomEnabled: boolean
    rendererStatus.postProcessBloomThreshold: number
    rendererStatus.postProcessBloomIntensity: number
    rendererStatus.postProcessDofEnabled: boolean
    rendererStatus.postProcessDofFocusDistance: number
    rendererStatus.postProcessDofFocusRange: number
    rendererStatus.postProcessDofMaxBlurPixels: number
    rendererStatus.terrainUpdateTotalMs: number
    rendererStatus.terrainUpdateUpsertedMeshCount: number
    rendererStatus.terrainUpdateRemovedMeshCount: number
    rendererStatus.terrainUpdateUploadedVertexFloatCount: number
    rendererStatus.terrainUpdateUploadedIndexCount: number
    rendererStatus.frameCulledDrawCount: number
    rendererStatus.frameSubmittedVertexCount: number
    rendererStatus.frameSubmittedIndexCount: number
    rendererStatus.frameSubmittedTriangleCount: number
    rendererStatus.shadowMaxDistanceMeters: number
    rendererStatus.shadowStrength: number
    rendererStatus.shadowEffectiveSunElevation: number
    rendererStatus.shadowEffectiveSunDirection: { x, y, z }
    rendererStatus.gpuTimerAvailable: boolean
    rendererStatus.gpuTimerUnavailableReason: string
    rendererStatus.gpuTimestampPeriodNs: number
    rendererStatus.gpuTimerPendingReadbackCount: number
    rendererStatus.renderDebugOptions: RenderDebugOptions
    rendererStatus.lastRenderCounters: RenderCounterSample
    rendererStatus.lastGpuPassTimings: GpuPassTimingSample

The root debug snapshot also includes:

    rustPerfStats
    renderDebugOptions

The terrain update fields are Rust-owned CPU-side diagnostics for the latest
terrain stream update on the browser game tick. They are intended for smoke and
performance reports, not for browser-side terrain scheduling decisions.

`rustPerfStats` is Rust-owned frame-history data. It summarizes recent Rust CPU
timing spans, renderer counters, latest terrain LOD counters, shadow cascade
counters, and optional GPU pass timings. `renderDebugOptions` is the active
Rust-owned diagnostic render state. TypeScript may display, dump, and test these
values, but must not use them to compute terrain visibility, culling, material
selection, or renderer behavior.

Terrain debug state currently includes LOD0 compatibility keys
`loadedTerrainChunkKeys` and `terrainChunkKeys`, plus explicit multi-resolution
node keys `loadedTerrainNodeKeys` and `terrainNodeKeys`. Terrain node key strings
are Rust-produced stable IDs in the form `lodN:x,y,z`. The accompanying
`terrainStreamStatus` includes legacy chunk counts for HUD/smoke compatibility
and node/LOD fields such as `loadedNodeCount`, `renderedNodeCount`,
`maxRenderedLod`, `visibleWorldSpanXMeters`, `visibleWorldSpanZMeters`, and
`terrainLodSummary`. The default playable stream currently reaches LOD4 and
reports a settled horizontal visible span of at least 4096 meters in X and Z.
The browser playable path reports `workerPoolRuntime === "browser-worker"`,
the actual `terrainWorkerCount`, worker in-flight/queued/completed/stale/failed
counters, and `synchronousBuildCount`. Native tests and Rust smoke can still
use the synchronous stream path, where the runtime reports `"rust-sync"`.
Browser TypeScript may display or assert these values but must not compute
desired nodes, LOD selection, fallback cover, density dependencies, mesh
visibility, or renderer state.

The active stream scheduler is generated-node based: a scheduled node build
produces either a renderable mesh or an empty node. Some debug and fixture
fields retain density-shaped names such as `densityReadyChunkCount` or
`missingDensityCount` for compatibility with existing HUD, smoke, and
standalone `terrain_core.wasm` export checks. Browser code must treat these as
opaque Rust-owned status values, not as a signal to reintroduce a browser terrain
density pipeline.

Contract rules:

- Add new user/debug control through `GameCommand` before adding new public
  methods.
- Add new HUD/smoke state through `debugSnapshot()` before adding TypeScript
  mirrors.
- Keep frame input object-shaped. Do not add scalar wasm-bindgen frame methods.
- Generated wasm-bindgen types currently show `any` for object packets. Treat
  `src/engine/web/browserGameTypes.ts` as the schema until this is generated.

## OFG-API-002: Rust Game To Browser Asset Loader

Rust owns terrain texture manifest interpretation, layer ordering, texture
array IDs, texture-array shape validation, and GPU texture installation.
TypeScript only decodes Rust-provided URL lists into RGBA bytes.

The active terrain texture path calls:

    assetLoader.loadTextureArrays(requests)

The same browser asset-loader object exposes the GLTF/model byte fetch lane
described by `OFG-API-010`:

    assetLoader.loadBytes(requests)

`loadBytes` returns opaque bytes by ID. TypeScript must not interpret those
bytes as model, material, animation, or renderer data.

TypeScript accepts:

    export type RgbaTextureArrayAssetRequest = {
      readonly id: string;
      readonly urls: readonly string[];
    };

TypeScript returns:

    export type RgbaTextureArrayAsset = {
      readonly id: string;
      readonly width: number;
      readonly height: number;
      readonly layers: number;
      readonly data: Uint8Array;
    };

Current Rust-owned array IDs are:

    terrain.albedo
    terrain.normal
    terrain.material

Contract rules:

- TypeScript must not parse the Poly Haven terrain manifest.
- TypeScript must not assign material layers or texture roles.
- Rust validates that all returned arrays have positive dimensions, exactly 16
  layers, matching shapes, and `width * height * layers * 4` bytes.

## OFG-API-003: Debug And Smoke-Test Hooks

`window.__ofgDebug` is a browser-only debug and test contract. It is not game
simulation ownership. Browser smoke uses it only for black-box integration
signals such as runtime ownership strings, renderer status, HUD/input effects,
and reload health. Terrain image verification belongs in Rust offscreen smoke,
not browser debug-hook terrain clients.

Current hook categories:

- Terrain chunk compatibility keys, terrain node keys, and terrain stream
  status from Rust `debugSnapshot()`.
- Terrain preset and seed from Rust `debugSnapshot()`.
- Renderer status from Rust `debugSnapshot()`, including resource counts,
  frame count, total frame draw candidates, and visible post-cull frame draw
  count. Shadow resource status currently reports cascade count, shadow-map
  size, per-frame shadow-pass draw count, maximum receiver distance, active
  fade strength, effective sun elevation, and clamped effective sun direction.
  Post-process status reports the
  selected debug view, exposure, tone mapping, bloom, and depth-of-field
  settings. Performance status also reports main-camera cull count, submitted
  vertices/indices/triangles, GPU timer availability, latest render counters,
  active render debug options, and latest GPU pass timings when available.
- Browser CPU frame-loop perf summaries from `src/app/perfDebug.ts`, combined
  with Rust-provided `rustPerfStats` only for DevTools dumps and capture
  artifacts. Current debug hooks are `getPerfStats()`, `dumpPerfStats()`, and
  `resetPerfStats()`.
- Render diagnostic controls through `setRenderDebugOptions(...)`,
  `getRenderDebugOptions()`, and `resetRenderDebugOptions()`. Options can
  filter submitted terrain LODs, disable sky draws, disable procedural sky
  cloud noise while keeping the analytic sky visible, disable shadow-map passes,
  choose active shadow cascades, disable shadow sampling, force deterministic
  shadow sun modes for capture diagnostics, force diagnostic white texture
  sampling, and use a basic Lambert material mode. These controls must default
  to production rendering and must not mutate terrain streaming, mesh
  generation, resource lifetime, or ownership policy. Shadow sun modes are
  Rust-owned renderer diagnostics: `production` uses the engine sky, `overhead`
  forces a vertical sun for tight culling probes, `angled` forces a non-vertical
  daylight sun, and `low` forces a near-horizon sun that should fade/disable
  shadows rather than expand caster search indefinitely.
- The browser app exposes those diagnostics through a DOM render-debug panel and
  a toggleable live perf overlay. These are UI wrappers around the same
  `game.command(...)`, `debugSnapshot()`, and `getPerfStats()` lanes; they must
  not compute terrain visibility, culling, material selection, GPU pass behavior,
  post-process behavior, or LOD policy in TypeScript. The same panel may expose
  post-process debug controls for debug view, tone mapping, bloom, depth of
  field, and numeric post-process settings by forwarding the existing
  post-process commands.
- Shadow debug view state from Rust `debugSnapshot()` as `shadowDebugView`, plus
  the browser-only `setShadowDebugView(...)` debug hook. Supported debug view
  names are `off`, `cascadeIndex`, `shadowVisibility`, and
  `shadowDepthCascade0` through `shadowDepthCascade3`.
- Sky runtime, day phase, sun elevation, cloud coverage, and star intensity from
  Rust `debugSnapshot()`.
- Player character ID/label, visibility, follow-state, animation clip,
  walk/run blend, playback scale, locomotion speed, numeric animation tuning,
  and CPU-skinning state from Rust `debugSnapshot()`.
- Runtime ownership sentinel strings such as `"rust"` and `"rust-wgpu"`.
- Debug commands that call `game.command(...)`.
- Post-process debug view commands and screenshots. Current debug views are
  final output, HDR scene color, linear depth, post-tone-map color, and bloom
  contribution, DoF circle of confusion, and DoF blurred scene color.

Compatibility fields:

- `terrainWorkerPoolRuntime` and `terrainWorkerCount` are active debug fields
  for the browser worker transport. They describe the runtime actually used for
  terrain build requests, not LOD policy ownership. The playable browser runtime
  should report `"browser-worker"` with a positive worker count; synchronous
  Rust-only harnesses may report `"rust-sync"`.

Contract rules:

- Debug hooks may expose browser test affordances, but must not compute terrain,
  renderer, sky, cloud, time-of-day, lighting, or player state.
- Smoke scripts must inspect both command results and screenshots/report JSON
  when visual behavior changes.
- Browser smoke must keep post-process debug views as black-box Rust/wgpu
  outputs. It may select a view through `game.command(...)`, but must not
  compute or interpret renderer textures in TypeScript.

## OFG-API-004: Terrain Vertex And Material Layout

Renderable terrain mesh vertices are 19 `f32` values per vertex:

| Field | Floats | Shader location |
|---|---:|---:|
| position | 3 | 0 |
| color | 3 | 1 |
| normal | 3 | 2 |
| uv | 2 | 3 |
| material layer indices | 4 | 4 |
| material weights | 4 | 5 |

Current duplicated constants live in `terrain_core`, `engine_web`, and the
WebGPU vertex-buffer layout. Shader contract tests still validate that the WGSL
locations match the renderer layout. Reviewers must treat this as a fragile
contract until it is generated from one source.

The shared scene shader currently writes two fragment outputs for the
post-process frame graph:

| Output | Shader location | Format |
|---|---:|---|
| scene color | 0 | Browser path `Rgba16Float`; smoke path readback color |
| linear depth/distance | 1 | `R32Float` |

Scene shader outputs are scene-linear. The fullscreen post-process shader owns
exposure and filmic tone mapping before presenting to the browser surface. The
browser renderer chooses an sRGB surface format when available, so the final
post shader writes display-linear values and lets the surface handle sRGB
encoding. Bloom is Rust/wgpu-owned: the browser path extracts bright HDR scene
energy into a half-resolution `Rgba16Float` bloom target, composites that target
before tone mapping, and exposes enabled/threshold/intensity through Rust
commands and renderer status. Depth of field is Rust/wgpu-owned and default
off: the post shader derives a per-pixel circle of confusion from the
renderer-owned linear-depth target, samples a small fullscreen blur in post, and
exposes enabled/focus-distance/focus-range/max-blur-pixels through Rust commands
and renderer status.

Contract rules:

- Any stride, offset, material-index, material-weight, or shader-location change
  must update all four sites and the shader/renderer tests in the same
  milestone.
- Any fragment output, scene target format, or post-process debug-view change
  must update `uber.wgsl`, `post.wgsl`, generated shader artifacts, Rust/wgpu
  pipeline target descriptors, and browser/Rust smoke coverage in the same
  milestone.
- Terrain and shader changes must run `npm run check:shaders`, `npm test`, and
  the relevant terrain/browser smoke tests.

## OFG-API-005: Terrain Presets And World Descriptor Codes

Browser URLs and TypeScript descriptors use string preset IDs:

    seed
    rollingHills
    mountainValley
    rockyHighland

WASM/Rust commands use numeric codes. Current mappings are duplicated in the
browser runtime, Rust terrain presets, and Rust debug snapshot conversion.

Contract rules:

- Adding, removing, or renaming a preset must update every mapping and tests.
- Prefer generating a small preset metadata artifact before adding more presets.
- `rollingHills` is the current default terrain preset.

## OFG-API-006: Standalone Terrain WASM Artifact

The previous `terrain_core.wasm` TypeScript adapters in `src/engine/world` have
been removed. TypeScript tests no longer instantiate `terrain_core.wasm`, read
terrain WASM memory buffers, call terrain density/mesh/scheduler exports, or
validate generated TypeScript metadata for the standalone terrain artifact.

The standalone `assets/wasm/terrain_core.wasm` artifact still exists as an
export-contract fixture and as the implementation loaded by the dedicated
browser terrain build worker. It is not a TypeScript-owned terrain runtime:
Rust still schedules requests through `engine_web`, validates completions, owns
visibility, and uploads renderer resources. `tools/build-terrain-wasm.mjs`
builds the artifact and validates the expected raw export names directly from
the WASM module; it no longer writes a generated TypeScript metadata module.
Terrain performance benchmarking now uses `npm run bench:terrain:rust`, which
calls `terrain_core` from Rust and writes JSON under `artifacts/terrain-bench/`.
The terrain benchmark report must sample a realistic multi-node terrain
population, not just a single chunk, and include aggregate/per-LOD/per-class
generation timing distributions plus coarse phase breakdowns for density,
Dual Contouring, material expansion, and buffer copy cost.

Contract rules:

- Runtime app code and TypeScript tests must not load or call
  `terrain_core.wasm` directly, except for `src/engine/web/terrainBuildWorker.ts`
  fulfilling Rust-issued opaque build requests.
- Do not recreate TypeScript adapters for terrain density sampling, chunk
  filling, stream scheduling, density storage, mesh generation, or raw terrain
  WASM memory buffers.
- Do not use the standalone artifact or raw export list as
  justification to rebuild TypeScript terrain scheduling, meshing, density
  storage, or worker protocols.

## OFG-API-007: Raw Linked WASM Exports In Engine Web

The old raw `ofg_engine_web_*` exports have been removed from
`crates/engine_web`, and `tools/build-engine-web-wasm.mjs` fails if
wasm-bindgen glue reintroduces that prefix. `assets/wasm/engine_web/engine_web.d.ts`
may still list raw `ofg_terrain_core_*`, `ofg_engine_*`, or other linked
`ofg_*` exports in `InitOutput` because linked Rust fixture crates still contain
`#[no_mangle]` facades. These linked exports are visible in generated output but
are not a supported browser runtime API.

Supported browser code must rely on `src/generated/web/engineWebWasm.ts`, which
recognizes only:

    RustBrowserGame

Contract rules:

- Do not call raw `ofg_*` exports from playable TypeScript.
- Do not restore `ofg_engine_web_*` exports; add behavior through
  `RustBrowserGame` commands, frame input, or debug snapshots instead.
- If a milestone touches Rust crate facades, generated wasm exports, or build
  scripts, review whether the raw exports can be feature-gated or split into
  standalone fixture crates.
- Add negative generated-binding checks before relying on the absence of old
  terrain mesh, texture upload, render-frame, or scalar player APIs.

## OFG-API-008: Future Game Lifecycle And Tuning Surface

Future supported facade methods may include:

    create(canvas, init)
    save()
    load(saveBytes)
    dispose()

Future command variants may include world config and terrain tuning commands.
These are not current acceptance criteria. Add them only when real behavior,
tests, and validation exist.

Contract rules:

- Do not add placeholder public methods that are not exercised.
- Browser smoke may read sentinel strings, renderer status, camera mode, and
  opaque terrain chunk-key counts, but must not compute terrain state.
- Terrain seam, preset, material, mesh, and visual verification should run
  through Rust tests or `npm run smoke:rust`.
- Future lifecycle methods must define ownership of Rust resources, browser
  handles, saves, and repeated start/stop behavior before implementation.

## OFG-API-009: Forbidden TypeScript Ownership

The following TypeScript ownership must not be reintroduced in runtime code,
tests, or test helpers:

- Scene graph or ECS.
- Terrain generator, density sampler, terrain manager, or terrain edit owner.
- Dual Contouring or terrain mesh generation.
- Terrain stream scheduler, density store, or terrain worker payload protocol.
- WebGPU device, pipeline, render pass, terrain mesh handle, texture handle, or
  draw submission owner.
- Terrain material manifest interpretation or material layer assignment.
- Factory/world simulation owner.

This rule forbids TypeScript ownership only. A small Rust-owned scene/component
model in `crates/engine_core` is allowed when it preserves the browser runtime
facade, keeps WebGPU handles out of scene resources, and does not route
per-entity work through TypeScript. The intended Rust shape is a scene-owned
array of entities addressed by stable generational `EntityId` handles, with
typed components such as camera, player, terrain, and mesh renderer components.
`engine_core` may extract visible mesh renderer items for Rust/wgpu to resolve,
but TypeScript must not mirror or traverse the scene.

Allowed TypeScript responsibilities remain:

- Browser startup and WASM module loading.
- Canvas lookup and size measurement.
- DOM input collection.
- URL seed/preset parsing.
- HTML HUD/debug UI and smoke-test hooks.
- Generic browser image decoding for Rust-provided texture-array requests.
- Generic opaque byte fetching for Rust-provided model asset requests.

## OFG-API-010: GLTF Model And Animation Loading

The completed feature plan is archived at
`docs/archived/GLTF_CHARACTER_PLAN.md`. The current supported slice loads
checked-in GLB fixtures through the generic byte asset loader, parses them in
Rust, imports renderer-neutral image/texture/sampler/material records, registers
model mesh/material/texture resources, attaches model nodes to the Rust scene,
renders them through Rust/wgpu, samples non-skinned node animation clips for
translation, rotation, and scale, imports skin joints/inverse bind matrices,
CPU-skins rigged model vertices, updates same-size model vertex buffers every
frame, and selects/blends idle, walk, and sprint clips from Rust horizontal
movement speed. Model images embedded in GLB buffer views or data URIs are
decoded in Rust to one-layer RGBA texture arrays; external GLTF image URIs are
preserved by the importer but are not accepted by the runtime texture resolver
yet. The current live player character path uses a shared Quaternius Universal
Animation Library 1 GLB for `Idle_Loop`, `Walk_Loop`, and `Sprint_Loop`, plus
separate male/female Quaternius base-character body GLBs. The current checked-in
bodies are Superhero male/female placeholders because the free Standard download
available to this repo does not include Regular male/female full-body GLBs.

The active character is selected through the Rust command lane with stable
browser IDs:

    export type PlayerCharacterId = "male" | "female";
    { type: "togglePlayerCharacter" }
    { type: "setPlayerCharacter", character: PlayerCharacterId }
    { type: "setPlayerAnimationTuning", walkSpeedMetersPerSecond,
      runSpeedMetersPerSecond, idlePlaybackScale, walkPlaybackScale,
      runPlaybackScale }

The selected body is attached to Rust-owned player character scene items, one
per skinned GLTF primitive, that follow the Rust player transform, stay hidden
in first-person, and replace the old yellow debug marker as the browser
debug-fly player representation. Rust debug snapshots expose the selected
character ID/label, active/next clip, crossfade weight, walk/run blend weight,
numeric playback scale, locomotion speed, numeric tuning values, active model
primitive/material/texture counts, non-fallback albedo part count, and
CPU-skinning joint count for HUD/debug/smoke tests. GPU skinning, tangent-space
normal-map application, automatic foot-contact extraction, inverse kinematics,
and full animation-tuning UI remain future milestones under the same boundary.

Supported model material workflows:

- glTF 2.0 core metallic-roughness. Rust imports base-color factors/textures,
  metallic and roughness factors, and metallic-roughness texture references.
  The shader uses roughness from texture green and metallic from texture blue,
  distinct from terrain material texture channels.
- Archived `KHR_materials_pbrSpecularGlossiness`. Rust imports diffuse
  factors/textures, specular factors, glossiness factors, and
  specular-glossiness textures. The renderer uses this path when the extension
  is present; required-extension fixtures should not silently fall back to core
  metallic-roughness.

The intended runtime format is checked-in GLB for model and animation assets.
Rust owns GLTF parsing, model resource registration, scene node/entity creation,
animation clips, skeletons, skinning, animation blending, and renderer resource
resolution.

TypeScript may provide only generic browser substrate:

    export type ByteAssetRequest = {
      readonly id: string;
      readonly url: string;
    };

    export type ByteAsset = {
      readonly id: string;
      readonly data: Uint8Array;
    };

    assetLoader.loadBytes(requests)

Contract rules:

- TypeScript must not parse GLTF JSON or GLB chunks.
- TypeScript must not inspect meshes, nodes, skins, animation channels, clips,
  materials, textures, images, samplers, material workflows, or skeletons.
- TypeScript must not create per-model or per-entity render calls.
- Rust debug snapshots may expose active model, player-character visibility,
  character ID/label, clip, blend, walk/run blend, playback scale, locomotion
  speed, numeric tuning values, primitive/material/texture counts, non-fallback
  texture counts, and skinning state for HUD and smoke tests.
- Static model meshes, skinned model meshes, and animation data should use
  explicit Rust-owned contracts rather than overloading the terrain vertex
  layout.

## Current Risk Register

These are known contract risks for milestone reviewers:

- Raw linked `ofg_*` exports leak through `engine_web.d.ts`.
- The wasm-bindgen facade object protocol is manually typed because generated
  d.ts uses `any`.
- Terrain vertex layout constants are duplicated across Rust and shader-facing
  renderer code.
- Camera/frame uniform layout constants are duplicated across Rust frame-packet
  builders, Rust/wgpu bind-group allocation, native smoke helpers, and WGSL
  `Camera` fields. Sky/time additions must update all sites and shader tests in
  one milestone.
- Terrain preset maps are duplicated across TypeScript and Rust.
- Browser terrain generation now uses browser workers for the playable path,
  while Rust retains scheduler, validation, visibility, and renderer ownership.
- `crates/engine_web/src/wgpu_renderer.rs` is still over the maximum preferred
  file size, `crates/engine_web/src/model_assets.rs` is over the split-pressure
  threshold, and `crates/terrain_core/src/facade.rs` is also oversized. Continue
  extracting focused model/renderer modules before GPU skinning, tangent-space
  normal maps, static model showcase loading, or retargeting adds more renderer
  code.
- The GLTF path uses the generic byte asset loader and Rust-owned animation
  sampling; keep TypeScript generic and do not let it grow model or animation
  semantics while expanding the feature.
