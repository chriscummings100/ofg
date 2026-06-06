# AGENTS

This file is the first stop for AI agents working in this repository. It gives the
project shape, the commands to trust, and the deeper docs to read before changing a
system.

## Project Goal

OFG is a browser-native online factory game prototype. The long-term direction is a
lightweight custom engine with voxel terrain, Dual Contouring, WebGPU rendering, and
a Rust/TypeScript toolchain that stays friendly to automated AI development.

The current playable seed is still simple:

- Chunk-streamed generated terrain from 3D density chunks.
- Runtime terrain meshed as per-chunk neighbor-aware Dual Contouring chunks.
- Poly Haven terrain materials rendered from global WebGPU texture arrays.
- Rust/WASM terrain core artifact that owns terrain height/density sampling,
  chunk meshing, stream scheduling, density storage, and mesh packet storage.
- Rust-owned first-person camera/player movement through `engine_web.wasm`,
  backed by `engine_core`.
- Rust/wgpu WebGPU renderer through `engine_web.wasm`; Rust owns browser draw
  submission, terrain mesh handles, texture handles, and renderer pruning.
- Debug fly camera toggled with `C` or `F1`.
- A yellow player marker visible in debug fly mode.
- WebGPU renderer using generated WGSL shader artifacts.

The current terrain uses same-LOD per-chunk Dual Contouring. LOD transitions and
far-field terrain are still future terrain architecture work.

## Read These When Needed

- [README.md](README.md): setup, commands, and high-level project shape.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): current architecture overview.
- [docs/RUST_CONVERSION_PLAN.md](docs/RUST_CONVERSION_PLAN.md): single active
  plan for making the app almost entirely Rust-owned, including the target
  TypeScript/Rust boundary and scorecard.
- [docs/TERRAIN_PLAN.md](docs/TERRAIN_PLAN.md): living terrain realism plan.
- [docs/TERRAIN_GEN_RESEARCH.md](docs/TERRAIN_GEN_RESEARCH.md): terrain
  generation research reference.
- [PLANS.md](PLANS.md): OpenAI/Codex ExecPlan standard for substantial
  multi-step work.

If context is compacted or you are unsure about engine ownership, reread
`docs/RUST_CONVERSION_PLAN.md`. If terrain realism or terrain generation is
involved, also reread `docs/TERRAIN_PLAN.md`.

## Archived Docs

`docs/archived/` contains retired plans and reference snapshots. Documents in
that folder are not active instructions. Use them only for historical context
when explicitly needed.

When an active plan finishes or is replaced, move it to `docs/archived/` and add
a short note explaining where the active source of truth moved.

## ExecPlans

Use an ExecPlan for multi-step or multi-file work, new features, refactors, or
tasks expected to take more than about an hour. Follow [PLANS.md](PLANS.md):
read it before drafting, keep the plan self-contained, and update its living
sections as work proceeds.

When implementing from an ExecPlan, continue to the next milestone without
asking for next steps unless blocked. Keep Progress, Surprises & Discoveries,
Decision Log, and Outcomes & Retrospective current at every stopping point, and
make acceptance criteria observable with exact commands or screenshots where
relevant.

## Commands

```powershell
npm run clean
npm run build
npm run build:shaders
npm run check:shaders
npm run build:wasm
npm run check:wasm
npm run bench:terrain:wasm
npm test
npm run smoke:browser
npm run dev
```

Use `npm test` for logic changes. Use `npm run smoke:browser` whenever rendering,
input, camera behavior, HUD behavior, or browser integration changes.

`npm run smoke:browser` launches installed Chrome/Edge through Playwright Core,
saves screenshots in `artifacts/browser-smoke/`, samples pixels, and verifies the
`FIRST -> FLY` camera toggle.

## Agent Workflow

1. Read the nearest module and test before changing behavior.
2. Make the smallest coherent change.
3. Run `npm test` for logic changes.
4. Run `npm run build` when build output or generated artifacts may be affected.
5. Run `npm run smoke:browser` for visual, browser, input, camera, HUD, worker,
   or rendering changes.
