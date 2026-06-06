# TypeScript Reduction Audit

This document is the current state of TypeScript in the Rust-first migration.
It should stay blunt and current. When TypeScript files are deleted, moved to
test support, or replaced by Rust-owned browser APIs, update this audit and
`docs/RUST_ENGINE_PLAN.md`.

The exact target TypeScript/Rust boundary is defined in
[BROWSER_RUST_API.md](BROWSER_RUST_API.md). Use that contract to decide whether a
remaining TypeScript file is long-term shell, temporary browser substrate, or
migration debt.

The goal is not to reduce TypeScript forever by smaller and smaller amounts. The
goal is to delete whole remaining categories until TypeScript is a browser shell:
startup, DOM input, UI, debug panels, URL parameters, and browser capability
bridges that Rust cannot directly own.

## Already Rust-Owned

These systems should not be reintroduced in TypeScript:

| System | Rust owner | TypeScript status |
|---|---|---|
| Terrain height/density sampling | `crates/terrain_core` | Runtime TypeScript generator/noise code deleted. |
| Density chunk filling | `crates/terrain_core` | Browser uses WASM exports; old TS filling helper remains only in `terrainChunk.ts` tests/reference. |
| Dual Contouring mesh emission | `crates/terrain_core` | Runtime TS meshing code deleted. |
| Terrain material/biome classification | `crates/terrain_core` | Runtime classification is Rust-owned; TS still has material asset metadata. |
| Terrain stream scheduling | `crates/terrain_core/src/stream.rs` | TS bridge calls the scheduler but does not choose terrain jobs itself. |
| Terrain retained density store | `crates/terrain_core/src/store.rs` | TS adapter still moves buffers between WASM instances and Workers. |
| Terrain worker-pool bookkeeping | `crates/terrain_core/src/worker_pool.rs` | TS still constructs browser Workers and posts messages. |
| Player/camera tick state | `crates/engine_web`, backed by `crates/engine_core` | Playable app no longer loads `engine_core.wasm` for active player movement. |
| WebGPU renderer | `crates/engine_web/src/wgpu_renderer.rs` | TypeScript no longer creates devices, pipelines, buffers, render passes, or draw calls. |
| Terrain GPU mesh/texture handles | `crates/engine_web` | TS still uploads terrain mesh bytes and decoded texture arrays into Rust. |
| Active terrain draw set | `crates/engine_web` | TS adapter mirrors chunk keys for debug/smoke only. |
| Debug player marker mesh/material | `crates/engine_web` | TS primitive marker mesh is no longer runtime-used. |

## Keep Long Term As TypeScript

These files are not migration failures. They are browser shell or UI code that
can remain TypeScript unless the project later chooses a Rust-owned browser loop.

| Files | Current role | Notes |
|---|---|---|
| `src/main.ts` | Browser entry point. | Loads the app module. |
| `src/app/game.ts` | Canvas/HUD/game-loop shell, URL seed/preset parsing, DOM input forwarding, debug hook setup, coarse calls to `game.tick()` and `game.renderFrame()`. | This is close to the desired shell shape. It still exposes terrain debug functions because smoke tests need them. |
| `src/app/frameTiming.ts` | Frame delta clamp helper. | Fine as shell utility. |
| `src/app/styles.css` | Browser UI styling. | Fine as UI. |
| `src/engine/input/inputTracker.ts` | DOM keyboard/mouse tracking. | Rust should interpret movement, but DOM event collection can stay TS. |
| `src/engine/web/browserGameTypes.ts` | Small browser input/debug snapshot types. | Acceptable while TS forwards input frames. |
| `src/engine/browser/browserWorkerGroup.ts` | Generic browser Worker group utility. | Acceptable only if it stays generic and terrain-free. |

## Temporary Browser Substrate

These files are legitimate browser bridges today, but they are also the main
reason TypeScript still understands too much terrain. The next migration slices
should remove or hide these as categories.

