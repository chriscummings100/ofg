# Rust Conversion Plan

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible.
Return to the user only for critical input that cannot be safely inferred, or
when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it.

This is the single active plan for moving OFG to an almost entirely Rust-owned
browser application. It replaces the older split Rust engine plan, TypeScript
reduction audit, browser/Rust API note, and the reduction ExecPlan. The archived
`docs/archived/RUST_TYPESCRIPT_REDUCTION_EXECPLAN.md` was the source of truth
for this consolidation; its purpose, observations, decisions, outcomes, context,
work plan, concrete commands, validation, recovery guidance, and target
interfaces are preserved here.

## Purpose / Big Picture

The Rust migration has already moved most high-volume engine responsibilities out
of TypeScript, but the remaining TypeScript files still make the boundary look
larger and less final than it should. After this work, a developer or AI agent
can open this one plan and see exactly:

- which systems are already Rust-owned,
- which TypeScript files remain only as browser substrate,
- which files are test-only or historical,
- what the final TypeScript/Rust API is,
- what implementation slices delete whole categories instead of repeatedly
  removing half of the remainder.

The user-visible outcome is project clarity and migration momentum. The next
agent should not need to rediscover the same state of play before continuing the
Rust-first plan.

The final target is a clear, minimal browser boundary:

- TypeScript owns browser shell concerns: module loading, canvas lookup, DOM
  input collection, HTML HUD/debug UI, URL parameters, and browser smoke hooks.
- Rust owns engine concerns: world state, player/camera, terrain generation,
  terrain streaming, worker job semantics, simulation, save/load schemas, render
  extraction, WebGPU resources, GPU uploads, and draw submission.

Migration progress is measured by how closely the code matches the target API
and scorecard in this document, not by vague TypeScript line-count reduction.

## Progress

- [x] (2026-06-06) Read `PLANS.md`, the old `docs/RUST_ENGINE_PLAN.md`,
  `docs/terrainplan.md`, `docs/ARCHITECTURE.md`, and `AGENTS.md`.
- [x] (2026-06-06) Listed compiled TypeScript files under `src/` and searched
  live references for terrain/render/engine bridge modules.
- [x] (2026-06-06) Created the now-archived
  `docs/archived/TYPESCRIPT_REDUCTION_AUDIT.md` to classify remaining
  TypeScript by runtime role, Rust ownership state, redundancy, and deletion
  path.
- [x] (2026-06-06) Updated `AGENTS.md`, `docs/ARCHITECTURE.md`,
  `docs/TERRAIN_PLAN.md`, and the historical scene-model documentation so they
  agreed on current ownership and the finite burn-down.
- [x] (2026-06-06) Added the now-archived
  `docs/archived/BROWSER_RUST_API.md` to define the exact target
  TypeScript-to-Rust API, allowed Rust-to-browser interactions, current API gap,
  and progress scorecard.
- [x] (2026-06-06) Re-ran `git -c safe.directory=C:/dev/ofg diff --check`
  after the API contract update; it passed with only line-ending warnings.
- [x] (2026-06-06) Consolidated the active Rust conversion docs into this single
  ExecPlan, with older split plans moved to `docs/archived/`.
- [x] (2026-06-06) Added the operational rule to proceed independently for as
  long as possible, returning only for critical input or plan completion.
- [x] (2026-06-06) Delete or demote test-only compiled TypeScript:
  `primitiveMesh.ts` and the legacy `TerrainCoreRenderPacketStore` surface after
  splitting live sink/packet types.
- [x] (2026-06-06) Split the live terrain render sink contract into
  `src/engine/render/terrainRenderChunkSink.ts`, retargeted runtime imports, and
  deleted the old packet-store module/tests plus the primitive mesh module/tests.
- [x] (2026-06-06) Move app-facing calls toward the target API: one frame input
  object, command lane, and debug snapshot facade. The underlying wasm-bindgen
  API still expands to scalar calls, and the adapter still calls the public wasm
  `renderGameFrame(aspect)` method internally.
- [x] (2026-06-06) Validated the app-facing boundary slice with `npm test` and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T08-10-02-931Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Replaced the generic `BrowserWorkerGroup` with
  `BrowserWorkerHost`, which owns Worker lifecycle and request-id envelopes while
  treating terrain job data as opaque payloads.
- [x] (2026-06-06) Validated the opaque worker-host slice with `npm test` and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T08-17-21-018Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [ ] Collapse terrain Worker semantics behind Rust or an opaque generic browser
  worker host.
- [ ] Move terrain texture asset ownership behind Rust and delete public
  terrain texture upload calls.
- [ ] Remove public terrain mesh upload calls by making Rust terrain streaming
  own mesh upload, retention, pruning, and debug visibility.
- [x] (2026-06-06) Deleted the unsupported standalone `engine_core.wasm`
  TypeScript wrapper, generated metadata, build script, package-script entries,
  and checked-in artifact; `engine_core` remains covered as a native Rust crate
  and through `engine_web`.
- [x] (2026-06-06) Validated the standalone `engine_core.wasm` deletion with
  `npm test`, `cargo test -p engine_core`, and `npm run check:wasm`.
- [x] (2026-06-06) Deleted the TypeScript density transfer path between main
  and worker `terrain_core.wasm` instances. LOD worker requests no longer carry
  density chunks or transfer-mode flags; worker meshing relies on Rust
  `ofg_build_chunk_mesh` to generate/reuse its own neighbor density apron. Also
  removed the now-unused public density-store contains/load WASM exports.
