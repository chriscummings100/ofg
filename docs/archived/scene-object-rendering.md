# Render scene objects through Camera, DrawList, Graphics, and Game

This ExecPlan follows [PLANS.md](../../PLANS.md). Completed 2026-10-03 under the approved implementation scope; replaces the cube-only proposal. Existing user changes were preserved.

## Purpose and contract

Render a procedural scene on D3D12 and browser WebGPU. Scene owns stable entities and typed Camera/MeshRenderer components. Setters invalidate cached descendant world matrices. One selected perspective Camera uses left-handed +Z-forward coordinates and depth [0,1]. Mesh owns immutable position/normal/UV vertices, uint32 triangle indices, material-bearing submeshes, bounds and private GPU buffers. Shader owns source/entry names and private compiled program. Material owns named scalar/vector/matrix values and shared Shader; explicit cloning copies values and shares dependencies. Overrides are nullable per-submesh slots.

Ready procedural resources bypass weak keyed lookup; CPU readiness is separate from GPU preparation. GPU-independent DrawList culls mesh bounds in homogeneous clip space and retains mesh/material/world-matrix entries in creation order. Graphics owns device/queue lifecycle, depth and pipelines keyed by weak Shader identity/color format; other settings are fixed (triangles, single sample, D32Float Less with writes, no blend/cull). Game updates Resources, root State, current Scene, then builds/renders the selected camera list. Hosts retain events/acquisition/presentation. Null targets update only; absent camera clears.

## Progress

- [x] (2026-10-03) Approved design and repository/RHI inspection.
- [x] (2026-10-03) CPU scene/resources/draw-list implemented; cpu-tests passes.
- [x] (2026-10-03) GPU rendering, all uniform types, depth, cache and release-after-submit tests pass in native-debug.
- [x] (2026-10-03) Game/two-host integration, native presentation and browser smoke verified.
- [x] (2026-10-03) Ownership/API/readability review, documentation, formatting and links checked; archived.

## Surprises & Discoveries

Slang caches both module names and source paths. Same-name Shader and device-recreation tests exposed cache collisions; unique internal names/paths now preserve independent source identity while keeping human source names in diagnostics.

The pinned WebGPU backend yields through Asyncify during initial buffer uploads and uniform staging maps. The startup-only allowlist crashed during a frame; the failing exported function identified the missing `dynCall_vi` callback thunk. OFG/RHI/frame calls and callback thunks are now covered, while the embedded Slang compiler remains excluded. Emscripten pauses/resumes its main loop across these yields.

Once callback instrumentation was corrected, WebGPU reported expired canvas textures: a canvas image cannot survive event-loop turns. Rendering into host-owned storage fixes that lifetime boundary. A final texture-copy experiment failed because the pinned browser surface reports no CopyDestination capability. A resource-only fullscreen image-load pass presents the completed image without uniform staging or a yield. Native pixel tests and browser resize/reload now verify it. Smoke treats unexpected warnings as failures.

SDK cache write denial previously stalled a sandboxed link. Only inspected task processes were stopped; building with the supplied full access resolved it without replacing dependencies or deleting caches.

## Decision Log

2026-10-03: User chose procedural scope, named typed uniforms, setter-based lazy transforms, one vertex layout/camera and passive components. No importer, material textures, lighting, generic ECS, sorting, spatial index, multiple camera passes or large-world coordinates. Private forward-declared graphics data keeps resources GPU-independent. Reflection validates every direct material field; missing/extra/unsupported/mismatched fields fail. Mesh/shader GPU data are weakly tracked for shutdown; CPU assets survive device reinitialization.

2026-10-03: Retain RHI command/backend resource ownership instead of adding a second retirement framework. Pipelines observe Shader control-block identity, not reusable addresses; fixed pipeline settings are documented beside the key. Each draw has independent shader-object uniform storage.

2026-10-03: Add the small `src/render/present.*` helper and `shaders/present.slang` to resolve the demonstrated browser canvas lifetime/capability boundary. This is host presentation plumbing, with no new Material uniform type or texture resource API. Keep the RHI submodule unchanged.

## Context and interfaces

Extend src/scene/entity and scene; add camera and mesh-renderer. Add src/resources/mesh, material and shader. src/render/draw-list belongs to ofg-core; graphics and private resource-gpu-data to ofg-render. src/game and src/lab/scene-fixture drive the shared demonstration; shaders/mesh.slang is embedded. Both hosts retain selectable checkerboard diagnostics. No dependencies added.

## Plan of Work