| Files | Current role | Rust ownership state | Deletion path |
|---|---|---|---|
| `src/engine/web/rustBrowserGameRuntime.ts` | Coarse TS runtime that loads Rust, starts terrain Workers, loads texture assets, owns stream debug accessors, and coordinates tick/render. | Rust owns player, renderer, terrain state, and draw submission; TS still assembles worker/asset transport. | Move worker orchestration and asset loading behind a Rust browser facade so the app constructs one `RustBrowserGame` and calls coarse methods only. |
| `src/engine/web/rustBrowserGameAdapter.ts` | Thin wrapper over wasm-bindgen `RustBrowserGame`; uploads terrain mesh bytes by chunk key, uploads texture arrays, mirrors live chunk keys for debug. | Rust owns actual GPU resources and active draw set. | Push live chunk-key debug snapshots into Rust, then collapse this into a generic wasm loader or delete it. |
| `src/engine/web/terrainCoreWorkerStreamer.ts` | Terrain-aware bridge that asks Rust scheduler for jobs, gathers density dependencies, posts worker work, and reports completions. | Rust owns scheduler/store/pool state; TS still performs terrain job transport. | Replace with Rust-owned worker/threading runtime or a generic worker host where TS sees opaque job bytes instead of density chunks and LOD jobs. |
| `src/engine/world/terrainChunkWorkerClient.ts` | Terrain Worker client, message routing, Worker reset/dispose, transfer-list handling. | Rust owns task IDs and assignment when `terrain_core.wasm` is present. | Replace with a generic worker submission utility plus Rust-owned job payload protocol, or wasm threads. Delete the TypeScript fallback worker pool. |
| `src/engine/world/terrainChunkWorker.ts` | Browser Worker entry point that loads `terrain_core.wasm`, fills density chunks, installs apron densities, and builds LOD0 meshes. | The algorithms are Rust; the worker script is terrain-aware TS glue. | Move worker entry/job loop to Rust/wasm-thread support or make it an opaque Rust module worker shim with no terrain semantics in TS. |
| `src/engine/world/terrainChunkWorkerTypes.ts` | Terrain-specific Worker message and result contracts. | Rust owns the concepts; TS owns the message schema. | Replace with Rust-defined job/result byte packets exposed by the browser facade. |
| `src/engine/world/terrainDensityTransfer.ts` | SharedArrayBuffer/transfer wrapping for density payloads. | Rust owns density stores; TS wraps browser buffers. | Delete once Rust owns worker memory layout or job packets. |
| `src/engine/world/terrainCoreDensityChunkStore.ts` | TS adapter over Rust retained density store exports. | Rust owns storage; TS still pulls/pushes density payloads. | Delete when worker execution no longer requires TS to inspect retained density chunks. |
| `src/engine/world/terrainCoreStreamScheduler.ts` | TS adapter over Rust stream scheduler exports. | Rust owns scheduling. | Delete when `engine_web` calls scheduler internally and TS no longer ticks terrain jobs. |
| `src/engine/world/terrainCoreWorkerPool.ts` | TS adapter over Rust worker-pool exports. | Rust owns worker-pool model. | Delete when TS no longer pairs browser Worker slots with terrain tasks. |
| `src/engine/world/terrainCoreDensityChunk.ts` | TS helper for calling Rust density chunk fill and copying the WASM buffer. | Rust owns generation; TS copies the result for Worker transport. | Delete when density jobs stay inside Rust/worker-owned memory. |
| `src/engine/world/terrainCoreChunkMesh.ts` | TS helper for installing density chunks and calling Rust mesh generation in a Worker. | Rust owns meshing; TS wires per-worker WASM memory. | Delete when chunk meshing is driven by Rust worker/thread code. |
| `src/engine/world/terrainCoreWasm.ts` | Raw `terrain_core.wasm` loader and export assertions. | Necessary while TS directly calls terrain_core. | Demote to test support or hide under `engine_web` once terrain_core is an internal Rust dependency of the browser game facade. |
| `src/engine/web/engineWebWasm.ts` | wasm-bindgen loader and browser compatibility shim for `engine_web`. | Rust owns game/renderer; TS must load browser WASM. | Keep only a minimal generic loader/compatibility shim. Avoid terrain-specific API shaping here. |