- [x] (2026-06-06) Validated density transfer deletion with
  `cargo test -p terrain_core`, `npm test`, and `npm run smoke:browser`;
  inspected
  `artifacts/browser-smoke/2026-06-06T08-45-02-188Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Removed the app/runtime facade's separate
  `game.renderFrame()` call. `game.tick(frame)` now performs Rust tick, terrain
  stream update, and Rust render through the runtime facade; the lower
  wasm-bindgen API still exposes `renderGameFrame(aspect)`.
- [x] (2026-06-06) Validated the one-call frame loop with `npm test` and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T08-51-28-528Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Deleted legacy TypeScript terrain material packing and
  triangle material-palette mesh expansion helpers. `terrainMesh.ts` carries
  only the mesh data shape plus Rust terrain vertex stride.
- [x] (2026-06-06) Validated legacy terrain helper deletion with `npm test`.
- [x] (2026-06-06) Deleted the legacy TypeScript terrain density source/edit
  API, density chunk class, sample indexing helpers, and broad density tests.
  `terrainChunk.ts` now keeps only the 3D chunk coordinate/key helpers still
  needed by the browser worker/render boundary, and the WASM density adapter
  returns a plain Rust-filled density result object.
- [x] (2026-06-06) Validated TypeScript density helper deletion with
  `npm test`.
- [x] (2026-06-06) Deleted the fallback TypeScript terrain worker pool from
  `TerrainChunkWorkerClient`. Browser Workers still exist as TypeScript browser
  substrate, but worker slots, request IDs, generations, in-flight tracking, and
  completion validation now require the Rust `terrain_core` worker pool.
- [x] (2026-06-06) Validated TypeScript worker-pool fallback deletion with
  `npm test` and `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-11-13-602Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Trimmed unused TypeScript terrain adapter surface: removed
  unused terrain-core density-window generator wrappers and the unused
  `getChunk` read method from the temporary terrain render sink contract.
- [x] (2026-06-06) Validated unused terrain adapter trim with `npm test`.
- [x] (2026-06-06) Removed duplicate terrain chunk-key strings from worker
  result payloads. Worker density and chunk results now carry chunk coordinates;
  the streamer derives string keys only when storing density results or
  uploading meshes to the current Rust renderer API.
- [x] (2026-06-06) Validated coord-only worker result payloads with `npm test`
  and `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-18-43-891Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Deleted `src/engine/world/terrainMaterials.ts`. Runtime
  texture loading now derives material layer URLs from the checked-in Poly Haven
  asset manifest instead of a duplicated compiled TypeScript material list.
- [x] (2026-06-06) Validated manifest-backed terrain texture loading with
  `npm test` and `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-23-07-362Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Changed the `engine_web` wasm-bindgen `tick` method from
  seven scalar input arguments to one browser frame object. The TypeScript
  adapter now forwards `BrowserFrameInput` directly into Rust, and Rust validates
  the packet fields before ticking `BrowserGameState`.
- [x] (2026-06-06) Validated the wasm-bindgen frame-object tick slice with
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`, and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-33-10-459Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Replaced exported scalar player/debug methods on
  `engine_web.wasm` with object-shaped `command(command)` and
  `debugSnapshot()` methods. `resetStreaming` remains handled by the TypeScript
  runtime facade because it still coordinates the temporary terrain streamer.
- [x] (2026-06-06) Validated the wasm-bindgen command/snapshot slice with
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`, and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-44-18-514Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Replaced the exported wasm `renderGameFrame(aspect)` method
  with `renderFrame()`. Rust now derives aspect from its owned surface
  configuration instead of accepting a TypeScript-computed aspect scalar.
- [x] (2026-06-06) Validated the no-argument `renderFrame()` slice with
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`, and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T09-52-34-964Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Moved initial Rust game reset into the object-shaped
  `command(command)` lane and removed the exported wasm
  `resetGame(seed, preset)` method.
- [x] (2026-06-06) Validated the reset-command slice with
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`, and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T10-01-04-541Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.
- [x] (2026-06-06) Folded Rust renderer status into
  `debugSnapshot().rendererStatus` and removed the exported
  `RustBrowserGameStatus` wasm class plus standalone `status()` method.