6. Summarize what changed, what was verified, and any remaining risk.

Prefer behavior-focused test names such as `grounds first-person player on sampled
terrain` or `rejects stale worker completions after reset`.

## Browser Verification

`npm run smoke:browser`:

- Builds the TypeScript app.
- Starts a temporary local dev server.
- Launches installed Chrome/Edge through Playwright Core.
- Saves screenshots under `artifacts/browser-smoke/`.
- Reads HUD state and samples screenshot pixels to catch blank or solid frames.
- Reloads the page and fails on black or blank refresh frames.
- Presses `C` and verifies the camera mode changes from `FIRST` to `FLY`.
- Moves the player across terrain chunk columns through debug hooks and verifies
  chunk streaming.

Useful environment variables:

- `OFG_SMOKE_PORT`: preferred local port. Defaults to `5174`.
- `OFG_BROWSER_PATH`: explicit Chromium-based browser executable path.
- `OFG_SMOKE_HEADED=1`: launch a visible browser for debugging.

This is a smoke test, not a full visual diff. Extend it as interactions become
important.

## Code style

- Code should be designed to be human readable
- Avoid huge files, or tiny files. Over 1000 lines is concerning.
- Files should have comments at the top saying what they do
- Comments at the top of a file can act as a living document - notes on decisions made
  for it can be stored and re-read.
- Well commented functions are important.
   - All functions should always have a description of what they do
   - Long functions should have internal comments to explain their steps
- Avoid overengineering, such as:
   - Tiny wrapper functions that're only called once, and could just be used directly
   - Complex data structures in anticipation of features we don't need
   - Backwards compatibility - we will never need this
- Cleanup functions that are no longer needed as you go - it is better to rewrite something than
  keep dead code around just in case its needed later.
- Commit and push regularly to git

## Current Architecture

```text
src/app
  Browser setup, game loop, HUD, URL seed/preset parsing, input forwarding,
  debug hooks, and calls into the `RustBrowserGameRuntime` facade through frame
  input packets, commands, debug snapshots, and the transitional `renderFrame`
  call.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` owns Worker lifecycle,
  request-id envelopes, resets, disposal, and completion forwarding without
  understanding terrain job payloads.

src/engine/world
  Browser-side terrain descriptor/config types, 3D density chunk data contracts,
  Rust/WASM terrain adapters, generic browser worker transport, terrain material
  metadata, and terrain mesh vertex layout helpers. Compiled TypeScript no
  longer owns terrain generation, noise, Dual Contouring, terrain streaming
  policy, density transfer between worker WASM instances, or a terrain manager.

src/engine/render
  Browser-side texture loading helpers, shader metadata tests, and the temporary
  terrain render chunk sink contract used by the browser Worker bridge. The
  playable browser path no longer has a TypeScript WebGPU renderer or
  `RenderWorld`; runtime worker mesh results are handed to `RustBrowserGame` by
  chunk key, and Rust/wgpu owns
  actual WebGPU resources and draw submission.
  Terrain rendering uses global 16-layer albedo, normal, and roughness texture
  arrays. Normal maps are loaded but not yet applied in shading.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact for
  shader contract tests, and the Rust renderer includes the shared WGSL source.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules
  and Rust/WASM terrain, engine, and engine-web artifact metadata.

crates/engine_core
  Browser-free Rust engine core. It owns player/camera logic, a small
  world/entity ID model, transforms, and render snapshot logic. It is tested as
  a native Rust crate and reached by the playable browser app through
  `engine_web`; no standalone `engine_core.wasm` browser artifact is built.

crates/terrain_core
  Rust terrain core built to wasm32-unknown-unknown. It owns macro base
  elevation, density, compatibility height sampling, density chunk filling,
  browser runtime chunk mesh generation, stream scheduling, density storage,
  worker-pool state, and the tested legacy terrain mesh packet store. It is now
  the browser terrain source of truth; the playable mesh handoff currently goes
  from worker results straight into `engine_web`/Rust-wgpu terrain mesh handles.