## Asset And Contract Bridges

These files are partly useful, but they mix long-term browser data with
Rust-owned terrain/render concepts. They should be split before deletion.

| Files | Current role | What can remain | What should move or die |
|---|---|---|---|
| `src/engine/render/textureLoader.ts` | Browser image fetch/decode into RGBA arrays. | Generic browser asset loader if Rust still asks TS to fetch browser assets. | Terrain-specific ownership should move into Rust asset management. |
| `src/engine/render/terrainTextures.ts` | Builds albedo/normal/roughness texture arrays from material URLs. | Maybe a thin asset manifest loader. | Terrain texture-array assembly and validation should move behind Rust or a generic asset bridge. |
| `src/engine/world/terrainMaterials.ts` | Material IDs, texture URLs, layer count, and old TS material packing helpers. | The checked-in material asset manifest may stay until Rust owns asset manifests. | Packing helpers and layer logic are Rust-owned and should be removed from compiled TS once tests migrate. |
| `src/engine/world/terrainDescriptor.ts` | Seed/preset descriptor and URL-facing validation. | URL/UI-facing seed and preset parsing can remain TS. | Climate/material palette behavior should be Rust-owned; descriptor should become a serializable config packet. |
| `src/engine/world/terrainChunk.ts` | Chunk constants, coord/key helpers, density chunk class, edit helpers, and a TS density fill helper. | Chunk key parsing may remain until TS no longer sees chunk keys. | `TerrainDensityChunk`, `EditableTerrainDensitySource`, edit helpers, and `generateTerrainDensityChunk()` are test/reference leftovers and should move out of compiled runtime. |
| `src/engine/world/terrainMesh.ts` | Vertex layout constants, material packing, triangle material-palette expansion, color helper. | Shader-layout tests may need constants while TS owns shader metadata checks. | Runtime terrain mesh layout, material packing, and palette expansion are Rust-owned; this file should be test support or deleted after test migration. |
| `src/engine/math/*.ts` | TS vector/matrix/quaternion helpers. | Browser shell and tests can keep small math helpers while debug APIs return structured vectors. | Any math used only to support deleted TS render/terrain helpers should be trimmed after those helpers go. |
| `src/generated/render/uberShader.ts` | Generated WGSL source metadata for TS tests. | Shader source metadata tests can remain. | Runtime Rust renderer should not depend on generated TS shader source. |

## Test-Only Or Legacy Candidates

These are the clearest places to remove compiled TypeScript code next.

| Files | Evidence | Recommended next action |
|---|---|---|
| `src/engine/world/primitiveMesh.ts` and `primitiveMesh.test.ts` | `rg` finds runtime imports only from its own test. The debug player marker mesh is Rust-owned. | Delete both files, or move to a test fixture outside compiled `src` if still useful. Run `npm test`. |
| `src/engine/render/TerrainCoreRenderPackets.ts` | The packet-store class and mirror helper are not on the playable mesh handoff. Live code imports only the packet/sink types; tests still exercise the old store. | Split `TerrainRenderChunkSink` and packet types into a small browser contract file, update imports, then delete or move `TerrainCoreRenderPacketStore` tests. Rust-side mesh packet store coverage should live in Rust/WASM tests. |
| `src/engine/core/engineCoreWasm.ts` and `engineCoreWasm.test.ts` | The playable app no longer loads `engine_core.wasm`; `engine_web` links `engine_core` as a Rust library. | Decide whether standalone `engine_core.wasm` is still a supported artifact. If not, remove the build artifact, generated metadata, wrapper, and tests. If yes, label it explicitly as a test/dev artifact, not runtime architecture. |
| TypeScript terrain density/edit helpers inside `terrainChunk.ts` | Live bridge code needs chunk coords/keys, not TS density sources or edit application. | Split chunk coord/key contracts from TS density/edit reference helpers. Move helpers to test support or delete after Rust edit APIs exist. |
| TypeScript material packing helpers inside `terrainMaterials.ts` and `terrainMesh.ts` | Runtime material classification and mesh layout are Rust-owned; current uses are mostly tests and legacy primitive mesh. | Keep only asset manifest/URL metadata needed for texture loading. Move or delete packing/layout helpers after shader tests use Rust or fixed metadata. |