- [x] (2026-06-06) Validated the status-snapshot slice with
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`, and
  `npm run smoke:browser`; inspected
  `artifacts/browser-smoke/2026-06-06T10-13-33-424Z/report.json` plus first
  person, debug-fly, and streamed first-person screenshots.

## Surprises & Discoveries

- Observation: Before the docs cleanup, `AGENTS.md` described an older runtime
  where `engine_core.wasm` owned active player movement and TypeScript still
  submitted WebGPU draws.
  Evidence: the pre-edit file mentioned a temporary TypeScript `RenderWorld` and
  `WebGpuRenderer`, while the current architecture says Rust/wgpu owns draw
  submission through `crates/engine_web`.
- Observation: `src/engine/render/TerrainCoreRenderPackets.ts` is no longer on
  the playable browser mesh handoff, but it still provides shared sink and packet
  types imported by the live browser game adapter/runtime.
  Evidence: `rg` found runtime imports from
  `src/engine/web/rustBrowserGameAdapter.ts`,
  `src/engine/web/rustBrowserGameRuntime.ts`, and
  `src/engine/web/terrainCoreWorkerStreamer.ts`.
- Observation: `src/engine/world/primitiveMesh.ts` appears to be compiled only
  for its test, not runtime.
  Evidence: `rg` found imports only from `src/engine/world/primitiveMesh.test.ts`.
- Observation: The worker-streamer tests did not need the legacy Rust mesh packet
  store; a local recording sink proves streamer behavior without keeping a
  compiled TypeScript adapter around.
  Evidence: `src/engine/web/terrainCoreWorkerStreamer.test.ts` now implements
  `RecordingTerrainRenderChunkSink` against the small sink contract.
- Observation: The new `debugSnapshot()` is a TypeScript runtime facade snapshot,
  not yet a Rust-assembled wasm-bindgen snapshot.
  Evidence: `src/engine/web/rustBrowserGameRuntime.ts` assembles it from the
  Rust renderer adapter, terrain streamer, and descriptor until the underlying
  Rust API grows a native snapshot call.
- Observation: The browser Worker host can be payload-opaque without changing
  terrain streaming behavior, but terrain payload semantics still live in
  TypeScript.
  Evidence: `src/engine/browser/browserWorkerHost.ts` now wraps requests and
  completions by request id, while
  `src/engine/world/terrainChunkWorkerTypes.ts` still names density and chunk
  request/result payloads.
- Observation: The standalone `engine_core.wasm` wrapper was no longer a live
  runtime or necessary dev artifact.
  Evidence: `rg` found live references only in its TypeScript wrapper, generated
  metadata, tests, build script, package scripts, and docs; the playable app
  reaches `engine_core` through `engine_web`.
- Observation: The LOD0 worker density dependency payloads were redundant for
  the current Rust mesh export.
  Evidence: `ofg_build_chunk_mesh` already calls `generate_neighbor_apron_chunks`
  from seed, preset, coord, and cell size inside the worker WASM instance, so
  the TypeScript bridge did not need to load main-thread density chunks and
  transfer them to the worker before meshing.
- Observation: TypeScript terrain material packing and mesh palette expansion
  were legacy test-only behavior.
  Evidence: `rg` found `packTerrainMaterialWeights`,
  `expandTerrainMeshForTriangleMaterialPalettes`, and related layout offsets
  used only by `terrainMaterials.test.ts`, `terrainMesh.test.ts`, and the
  TypeScript mesh helper itself; runtime meshes already come from Rust
  `terrain_core`.
- Observation: The TypeScript terrain density source/edit API was also legacy
  support surface, not runtime terrain ownership.
  Evidence: `rg` found `TerrainDensitySource`, `EditableTerrainDensitySource`,
  `TerrainEdit`, `sampleTerrainDensity`, and the TypeScript density chunk
  sampling helpers used only by `terrainChunk.test.ts` and the WASM density
  wrapper; the runtime worker path only needed a Rust-filled `Float32Array`.
- Observation: The TypeScript terrain worker-pool fallback was unused by the
  playable runtime and tests.
  Evidence: `createRustBrowserGameRuntime` always loads `terrain_core.wasm` and
  calls `createTerrainChunkWorkerClient(descriptor, terrainCore)`, while the
  worker-client test injects `TerrainCoreWorkerPool` directly. The fallback
  `TypeScriptTerrainWorkerPool` had no live call site.
- Observation: Some TypeScript terrain-core wrapper helpers and render-sink
  methods were leftover adapter surface, not runtime behavior.
  Evidence: `rg` found no call sites for
  `prepareTerrainCoreDensityChunkWindow`,
  `createTerrainCoreChunkMeshGenerator`,
  `createTerrainCoreDensityChunkWindowGenerator`, or the sink `getChunk` method.
- Observation: Worker result chunk keys were redundant protocol data.
  Evidence: `TerrainCoreWorkerStreamer` already receives the scheduler-selected
  coord for each job and can derive a string key at the renderer/density-store
  boundary. The Rust worker pool validates completions from generation and coord.
- Observation: The compiled TypeScript terrain material list duplicated the
  checked-in Poly Haven asset manifest.
  Evidence: `terrainMaterials.ts` repeated the same material IDs and map paths
  already present in `assets/textures/polyhaven/manifest.json`; tests only
  verified that the two copies stayed aligned.
- Observation: The active docs had become plan-shaped overlap: an engine plan,
  a reduction audit, an API contract, a scene-model plan, and a reduction
  ExecPlan were all close enough to confuse the source of truth.
  Evidence: the active docs were consolidated on 2026-06-06 into
  `ARCHITECTURE.md`, `RUST_CONVERSION_PLAN.md`, `TERRAIN_PLAN.md`, and
  `TERRAIN_GEN_RESEARCH.md`, with retired docs moved under `docs/archived/`.

## Decision Log

- Decision: Treat the remaining migration as category deletion, not wrapper
  shrinkage.
  Rationale: Repeatedly moving half of the remaining code leaves TypeScript
  permanently terrain-aware. The plan must name whole remaining categories and
  delete or demote them as units.
  Date/Author: 2026-06-06 / Codex.
- Decision: Measure migration progress against an exact browser/Rust API.
  Rationale: The migration should converge on a known boundary: TypeScript calls
  a small Rust game facade and Rust only uses browser capabilities directly or
  through opaque browser services. This prevents "half of the remainder" slices
  from looking like completion.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep one active Rust conversion plan.
  Rationale: Split active plans made the source of truth blurry. This file now
  contains the API contract, current TypeScript audit, scorecard, next slices,
  validation gates, and recovery guidance for Rust conversion work.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep `docs/TERRAIN_PLAN.md` separate.
  Rationale: Terrain realism is a different active task from the TypeScript to
  Rust conversion. Terrain work should still follow this plan for ownership, but
  its believable-terrain research, progress, and milestones live in the terrain
  plan.
  Date/Author: 2026-06-06 / Codex.
- Decision: Execute the Rust conversion plan autonomously once started.
  Rationale: The plan is explicit enough to keep moving through milestones
  without repeated approval, while still returning for critical input that
  cannot be safely inferred.
  Date/Author: 2026-06-06 / Codex.
- Decision: Stage the target frame, command, and snapshot API at
  `RustBrowserGameRuntime` before changing the wasm-bindgen surface.
  Rationale: This moves `src/app` onto the final-shaped boundary immediately
  while preserving the existing Rust/wgpu facade and browser smoke behavior.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use a request-id based `BrowserWorkerHost` as the temporary browser
  Worker substrate.
  Rationale: This removes terrain-specific request ids and worker lifecycle from
  the browser host while keeping the existing Rust worker-pool ownership of ids,
  slots, generations, and completion validation.
  Date/Author: 2026-06-06 / Codex.
- Decision: Delete the standalone `engine_core.wasm` TypeScript wrapper instead
  of preserving it as a dev/test artifact.
  Rationale: The playable runtime uses `engine_web`, and `engine_core` is better
  covered directly by native Rust tests plus `engine_web` integration tests.
  Date/Author: 2026-06-06 / Codex.
- Decision: Delete the TypeScript density transfer path and let worker meshing
  generate/reuse dependency chunks inside Rust.
  Rationale: This removes shared/transfer density buffer policy, density chunk
  payloads, main-to-worker density store reads from TypeScript, and the unused
  public density-store contains/load WASM exports. It may duplicate some density
  generation until Rust owns the full worker runtime, but the ownership boundary
  is cleaner and browser smoke confirms streaming still works.
  Date/Author: 2026-06-06 / Codex.
- Decision: Let `RustBrowserGameRuntime.tick(frame)` own the full browser frame.
  Rationale: The app should call one frame method now, even while the lower
  adapter still bridges to separate wasm `tick(...)` and `renderGameFrame(...)`
  calls. This removes the app-level split without blocking on the wasm-bindgen
  API collapse.
  Date/Author: 2026-06-06 / Codex.
- Decision: Delete legacy TypeScript terrain material packing and mesh expansion
  helpers.
  Rationale: Rust `terrain_core` owns material classification, packing, and
  triangle-local material palettes. TypeScript only still needs material texture
  asset metadata until texture ownership moves behind Rust.
  Date/Author: 2026-06-06 / Codex.
- Decision: Delete the legacy TypeScript terrain density source/edit API.
  Rationale: Rust already owns density sampling and chunk filling; keeping a
  compiled TypeScript density/edit model made the browser boundary look broader
  than the runtime required. The temporary worker bridge can pass plain
  Rust-filled density arrays until worker semantics move fully behind Rust.
  Date/Author: 2026-06-06 / Codex.
- Decision: Require Rust worker-pool ownership for terrain browser workers.
  Rationale: A TypeScript fallback for request IDs, worker slot assignment,
  reset generations, and completion validation contradicts the ownership model
  and was no longer used. Keeping it would make a broken non-Rust path look
  supported.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep temporary TypeScript terrain contracts write-only where the
  runtime only uploads or removes chunks.
  Rationale: A read method on the browser-side terrain render sink preserved old
  packet-store shape without helping the current worker-to-Rust upload path.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep terrain worker result identity coordinate-based until the
  worker protocol becomes opaque.
  Rationale: String chunk keys are a TypeScript/debug/render-upload adaptation,
  not needed by Rust scheduler completion checks. Removing them narrows the
  terrain-specific worker payload while preserving current renderer calls.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use the checked-in Poly Haven manifest instead of a compiled
  TypeScript material list until Rust owns terrain texture assets outright.
  Rationale: This removes duplicated material order and path metadata from
  compiled TypeScript while preserving the current browser-only image decode
  step and texture upload API.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep project-facing frame input strongly typed while allowing
  wasm-bindgen's generated d.ts to expose raw `any` for the imported JS object.
  Rationale: `src/engine/web/engineWebWasm.ts` narrows the public TypeScript
  wrapper to `BrowserFrameInput`, and Rust validates every required packet
  field before ticking the game state. This removes the scalar wasm boundary
  without fighting wasm-bindgen's generic `JsValue` type emission.
  Date/Author: 2026-06-06 / Codex.
- Decision: Move player/debug controls through an object-shaped
  `engine_web.wasm` command lane.
  Rationale: Removing `togglePlayerMode`, `playerMode`, `playerX/Y/Z`,
  `setPlayerMode`, `setPlayerPosition`, and `setDebugCamera` from the exported
  wasm facade narrows the player API to the planned command/snapshot shape.
  `resetStreaming` stays in the TypeScript runtime facade until Rust owns the
  temporary terrain streamer.
  Date/Author: 2026-06-06 / Codex.
- Decision: Let Rust derive render aspect from its WebGPU surface configuration.
  Rationale: TypeScript still asks the Rust renderer to draw after the temporary
  terrain streamer updates, but it no longer needs to compute or pass render
  projection values into `engine_web.wasm`.
  Date/Author: 2026-06-06 / Codex.
- Decision: Treat game reset as an engine-web command instead of a standalone
  wasm method.
  Rationale: The browser runtime still chooses seed and terrain preset from URL
  startup state, but the Rust-facing boundary should not grow scalar lifecycle
  methods when the command lane already carries structured control packets.
  Date/Author: 2026-06-06 / Codex.
- Decision: Return renderer status through `debugSnapshot()`.
  Rationale: Renderer status is debug/HUD data, so a separate exported
  `RustBrowserGameStatus` wasm class and `status()` method kept a second debug
  read path alive after player mode and position had moved behind the snapshot.
  Date/Author: 2026-06-06 / Codex.

## Outcomes & Retrospective

The docs now separate completed Rust ownership from remaining TypeScript browser
substrate, and this plan gives the exact target boundary. The main remaining
implementation gap is terrain-aware Worker code in TypeScript, including
terrain-specific worker payload construction, plus texture asset loading, the
still public wasm `renderFrame()` method beneath runtime `tick`, public terrain
mesh and texture upload calls, and TypeScript terrain-specific worker result
contracts. Frame input, reset,
player/debug commands, player mode/position, and render submission now cross the
wasm-bindgen boundary as object-shaped or no-argument calls rather than growing
scalar lists.

The recommended next slice is to replace terrain-specific worker request/result
payload construction with Rust-owned worker/threading support or a strictly
opaque byte protocol.

The previous docs cleanup validated with:

    git -c safe.directory=C:/dev/ofg diff --check

The first source cleanup slice validated with:

    npm test

The app-facing frame/command/snapshot slice validated with:

    npm test
    npm run smoke:browser

The opaque worker-host slice validated with:

    npm test
    npm run smoke:browser

The standalone `engine_core.wasm` deletion slice validated with:

    npm test
    cargo test -p engine_core
    npm run check:wasm

The density transfer deletion slice validated with:

    cargo test -p terrain_core
    npm test
    npm run smoke:browser

The one-call frame loop slice validated with:

    npm test
    npm run smoke:browser

The legacy terrain helper deletion slice validated with:

    npm test

The TypeScript density helper deletion slice validated with:

    npm test

The TypeScript worker-pool fallback deletion slice validated with:

    npm test
    npm run smoke:browser

The unused terrain adapter trim validated with:

    npm test

The coord-only worker result payload slice validated with:

    npm test
    npm run smoke:browser

The manifest-backed terrain texture loading slice validated with:

    npm test
    npm run smoke:browser

The wasm-bindgen frame-object tick slice validated with:

    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

The wasm-bindgen command/snapshot slice validated with:

    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

The no-argument `renderFrame()` slice validated with:

    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

The reset-command slice validated with:

    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

The status-snapshot slice validated with:

    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

## Context and Orientation

The repository root is `C:\dev\ofg`. The current browser game is a lightweight
factory-game prototype with generated voxel terrain and Rust/wgpu rendering.

`crates/terrain_core` is the Rust terrain crate. It owns terrain sampling,
density chunk filling, Dual Contouring mesh emission, material classification,
stream scheduling, retained density storage, worker-pool request bookkeeping,
and a legacy mesh packet store.

`crates/engine_core` is the browser-free Rust engine logic crate. It owns tested
player, camera, world ID, transform, and render snapshot logic.

`crates/engine_web` is the browser-facing Rust/WASM crate. It composes
`engine_core` and `terrain_core` for the active playable runtime, owns the active
player/camera tick, owns Rust/wgpu WebGPU resources and draw submission, owns
terrain mesh/texture handles, and exposes the `RustBrowserGame` wasm-bindgen
facade.

`src/app` is the TypeScript browser shell. It creates the canvas, collects DOM
input, owns the HUD/debug wiring, parses URL seed/preset values, sends compact
frame input packets to `tick`, sends UI/debug actions through `command()`, reads
HUD/debug state through `debugSnapshot()`, and no longer calls a separate
runtime render method.

`src/engine/web` contains the remaining TypeScript browser/WASM shell around
Rust. `RustBrowserGameRuntime` is the current coarse shell, but it still starts
terrain workers, loads texture assets, and wires worker mesh results into
`RustBrowserGame`.

`src/engine/world` contains terrain descriptor types, chunk-key utilities, and
thin TypeScript adapters to `terrain_core.wasm`. It also still contains terrain
worker request/response types, plain worker density result contracts, and
worker transport code.

The key architectural goal is that TypeScript should eventually call something
close to `game.tick(frame)` and should not understand terrain scheduling,
density chunks, LOD stages, mesh packets, render resources, or world simulation.

Active docs are:

- `docs/ARCHITECTURE.md`: current architecture overview.
- `docs/RUST_CONVERSION_PLAN.md`: this active Rust conversion ExecPlan.
- `docs/TERRAIN_PLAN.md`: active terrain realism plan.
- `docs/TERRAIN_GEN_RESEARCH.md`: terrain generation research reference.

`docs/archived/` contains retired plans and reference snapshots. Documents in
that folder are not active instructions. Use them only for historical context
when explicitly needed.

## Target TypeScript-To-Rust API

TypeScript should eventually call only this small game facade:

```ts
export type OfgGameModule = {
  create(canvas: HTMLCanvasElement, init: GameInit): Promise<OfgGame>;
};