crates/engine_web
  Browser-facing Rust game/render bridge built to wasm32-unknown-unknown. It
  owns the active browser player/camera tick state, Rust/wgpu renderer, WebGPU
  resource handles, terrain texture handles, terrain mesh handles, live terrain
  draw set, and frame draw submission.

src/engine/web
  Browser-facing TypeScript shell around Rust/WASM systems. It loads
  `RustBrowserGame`, hosts the temporary terrain Worker transport, forwards
  input/debug commands, uploads decoded terrain texture arrays and worker mesh
  bytes to Rust, and keeps browser-only compatibility shims. It should keep
  shrinking toward a generic browser shell with no terrain semantics.

tools
  Local scripts, including shader generation, Poly Haven terrain texture import,
  the static dev server, and browser smoke tests.
```

## Runtime Ownership Rules

The compiled TypeScript scene/component model has been retired. Do not recreate a
new TypeScript scene graph, ECS, terrain generator, or terrain manager as the next
step.

- Rust owns active browser player/camera state through `engine_web.wasm`, using
  `engine_core` as a Rust library.
- Rust owns generated terrain sampling, Dual Contouring mesh emission, stream
  scheduling, density stores, worker-pool state, and terrain mesh packet stores
  through `terrain_core.wasm`.
- Rust owns browser WebGPU resource creation and draw submission through
  `engine_web.wasm` and `wgpu`.
- TypeScript currently owns browser startup, DOM input collection, URL parameter
  parsing, debug hooks, browser Worker transport, terrain mesh/texture byte
  upload adaptation, and terrain status/debug mirrors below the browser runtime
  facade.
- New high-volume world, terrain streaming, simulation, render extraction, and
  WebGPU ownership should follow `docs/RUST_CONVERSION_PLAN.md`.
- Use `docs/RUST_CONVERSION_PLAN.md` before deleting or adding TypeScript around
  terrain, rendering, or engine ownership.

## Testing Expectations

This project should be test-heavy because it is intended to be heavily AI-built.

Current test areas include:

- Math: vectors, quaternions, and matrices.
- Render data: mesh/material/texture metadata and Rust-backed terrain render
  packet store.
- Shader boundary: generated shader source artifact metadata and vertex layout
  contract.
- World terrain: 3D density chunks, terrain edits, primitive meshes, terrain
  material packing, Rust-owned chunk streaming, and Rust/WASM terrain core
  sampling/mesh/stream fixtures.
- Gameplay/input: Rust browser game/player facade and input tracker.
- Browser smoke: actual Chrome/Edge WebGPU render, screenshots, pixel checks, HUD
  camera toggle verification, and a basic player-position chunk streaming check.

When adding behavior, add tests near the behavior first or in the same change.
Prefer behavior names such as `rejects stale worker completions after reset`.

## Design Bias

- Keep the engine lightweight and browser-native.
- Prefer deterministic pure logic in small modules.
- Add abstractions only when they match the architecture plan or remove real
  duplication.
- Keep WebGPU details behind render-facing boundaries.
- Do not introduce a full ECS.
- Use Rust as the target home for world, simulation, terrain streaming, render
  extraction, and eventually WebGPU rendering. Migrate by tested vertical slices
  that remove TypeScript ownership rather than by adding parallel systems.
- Prefer deleting or demoting whole TypeScript categories named in
  `docs/RUST_CONVERSION_PLAN.md` rather than repeatedly shrinking wrappers while
  preserving terrain-aware TypeScript.
- Keep shader work in plain WGSL behind `tools/build-shaders.mjs`. Do not introduce
  alternate shader languages unless the project direction changes again.

## Git Notes

There may be Windows ownership warnings from Git. Use the repository-safe-directory
flag if needed:

```powershell
git -c safe.directory=C:/dev/ofg status
```

Do not commit generated `dist/`, `node_modules/`, or `artifacts/` output.