## Remaining Runtime TypeScript By File Group

This is the shortest honest summary of what still executes in the browser:

| Group | Runtime TypeScript still does | Why it remains |
|---|---|---|
| App shell | Starts the game, tracks input, updates HUD, exposes debug hooks, reads URL params, calls `game.tick()` and `game.renderFrame()`. | Browser DOM and UI are TypeScript-owned by design. |
| WASM loading | Loads `engine_web` wasm-bindgen module and `terrain_core.wasm` for the temporary worker bridge. | Browser module loading is still TS; terrain_core should become internal to Rust/browser facade where possible. |
| Terrain worker transport | Creates module Workers, posts terrain-specific density/mesh jobs, resolves results, resets Workers. | Rust cannot currently spawn browser Workers directly in this codebase; this is the largest remaining terrain-aware TS category. |
| Density payload movement | Wraps/copies/shared-buffers density chunks between the main terrain_core WASM instance and worker-local terrain_core WASM instances. | Worker memory ownership is not yet Rust-managed across browser Workers. |
| Texture asset decode | Fetches checked-in JPGs, draws them to canvas, reads RGBA pixels, uploads arrays into Rust. | Browser image decode APIs are convenient in TS; Rust asset ownership is still unfinished. |
| Debug/smoke mirrors | Tracks live chunk keys and exposes terrain status snapshots to `window.__ofgDebug`. | Smoke tests need observability. This should move to Rust debug snapshots. |

## Recommended Next Implementation Slices

Do these as category deletions, not partial wrapper shrinkage.

1. **Delete test-only compiled TypeScript.**
   Split live terrain sink/packet types out of `TerrainCoreRenderPackets.ts`,
   then delete the old packet-store class if its coverage is redundant with
   Rust/WASM tests. Delete `primitiveMesh.ts` and its test. This is a low-risk
   cleanup that makes the remaining source tree more honest.

2. **Collapse terrain Worker semantics behind Rust.**
   Replace `TerrainCoreWorkerStreamer`, `terrainChunkWorkerClient`,
   `terrainChunkWorkerTypes`, and density-transfer wrappers with either
   Rust-owned wasm-thread support or an opaque generic Worker host where TS sees
   bytes and request IDs, not terrain coords, density chunks, or LOD names.
   This directly closes the Worker rows in `docs/BROWSER_RUST_API.md`.

3. **Move terrain texture asset ownership behind Rust.**
   Keep TS only as a browser fetch/decode primitive if needed. Rust should own
   the material manifest, layer ordering, texture-array validation, and texture
   upload decisions. This deletes `upsertTerrainTextures` from the public
   TypeScript-to-Rust API.

4. **Demote standalone WASM wrappers that are not playable runtime.**
   Decide whether `engine_core.wasm` remains a supported dev/test artifact. If it
   does, document it as such. If not, delete its TS wrapper, generated metadata,
   build step, and tests.

5. **Move debug snapshots to Rust.**
   Replace TS chunk-key mirrors and terrain status aggregation with a Rust debug
   snapshot from `engine_web`. Browser smoke can still read it through
   `window.__ofgDebug`, but TS should not assemble terrain state. This replaces
   individual player/status getters with `debugSnapshot()`.

## Stopping Rule

A Rust migration slice is not done merely because less TypeScript remains. It is
done when one named TypeScript category has either disappeared from compiled
`src/` or been reduced to a generic browser primitive with no terrain, render, or
world semantics.