export type OfgGame = {
  tick(frame: BrowserFrameInput): void;
  resize(viewport: BrowserViewport): void;
  command(command: GameCommand): void;
  debugSnapshot(): GameDebugSnapshot;
  save(): Uint8Array;
  load(saveBytes: Uint8Array): void;
  dispose(): void;
};
```

`create(canvas, init)` initializes the Rust-owned game, terrain, renderer,
assets, and worker/threading runtime. TypeScript passes the canvas and startup
config; it does not separately initialize terrain, textures, render resources,
workers, or player state.

`tick(frame)` is the normal frame call. It advances input, player/camera,
simulation, terrain streaming, worker result consumption, GPU uploads, and render
submission. TypeScript should not call a separate render function in the target
API.

`resize(viewport)` forwards browser viewport size and device-pixel-ratio
changes. Rust owns canvas backing size validation and renderer reconfiguration.

`command(command)` is the single control lane for UI, debug, and tuning actions.
It replaces many narrow functions such as `setPlayerPosition`,
`setDebugCamera`, `setTerrainConfig`, `resetStreaming`, and camera mode toggles.
Commands are explicit Rust-owned data, not arbitrary string hooks.

`debugSnapshot()` returns a Rust-assembled snapshot for HUDs, tuning UI, smoke
tests, and screenshots. It may include player mode, player position, stream
status, renderer status, active chunk counts, worker timings, and tuning values.
TypeScript reads the snapshot but does not derive it.

`save()` and `load(saveBytes)` are final-shape APIs for durable game state. They
can remain unimplemented until save/load work begins, but save ownership belongs
to Rust.

`dispose()` releases Rust-owned renderer resources, workers, event-side handles,
and memory. It should make repeated browser smoke runs and future game switching
safe.

## Target Frame And Command Data

Frame input should be one object, not a growing scalar parameter list:

```ts
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
  readonly buttons?: readonly BrowserButtonEvent[];
};
```

The command lane should cover UI and debug control without adding new public
methods for every feature:

```ts
export type GameCommand =
  | { readonly type: "setPlayerMode"; readonly mode: "firstPerson" | "debugFly" }
  | { readonly type: "togglePlayerMode" }
  | { readonly type: "setPlayerPosition"; readonly x: number; readonly y?: number; readonly z: number }
  | { readonly type: "setDebugCamera"; readonly x: number; readonly y: number; readonly z: number; readonly yaw: number; readonly pitch: number }
  | { readonly type: "setWorldConfig"; readonly config: WorldConfig }
  | { readonly type: "resetStreaming" }
  | { readonly type: "setTerrainTuning"; readonly tuning: TerrainTuningConfig };
