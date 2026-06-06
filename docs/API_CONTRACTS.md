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
| OFG-API-001 | Browser shell to Rust browser game | Active | `src/engine/web/browserGameTypes.ts`, `src/engine/web/engineWebWasm.ts`, `crates/engine_web/src/wgpu_renderer.rs` |
| OFG-API-002 | Rust browser game to browser asset loader | Active | `src/engine/browser/textureAssetLoader.ts`, `crates/engine_web/src/terrain_textures.rs` |
| OFG-API-003 | Debug and smoke-test hooks | Active | `src/app/game.ts`, `src/engine/web/browserGameTypes.ts`, `tools/browser-smoke.mjs` |
| OFG-API-004 | Terrain vertex and material layout | Active | `crates/terrain_core/src/constants.rs`, `crates/engine_web/src/config.rs`, `src/engine/world/terrainMesh.ts`, `crates/engine_web/src/wgpu_renderer.rs` |
| OFG-API-005 | Terrain presets and world descriptor codes | Active | `src/engine/world/terrainDescriptor.ts`, `src/engine/web/rustBrowserGameRuntime.ts`, `src/engine/world/terrainCoreWasm.ts`, `crates/terrain_core/src/presets.rs` |
| OFG-API-006 | Standalone `terrain_core.wasm` TypeScript adapters | Fixture | `src/engine/world/terrainCoreWasm.ts` and related tests |
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
    { type: "setPlayerMode", mode: "firstPerson" | "debugFly" }
    { type: "setPlayerPosition", x, y?, z }
    { type: "setDebugCamera", x, y, z, yaw, pitch }
    { type: "resetStreaming" }

The TypeScript runtime also sends the internal create-time reset command:

    { type: "resetGame", terrainSeed, terrainPreset }

This reset command is part of the current browser runtime handshake, not a
general public UI command.

`debugSnapshot()` returns the Rust-assembled game/debug state. TypeScript may
validate and copy values, but it must not derive terrain stream, renderer,
player, or chunk state itself.

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
simulation ownership, but smoke tests and terrain verification scripts depend on
it.

Current hook categories:

- Terrain keys and stream status from Rust `debugSnapshot()`.
- Terrain preset and seed from Rust `debugSnapshot()`.
- Renderer status from Rust `debugSnapshot()`.
- Runtime ownership sentinel strings such as `"rust"` and `"rust-wgpu"`.
- Debug commands that call `game.command(...)`.

Compatibility fields:

- `terrainWorkerPoolRuntime` and `terrainWorkerCount` are legacy-shaped debug
  names. The playable runtime no longer has a TypeScript terrain worker bridge;
  `terrainWorkerCount` currently reflects Rust stream work capacity. Future
  cleanup may rename these fields, but smoke tests currently rely on them.

Contract rules:

- Debug hooks may expose browser test affordances, but must not compute terrain,
  renderer, or player state.
- Smoke scripts must inspect both command results and screenshots/report JSON
  when visual behavior changes.

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

Current duplicated constants live in TypeScript, `terrain_core`, `engine_web`,
and the WebGPU vertex-buffer layout. Reviewers must treat this as a fragile
contract until it is generated from one source.

Contract rules:

- Any stride, offset, material-index, material-weight, or shader-location change
  must update all four sites and the shader/renderer tests in the same
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
browser runtime, terrain WASM fixture adapter, Rust terrain presets, and Rust
debug snapshot conversion.

Contract rules:

- Adding, removing, or renaming a preset must update every mapping and tests.
- Prefer generating a small preset metadata artifact before adding more presets.
- `rollingHills` is the current default terrain preset.

## OFG-API-006: Standalone Terrain WASM Fixture Adapters

The `terrain_core.wasm` TypeScript adapters in `src/engine/world` are fixture
and compatibility surfaces for tests, benchmarks, and generated artifact
validation. They are not the playable terrain runtime.

Contract rules:

- Runtime app code must not load or call `terrain_core.wasm` directly.
- Fixture-looking files should keep top-of-file comments that say they are not
  playable runtime terrain ownership.
- Do not use fixture adapters as justification to rebuild TypeScript terrain
  scheduling, meshing, density storage, or worker protocols.

## OFG-API-007: Raw Linked WASM Exports In Engine Web

`assets/wasm/engine_web/engine_web.d.ts` currently lists raw `ofg_engine_web_*`,
`ofg_terrain_core_*`, and `ofg_engine_*` exports in `InitOutput` because linked
Rust crates still contain `#[no_mangle]` facades. These exports are visible in
generated output but are not a supported browser runtime API.

Supported browser code must rely on `src/generated/web/engineWebWasm.ts`, which
recognizes only:

    RustBrowserGame

Contract rules:

- Do not call raw `ofg_*` exports from playable TypeScript.
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
- Future lifecycle methods must define ownership of Rust resources, browser
  handles, saves, and repeated start/stop behavior before implementation.

## OFG-API-009: Forbidden TypeScript Ownership

The following TypeScript ownership must not be reintroduced:

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
Rust, registers model mesh/material resources, attaches model nodes to the Rust
scene, renders them through Rust/wgpu, samples non-skinned node animation clips
for translation, rotation, and scale, imports skin joints/inverse bind matrices,
CPU-skins rigged model vertices, updates a same-size model vertex buffer every
frame, and selects/blends idle and walk clips from Rust horizontal movement
input. The current live player asset is a selected Quaternius Universal
Animation Library 2 GLB using `Idle_FoldArms_Loop` and `Walk_Carry_Loop`. It is
attached to a Rust-owned player character scene item that follows the Rust
player transform, stays hidden in first-person, and replaces the old yellow
debug marker as the browser debug-fly player representation. GPU skinning,
multi-primitive character assembly, and retargeting the separate Quaternius
base-character GLB remain future milestones under the same boundary.

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
  materials, or skeletons.
- TypeScript must not create per-model or per-entity render calls.
- Rust debug snapshots may expose active model, player-character visibility,
  clip, blend, and skinning state for HUD and smoke tests.
- Static model meshes, skinned model meshes, and animation data should use
  explicit Rust-owned contracts rather than overloading the terrain vertex
  layout.

## Current Risk Register

These are known contract risks for milestone reviewers:

- Raw linked `ofg_*` exports leak through `engine_web.d.ts`.
- The wasm-bindgen facade object protocol is manually typed because generated
  d.ts uses `any`.
- Terrain vertex layout constants are duplicated across TypeScript, Rust, and
  shader-facing renderer code.
- Terrain preset maps are duplicated across TypeScript and Rust.
- Runtime debug names still include worker terminology even though the playable
  terrain stream is Rust-owned and currently synchronous.
- Some standalone WASM fixture adapters look like runtime modules by filename.
- `crates/engine_web/src/wgpu_renderer.rs` is still over the maximum preferred
  file size, `crates/engine_web/src/model_assets.rs` is over the split-pressure
  threshold, and `crates/terrain_core/src/facade.rs` is also oversized. Continue
  extracting focused model/renderer modules before GPU skinning, multi-primitive
  character rendering, or retargeting adds more renderer code.
- The GLTF path uses the generic byte asset loader and Rust-owned animation
  sampling; keep TypeScript generic and do not let it grow model or animation
  semantics while expanding the feature.
