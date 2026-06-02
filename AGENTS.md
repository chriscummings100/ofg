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
- First Rust/WASM terrain core artifact with golden tests against the TypeScript
  terrain generator.
- Rust-owned first-person camera/player movement through `engine_core.wasm`.
- Debug fly camera toggled with `C` or `F1`.
- A yellow player marker visible in debug fly mode.
- WebGPU renderer using generated WGSL shader artifacts.

The current terrain uses same-LOD per-chunk Dual Contouring. LOD transitions and
far-field terrain are still future terrain architecture work.

## Read These When Needed

- [README.md](README.md): setup, commands, and high-level project shape.
- [docs/ROADMAP.md](docs/ROADMAP.md): milestone direction.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): current architecture overview.
- [docs/RUST_ENGINE_PLAN.md](docs/RUST_ENGINE_PLAN.md): Rust-first engine
  migration, including Rust-owned WebGPU through `wgpu`.
- [docs/SCENE_MODEL_PLAN.md](docs/SCENE_MODEL_PLAN.md): scene/entity/component model
  and its intended test coverage. This is now historical/transitional guidance,
  not the target architecture for high-volume world systems.
- [docs/BROWSER_VERIFICATION.md](docs/BROWSER_VERIFICATION.md): screenshot and
  browser interaction verification.
- [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md): expected agent loop and testing habits.

If context is compacted or you are unsure about engine ownership, reread
`docs/RUST_ENGINE_PLAN.md` and `docs/terrainplan.md` before continuing.

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

## Current Architecture

```text
src/app
  Browser setup, game loop, HUD, and current scene bootstrapping. Long term this
  becomes the TypeScript shell around the Rust engine.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/world
  Seed terrain scalar field backed by low-frequency x/z noise plus octave 3D
  simplex density detail with gradients, 3D density chunk model, runtime Dual
  Contouring Hermite extraction and guarded QEF placement, terrain material
  classification and packed material weights, legacy highest-surface and
  heightfield mesh generation, primitive box mesh. Runtime terrain meshes use
  position/color/normal/uv/material-index/material-weight vertex data.

src/engine/scene
  Global Scene, Entity tree, Component lifecycle, Transform hierarchy,
  ResourceStore, and related tests. This is current prototype infrastructure and
  should not become the high-volume world authority.

src/engine/render
  WebGPU renderer plus scene and packet render data types. Runtime rendering
  flows through MeshRenderer, TerrainRenderPacketStore for streamed terrain
  chunks, RenderWorld, and SceneRenderExtractor.
  Materials currently support albedo factor, albedo/normal/material texture
  resources, specular, and specular factor; the shader uses Lambert plus
  Blinn-Phong lighting. Terrain rendering uses global 16-layer albedo, normal, and
  roughness texture arrays; normal maps are loaded but not yet applied in shading.
  `RenderWorld.mainLight` also drives the procedural sky sun disk. Rust/wgpu is
  the target renderer once Rust render packets exist.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact before
  `tsc` runs.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules
  and Rust/WASM terrain and engine artifact metadata.

crates/engine_core
  Rust engine core built to wasm32-unknown-unknown. It owns player/camera state,
  a small world/entity ID model, transforms, and the first render packet snapshot
  bridge for camera/light/player-marker data.

crates/terrain_core
  Rust terrain core built to wasm32-unknown-unknown. It mirrors TypeScript macro
  base elevation, density, compatibility height sampling, density chunk filling,
  and the browser runtime chunk mesh path. Keep migrated terrain slices
  golden-tested against TypeScript until a Rust path is intentionally promoted as
  source of truth.

Future crates
  `docs/RUST_ENGINE_PLAN.md` proposes a browser-facing Rust/WASM renderer crate.
  New world/simulation/render ownership should generally move in that direction.

src/game/components
  Game-level compatibility components such as RustPlayerController and
  TerrainChunkStreamer.

tools
  Local scripts, including shader generation, Poly Haven terrain texture import,
  the static dev server, and browser smoke tests.
```

## Scene Model Rules

There is one global active `Scene`.

- Use `createScene()`, `getScene()`, `setScene()`, and `resetScene()` from
  `src/engine/scene/activeScene.ts`.
- Tests should call `resetScene()` to isolate global scene state.
- Entities form a tree and always have a `Transform`.
- Components attach to one entity at a time.
- Components may call `getScene()` when they need global context.
- Scene resources are CPU-side descriptions. Do not put WebGPU handles in
  `ResourceStore`.
- Render extraction produces plain `RenderWorld` data. The WebGPU renderer should
  not know about entities.
- `scene.mainLight` is the sun: use it for world lighting and sky placement.

The playable app is currently partly scene-model backed: the terrain streamer,
mirrored player entity, and debug player marker are scene entities/components,
but streamed terrain chunks render through `TerrainRenderPacketStore` rather than
`TerrainRenderer`. The authoritative player/camera state and first camera/light
render packet are now Rust-owned. Treat the remaining scene path as transitional
runtime glue. New high-volume world, terrain streaming, simulation, render
extraction, and WebGPU ownership should follow `docs/RUST_ENGINE_PLAN.md`.

## Testing Expectations

This project should be test-heavy because it is intended to be heavily AI-built.

Current test areas include:

- Math: vectors, quaternions, matrices through transform behavior.
- Scene core: active scene lifecycle, entity hierarchy, component lifecycle,
  transform propagation, resource storage.
- Render data: mesh/material/texture metadata, mesh renderer, terrain renderer,
  terrain render packet store, render extraction.
- Shader boundary: generated shader source artifact metadata and vertex layout
  contract.
- World terrain: simplex noise generation, 3D density chunks, baseline field
  sampling, terrain edits, Dual Contouring meshing, highest-surface legacy meshing,
  chunk streaming, heightfield and primitive meshes, Rust/WASM terrain core
  golden fixtures.
- Gameplay/input: Rust player controller adapter and input tracker.
- Browser smoke: actual Chrome/Edge WebGPU render, screenshots, pixel checks, HUD
  camera toggle verification, and a basic player-position chunk streaming check.

When adding behavior, add tests near the behavior first or in the same change. Prefer
behavior names such as `reparenting removes the child from its previous parent`.

## Browser Verification Workflow

For visual or interactive work:

1. Run `npm test`.
2. Run `npm run smoke:browser`.
3. Inspect screenshots in `artifacts/browser-smoke/<run-id>/` when behavior or
   framing matters.
4. Check `report.json` for HUD state, WebGPU availability, pixel stats, and console
   messages.

The smoke test is designed to catch blank frames, solid-color regressions, broken
WebGPU startup, and camera toggle failures. Extend it as new interactions become
important.

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
- Keep shader work in plain WGSL behind `tools/build-shaders.mjs`. Do not introduce
  alternate shader languages unless the project direction changes again.

## Git Notes

There may be Windows ownership warnings from Git. Use the repository-safe-directory
flag if needed:

```powershell
git -c safe.directory=C:/dev/ofg status
```

Do not commit generated `dist/`, `node_modules/`, or `artifacts/` output.