```

This union should grow deliberately as UI becomes real. Growing the command union
is preferable to adding many ad hoc methods, because every new command remains
observable, testable, serializable, and Rust-owned.

## Target Rust-To-Browser Interactions

Rust may interact with browser capabilities, but TypeScript should mediate as
little as possible.

| Browser capability | Target owner | TypeScript role |
|---|---|---|
| WASM module loading | TypeScript bootstrap | Load the module and call `create`. |
| Canvas element | TypeScript bootstrap passes it once | Rust owns WebGPU surface configuration after `create`. |
| WebGPU | Rust through `wgpu` | No TypeScript mediation. |
| DOM input events | TypeScript | Collect events and pass compact frame input to `tick`. |
| HTML HUD/debug UI | TypeScript | Read `debugSnapshot()` and send `command()` values. |
| Workers/threading | Rust-owned semantics, browser-provided execution | Prefer Rust-created workers or wasm threads. If TypeScript must host Workers, it must expose only an opaque worker service. |
| Asset loading | Rust-owned asset system | Prefer Rust using browser fetch/decode APIs directly. If TypeScript must help, expose a generic byte/image service with no terrain material semantics. |
| Console/panic hooks | Rust during development | Allowed for diagnostics; not part of game state. |

If Rust cannot directly create/manage browser Workers in the current toolchain,
TypeScript may temporarily provide an opaque worker host:

```ts
export type BrowserWorkerHost = {
  createPool(options: WorkerPoolOptions): WorkerPoolId;
  post(pool: WorkerPoolId, workerIndex: number, requestId: number, payload: Uint8Array, transfer?: Transferable[]): void;
  poll(pool: WorkerPoolId): readonly WorkerCompletion[];
  reset(pool: WorkerPoolId, generation: number): void;
  destroy(pool: WorkerPoolId): void;
};
```

The fallback has hard rules:

- TypeScript does not choose terrain work.
- TypeScript does not know density, LOD, chunk keys, material layers, or mesh
  packet structure.
- TypeScript does not inspect worker payloads.
- Rust owns request IDs, priorities, cancellation generations, dependency rules,
  retries, and result interpretation.

## Current State

Already Rust-owned:

| System | Rust owner | Current TypeScript status |
|---|---|---|
| Terrain height/density sampling | `crates/terrain_core` | Runtime TypeScript generator/noise code deleted. |
| Density chunk filling | `crates/terrain_core` | Browser bridge stores completed density jobs in Rust for scheduler bookkeeping, but no longer copies density chunks between main and worker WASM instances. |
| Dual Contouring mesh emission | `crates/terrain_core` | Runtime TypeScript meshing code deleted. |
| Terrain material/biome classification | `crates/terrain_core` | Runtime classification is Rust-owned; the duplicated TypeScript material list is deleted, but TypeScript still reads the checked-in texture asset manifest for browser image decode. |
| Terrain stream scheduling | `crates/terrain_core/src/stream.rs` | TypeScript bridge calls the scheduler but does not choose jobs itself. |
| Terrain retained density store | `crates/terrain_core/src/store.rs` | TypeScript adapter writes density job results and retains desired windows; it no longer reads chunks back for worker transfer. |
| Terrain worker-pool bookkeeping | `crates/terrain_core/src/worker_pool.rs` | TypeScript still constructs browser Workers; `BrowserWorkerHost` is payload-opaque and there is no TypeScript worker-pool fallback, but `TerrainChunkWorkerClient` still builds terrain-specific density/chunk request payloads. |
| Player/camera tick state | `crates/engine_web`, backed by `crates/engine_core` | Playable app no longer loads or builds a standalone `engine_core.wasm` artifact. |
| WebGPU renderer | `crates/engine_web/src/wgpu_renderer.rs` | TypeScript no longer creates devices, pipelines, buffers, render passes, or draw calls. |
| Terrain GPU mesh/texture handles | `crates/engine_web` | TypeScript still uploads terrain mesh bytes and decoded texture arrays into Rust. |
| Active terrain draw set | `crates/engine_web` | TypeScript adapter mirrors chunk keys for debug/smoke only. |
| Debug player marker mesh/material | `crates/engine_web` | TypeScript primitive marker mesh is no longer runtime-used. |
| Player/debug commands and player snapshot | `crates/engine_web`, backed by `crates/engine_core` | TypeScript sends player/debug command objects into Rust and reads player mode/position from Rust `debugSnapshot()`. |

Current public browser-facing Rust API in `src/engine/web/engineWebWasm.ts`:

```ts
create(canvas)
resize(width, height)
tick(frame)
command(command)
debugSnapshot()
upsertTerrainMesh(chunkKey, vertices, indices)
destroyTerrainMesh(chunkKey)
retainTerrainMeshes(chunkKeys)
clearTerrainMeshes()
upsertTerrainTextures(width, height, layers, formatCode, albedo, normal, material)
renderFrame()
```

Current runtime TypeScript that remains:

| Group | Runtime role | Target fate |
|---|---|---|
| App shell | Starts game, tracks input, updates HUD, exposes debug hooks, reads URL params, calls `game.tick(frame)`, sends `game.command(...)`, and reads `game.debugSnapshot()`. | Keep as browser shell. |
| WASM loading | Loads `engine_web` and `terrain_core.wasm` for the temporary worker bridge. | Keep only generic game module loading; remove runtime `terrain_core.wasm` calls. |
| Terrain worker transport | `BrowserWorkerHost` owns Worker lifecycle and request-id envelopes; `TerrainChunkWorkerClient` still builds terrain-specific density/chunk payloads, but worker results no longer carry string chunk keys, LOD chunk requests no longer include density buffers, and worker-pool bookkeeping has no TypeScript fallback. | Replace terrain-specific payload construction with Rust-owned worker/threading runtime or an opaque byte protocol. |
| Texture asset decode | Fetches the checked-in Poly Haven manifest and JPGs, draws them to canvas, reads RGBA pixels, uploads arrays into Rust. | Move terrain asset ownership behind Rust; TS may remain generic byte/image helper only. |
| Debug/smoke mirrors | `window.__ofgDebug` reads the TypeScript runtime `debugSnapshot()` and sends `game.command(...)`; player mode/position now come from Rust `debugSnapshot()`, while terrain stream/debug fields are still assembled by TypeScript. | Replace with a fully Rust-assembled `debugSnapshot()`. |

## Current Scorecard

| Target item | Current state | Status |
|---|---|---|
| TypeScript creates one Rust game facade | `createRustBrowserGameRuntime` wraps `RustBrowserGame` and other TS terrain systems. | Partial |
| TypeScript calls one frame method | App/runtime facade calls `game.tick(frame)` only; `RustBrowserGameAdapter` still calls wasm `renderFrame()` internally after terrain streaming. | Partial |
| Frame input is one object packet | App/runtime/adapter and the `engine_web` wasm-bindgen API use `BrowserFrameInput`; wasm-bindgen currently types the raw JS argument as `any` in generated d.ts. | Complete |
| UI/debug uses command lane | App/runtime/adapter and `engine_web.wasm` use object-shaped `command(command)` for player/debug actions; `resetStreaming` remains TypeScript-owned until terrain streaming moves behind Rust. | Partial |
| Debug/status uses one Rust snapshot | App reads `game.debugSnapshot()`; player mode, player position, and renderer status come from Rust `debugSnapshot()`, but terrain stream/debug fields are still assembled by the TypeScript runtime facade. | Partial |
| No public terrain mesh upload calls | `upsertTerrainMesh` and retention calls remain. | Pending |
| No public terrain texture upload calls | `upsertTerrainTextures` remains. | Pending |
| No direct TypeScript `terrain_core.wasm` runtime calls | `RustBrowserGameRuntime` and Workers still load/call `terrain_core.wasm`. | Pending |
| Worker semantics are Rust-owned and opaque to TS | Rust owns scheduler/pool and `BrowserWorkerHost` is payload-opaque; TS no longer moves density buffers, but terrain payloads still name density and chunk jobs. | Partial |
| Rust owns WebGPU directly | Rust/wgpu owns browser rendering. | Complete |

## Plan of Work

Work from the scorecard. Each slice should remove a named TypeScript category or
move one public call category to the target API.

First, delete test-only compiled TypeScript. This slice is complete: the live
terrain sink/packet types now live in
`src/engine/render/terrainRenderChunkSink.ts`, and the old packet-store module,
packet-store tests, primitive mesh module, primitive mesh tests, legacy
TypeScript material packing/mesh expansion helpers, and legacy TypeScript
density source/edit helpers are deleted.

Second, move app-facing calls to the target shape. This is complete at the
TypeScript runtime facade: `src/app` sends `BrowserFrameInput` packets, uses the
`GameCommand` lane for UI/debug actions, reads `GameDebugSnapshot`, and calls
only `game.tick(frame)` for each frame. The lower wasm-bindgen `tick` method now
accepts the same object-shaped frame packet; the remaining work in this area is
to delete the public `renderFrame()` method once Rust owns the terrain streaming
step that must currently run between tick and render.

Third, collapse terrain Worker semantics behind Rust. The generic browser host
piece is partially complete: `BrowserWorkerHost` sees only request ids and
opaque payloads, the density-transfer wrapper has been deleted, and the
TypeScript worker-pool fallback is gone. Remaining work is to replace
`TerrainCoreWorkerStreamer`, `terrainChunkWorkerClient`, and
`terrainChunkWorkerTypes` with Rust-owned wasm-thread/Worker support or a byte
protocol where TypeScript sees no density, LOD, chunk-key, or mesh semantics.

Fourth, move terrain texture asset ownership behind Rust. Delete
`upsertTerrainTextures` from the public TypeScript-to-Rust API. Rust should own
material manifests, layer ordering, texture-array validation, and upload
decisions. TypeScript may remain only as a generic browser asset helper if Rust
cannot directly perform one browser-only step.

Fifth, remove public terrain mesh upload calls. Delete `upsertTerrainMesh`,
`destroyTerrainMesh`, `retainTerrainMeshes`, and `clearTerrainMeshes` from the
public API by making Rust terrain streaming own mesh upload, retention, pruning,
and debug visibility.

Sixth, demote or delete standalone WASM wrappers that are not playable runtime.
This is complete for `engine_core.wasm`: the wrapper, generated metadata, build
script, package-script entries, checked-in artifact, and tests are deleted.

## Concrete Steps

Re-audit current TypeScript from `C:\dev\ofg` before deleting files:

    rg --files src | Sort-Object
    rg -n "TerrainCoreRenderPacket|primitiveMesh|engineCoreWasm|TerrainCoreWorkerStreamer" src docs AGENTS.md

For docs-only edits:

    git -c safe.directory=C:/dev/ofg diff --check

For TypeScript cleanup or API changes:

    npm test

For Rust/WASM boundary changes:

    npm run check:wasm
    cargo test -p engine_web
    cargo test -p terrain_core

For rendering, browser startup, worker, input, camera, HUD, or visual changes:

    npm run smoke:browser

After browser smoke, inspect `artifacts/browser-smoke/<run-id>/` screenshots and
`report.json`. A black screen after refresh is a failure.

## Validation and Acceptance

This plan is complete when the current scorecard is all complete:

- TypeScript creates one Rust game facade.
- TypeScript calls one frame method.
- Frame input is one object packet.
- UI/debug uses the command lane.
- Debug/status uses one Rust snapshot.
- There are no public terrain mesh upload calls.
- There are no public terrain texture upload calls.
- Runtime TypeScript does not instantiate or call `terrain_core.wasm` directly.
- Worker semantics are Rust-owned and opaque to TypeScript.
- Rust continues to own WebGPU directly.

A migration slice is acceptable only when it includes tests near the moved
behavior and passes the relevant commands from Concrete Steps. Browser smoke and
screenshot inspection are mandatory for rendering, browser startup, worker,
input, camera, HUD, or visual changes.

## Idempotence and Recovery

The documentation parts of this plan are safe to rerun. If the audit becomes
stale, rerun the `rg --files src` and reference searches, then update Current
State, Current Scorecard, Progress, Surprises & Discoveries, and Outcomes &
Retrospective.

Do not delete TypeScript files based only on this plan. Before any deletion,
search imports with `rg`, remove or replace tests intentionally, and run
`npm test`. If rendering, browser integration, terrain streaming, or input is
affected, run `npm run smoke:browser` and inspect the screenshots.

Do not reintroduce a TypeScript scene graph, ECS, terrain generator, terrain
manager, renderer, render packet assembler, or terrain-aware Worker scheduler.
If a browser capability truly requires TypeScript, keep the TypeScript API
generic and opaque, then record the reason here.

## Artifacts and Notes

The archived reduction ExecPlan recorded these useful artifacts:

    M AGENTS.md
    ?? PLANS.md

It also recorded that empty source directories were removed after being found
empty:

    src/engine/camera
    src/engine/scene
    src/game/components

The active-doc cleanup moved retired plans and reference snapshots under
`docs/archived/`. Those archived files are not active instructions. They are
available for historical context if needed.

## Interfaces and Dependencies

Stable target interface:

    game.tick(frameInput)
    game.resize(viewport)
    game.command(command)
    game.debugSnapshot()
    game.save()
    game.load(saveBytes)
    game.dispose()

Temporary or current interface elements that must disappear from the public
TypeScript/Rust boundary:

    renderFrame()
    upsertTerrainMesh(chunkKey, vertices, indices)
    destroyTerrainMesh(chunkKey)
    retainTerrainMeshes(chunkKeys)
    clearTerrainMeshes()
    upsertTerrainTextures(width, height, layers, formatCode, albedo, normal, material)

New TypeScript files must not become terrain schedulers, terrain mesh stores,
render resource owners, scene graphs, ECS systems, factory simulation owners, or
terrain-specific Worker protocols.

## Revision Note

2026-06-06: This plan was rewritten as a full ExecPlan after consolidating active
docs. It now preserves the content of the archived reduction ExecPlan and embeds
the browser/Rust API contract and TypeScript reduction audit directly.
