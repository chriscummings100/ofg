# AGENTS

This file is the first stop for AI agents working in this repository. It gives the
project shape, the commands to trust, and the deeper docs to read before changing a
system.

## Project Goal

OFG is a browser-native online factory game prototype. The long-term direction is a
lightweight custom engine with voxel terrain, Dual Contouring, WebGPU rendering, and
a Rust/TypeScript toolchain that stays friendly to automated AI development.

The current playable seed is still simple:

- Generated heightfield terrain.
- First-person camera/player movement.
- Debug fly camera toggled with `C` or `F1`.
- A yellow player marker visible in debug fly mode.
- WebGPU renderer using generated WGSL shader artifacts.

The current terrain is deliberately not Dual Contouring yet.

## Read These When Needed

- [README.md](README.md): setup, commands, and high-level project shape.
- [docs/ROADMAP.md](docs/ROADMAP.md): milestone direction.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): current architecture overview.
- [docs/SCENE_MODEL_PLAN.md](docs/SCENE_MODEL_PLAN.md): scene/entity/component model
  and its intended test coverage.
- [docs/BROWSER_VERIFICATION.md](docs/BROWSER_VERIFICATION.md): screenshot and
  browser interaction verification.
- [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md): expected agent loop and testing habits.

If context is compacted or you are unsure about scene architecture, reread
`docs/SCENE_MODEL_PLAN.md` before continuing.

## Commands

```powershell
npm run build
npm run build:shaders
npm run check:shaders
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
  Browser setup, game loop, HUD, and scene bootstrapping.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/camera
  Legacy tested camera rig helpers. The current playable camera is driven by
  PlayerController plus SceneRenderExtractor.

src/engine/world
  Seed terrain scalar field, heightfield mesh generation, primitive box mesh.

src/engine/scene
  Global Scene, Entity tree, Component lifecycle, Transform hierarchy,
  ResourceStore, and related tests.

src/engine/render
  WebGPU renderer plus scene render data types. Runtime rendering flows through
  MeshRenderer, TerrainRenderer, RenderWorld, and SceneRenderExtractor.
  Materials currently support albedo factor, CPU-side albedo texture id, specular,
  and specular factor; the shader uses Lambert plus Blinn-Phong lighting.
  `RenderWorld.mainLight` also drives the procedural sky sun disk.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact before
  `tsc` runs.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules.

src/game/components
  Game-level components such as PlayerController.

tools
  Local scripts, including shader generation, the static dev server, and browser
  smoke test.
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

The playable app is scene-model backed: terrain, player, camera, and the debug
player marker are scene entities/components. Keep new runtime behavior on that
path unless a change explicitly targets legacy camera helpers.

## Testing Expectations

This project should be test-heavy because it is intended to be heavily AI-built.

Current test areas include:

- Math: vectors, quaternions, matrices through transform behavior.
- Scene core: active scene lifecycle, entity hierarchy, component lifecycle,
  transform propagation, resource storage.
- Render data: mesh/material/texture metadata, mesh renderer, terrain renderer,
  render extraction.
- Shader boundary: generated shader source artifact metadata and vertex layout
  contract.
- World mesh generation: heightfield and primitive meshes.
- Gameplay/input: player controller, camera rig, input tracker.
- Browser smoke: actual Chrome/Edge WebGPU render, screenshots, pixel checks, HUD
  and camera toggle verification.

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
- Do not migrate to Rust/WASM until TypeScript contracts are stable and tested.
- Keep shader work behind `tools/build-shaders.mjs` so the Slang build path can
  replace the current WGSL source step cleanly.

## Git Notes

There may be Windows ownership warnings from Git. Use the repository-safe-directory
flag if needed:

```powershell
git -c safe.directory=C:/dev/ofg status
```

Do not commit generated `dist/`, `node_modules/`, or `artifacts/` output.