1. Add transform setters/cache, typed ownership/selection, ready resources, mesh validation, clones/overrides and conservative culling. Prove CPU contracts.
2. Add lazy GPU preparation, per-draw uniform binding, depth and pipeline cache. Verify indexing/depth/packing/all uniform types/errors/cache reuse/release-after-submit.
3. Add Game and a stationary fixture with two shared cubes, tinted clone, parented/overlapping/off-camera objects. Integrate hosts; verify resize/reload/missing-WebGPU and save native/browser evidence under artifacts/scene-rendering.
4. Review correctness/ownership/readability/comments and update architecture, contracts, DEVELOPING.md and build skills.

## Concrete Steps and Acceptance

Use build-native/build-web skills and DEVELOPING.md environments. Configure/build/test cpu-tests and native-debug presets, configure/build web and run npm.cmd run smoke:web. These commands were executed successfully; results follow below. Test ancestor/root edits, cached reads, scale, moves/clear, uniqueness/camera invalidation; invalid mesh data, overrides/clones/retained lists; all clip boundaries and singular cameras; indexed GPU geometry/depth/uniforms/errors/cache/lifetime; Game order/replacement/no-target/no-camera; native/browser presentation. Preserve checkerboard/startup tests. Use explicit tolerances and scenario coverage; no performance or percentage claims.

## Idempotence and Recovery

Reuse build trees and pinned dependencies, preserve user edits and historical backup. Explicit shutdown resets live asset GPU state for reinitialization. Report errors without fallback materials.

## Milestone Review

Reviewed CPU/RHI separation, observer/shared ownership, component-before-entity destruction, weak pipeline identity, explicit matrix packing, material slot reset/clone semantics and host shutdown. The device-recreation regression releases the first native device completely, then renders surviving CPU assets on a newly created device. Releasing draw assets immediately after submission leaves the encoded geometry valid. Browser presentation uses no uniform staging after canvas acquisition.

Project declarations document contracts; source ownership remains explicit and typed. Obsolete mutable transform callers were migrated. Architecture, state/scene/resource contracts, development instructions, bootstrap references and both build skills reflect the implemented behavior. clang-format 22.1.3 dry-run, `git diff --check` and local Markdown link checks passed. Vendored RHI and the historical backup are unchanged.

## Outcomes & Retrospective

All four implementation stages are complete. The default native/browser application renders two shared six-submesh cubes, one independent cyan face material, parented transforms and an off-camera instance. Checkerboard diagnostics and finite native startup remain available. The scene is stationary; application State can edit camera, transforms and materials through ordinary APIs.

CPU tests establish cache invalidation/root/ancestor/nonuniform scale behavior, typed lifetime/move/clear, material cloning/overrides, mesh validation, retained draw lists, all clip boundaries and invalid camera inputs. Native GPU tests establish indexed geometry, overlapping depth, independent bindings, every supported uniform type, row/column matrix packing, binding diagnostics, shader/format cache identity, new-device preparation and post-submit lifetime. Game ordering, scene replacement, reentry rejection, no-target updates and no-camera clearing pass.

Validation on 2026-10-03:

| Check | Actual result |
| --- | --- |
| cpu-tests configure/build/CTest | Passed; 55 cases / 959 assertions, no RHI dependency. |
| native-debug configure/build/CTest | All four CTest checks passed: core, startup, checkerboard and scene rendering. Render suite: 7 cases / 178 assertions. |
| Native presentation | NVIDIA GeForce RTX 3050 Ti Laptop GPU, D3D12, initial client 960x640; resize, minimize/restore and normal close passed; exit 0 and empty stderr. |
| web build | Emscripten 6.0.0, Slang 2026.17.1; final WASM 27,140,650 bytes uncompressed. |
| `npm.cmd run smoke:web` | Passed in Chrome 154.0.8037.95: scene and original checkerboard, resize/reload, missing-WebGPU message. No errors or validation warnings; only Chrome's known Windows power-preference warning. |
| Browser captures | 960x641 initial scene and 773x478 resized/reloaded scene; inspected checker geometry and cyan override. Browser query reported Intel gen-12lp; RHI omitted its adapter description. |

Native evidence is under `artifacts/scene-rendering/native` (`scene.png`, `scene-resized.png`, `scene-restored.png`, logs and local capture probe). Browser evidence is under `artifacts/scene-rendering/browser` (six PNGs and `report.json`); the web build log is `artifacts/scene-rendering/web-build.log`. Representative native/browser images were inspected and presented during implementation. Reproduce using [DEVELOPING.md](../../DEVELOPING.md) and the repository build skills.

The Emscripten Asyncify/WASM-exception build warning remains: smoke establishes the exercised paths, not arbitrary exceptions through suspended calls. Slang sessions may retain compiler internals until device destruction. No percentage coverage, performance, long-running residency or identical cross-backend pixel claim is made. The important lesson was to validate complete frame/presentation lifetimes, not infer browser readiness from a successful build or initialization.
