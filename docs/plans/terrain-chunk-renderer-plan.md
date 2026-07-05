# Terrain Chunk Renderer v0

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

This plan starts the procedural terrain system by making terrain real in the renderer. The first user-visible result is a rendered scene where C++ owns a scene-level `Terrain` object, generates deterministic addressable `TerrainChunk` data, and renders one ordinary render object per chunk. The renderable terrain path is deliberately direct: `Terrain` owns terrain-level materials, each `TerrainChunk` owns its generated renderable data, and `Terrain::extract_render_objects()` packages existing chunks into the same transient render-object shape the renderer already consumes for scene objects.

The goal is not to solve final async or multi-LOD terrain streaming yet. The goal is to establish the first stable terrain contract: a `Scene` owns terrain like it owns `Environment`, terrain owns a streamed map of addressable chunks keyed by `(LOD, X, Y, Z)`, C++ can generate the same heightfield every time, adjacent surface chunks line up, each chunk can build a local-space mesh, and the result is visible in the game before we try to reason about richer generation.

There must be no `terrain_scene.cpp`, `TerrainScene`, renderer-side terrain chunk map, reusable terrain mesh slot table, visibility-time terrain mesh generation, or equivalent bridge. Debug visualization is still allowed, but it must follow the same per-chunk data model: `TerrainChunk` owns `Ptr<Mesh> m_debug_plane_mesh` and `Ptr<Texture> m_debug_plane_texture`, while `Terrain` owns `Ptr<Material> m_debug_plane_material`. The renderer should only receive ordinary render objects, whether those render objects point at clay terrain meshes or debug planes. The only new terrain ownership classes for this plan are `Terrain` and `TerrainChunk`, plus simple value types such as ids, config, tick context, render mode, and samples.

## Progress

- [x] (2026-07-04 10:25+01:00) Reviewed `docs/research/terrain-research-overview.md`, `docs/research/terrain-research-implementation.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, and `docs/SYSTEMS.md`.
- [x] (2026-07-04 10:25+01:00) Captured the user direction that CDLOD is out of scope for this first step, while addressable single-LOD mesh chunks and visible rendering are in scope.
- [x] (2026-07-04 10:25+01:00) Revised Milestones 1 and 2 around scene-owned `Terrain`, chunk maps keyed by `(LOD, X, Y, Z)`, heightfield debug textures, debug chunk quads, and later clay heightfield meshes.
- [x] (2026-07-04 10:25+01:00) Removed browser-facing terrain debug/editor APIs from this plan; terrain editor work will be planned separately on top of ImGui.
- [x] (2026-07-04 12:00+01:00) Ran the `$review-plan` skill with correctness, completeness, clarity, efficiency, and performance reviewers, then accepted all required review findings for incorporation into this ExecPlan.
- [x] (2026-07-04 12:00+01:00) Updated the plan to reflect that ImGui is now integrated; terrain editor work remains out of scope here and should be planned separately on top of ImGui.
- [x] (2026-07-04 20:00+01:00) Created the scene-owned C++ `Terrain` and `TerrainChunk` model, added deterministic sine-octave heightfield generation, wired `Scene::terrain()`, and added doctests for chunk ids, ticking, determinism, edge agreement, and scene ownership.
- [x] (2026-07-04 20:08+01:00) Added the narrow `R16Float` texture path, binary16 packing helpers, clamp/nearest sampler behavior for half-float textures, and texture doctests for conversion, storage, and update validation.
- [x] (2026-07-04 20:12+01:00) Updated material GPU binding layout selection so `R16Float` textures use unfilterable-float texture bindings and non-filtering sampler bindings; added a Dawn-backed material test for that path.
- [x] (2026-07-04 20:20+01:00) Rendered one heightfield debug quad per generated terrain chunk, replaced the demo checker ground renderer, added the height-debug WGSL shader, updated demo-scene tests/counts, and broadened native/browser smoke surface classification for red/green terrain debug output.
- [x] (2026-07-04 20:45+01:00) Revised Milestone 1 streaming ownership: `Terrain::tick(...)` now reconciles the wanted 5 by 5 chunk window by unloading unwanted chunks and generating missing chunks. The current draw-slot resource path is now considered stale implementation debt and must be replaced by direct terrain render-object extraction before this plan is complete.
- [x] (2026-07-04 21:05+01:00) Fixed renderer resource churn exposed by streamed terrain browser smoke: compatible material bind-group layouts now share a cache key so terrain chunk textures do not force one pipeline per material, and the opaque pass starts with a bounded 256-draw uniform capacity instead of growing during camera-mode exercises.
- [x] (2026-07-05 05:30+01:00) Re-scoped the plan around the direct terrain rendering ownership contract.
  Evidence: this plan now requires no `terrain_scene.cpp`, `Terrain` ownership of `Ptr<Material> m_material` and `Ptr<Material> m_debug_plane_material`, chunk ownership of `Ptr<Mesh> m_render_mesh`, `Ptr<Mesh> m_debug_plane_mesh`, and `Ptr<Texture> m_debug_plane_texture`, generation-time render mesh creation, local-space chunk mesh vertices, and `Terrain::extract_render_objects()` as the only terrain render handoff.
- [x] (2026-07-05 08:20+01:00) Stripped the stale terrain scene bridge out of code.
  Evidence: deleted `cpp/include/ofg/terrain/terrain_scene.hpp` and `cpp/src/terrain/terrain_scene.cpp`, removed `TerrainSceneResources` from `DemoScene`, removed per-frame terrain debug sync from `Game`, and kept `Terrain`/`TerrainChunk` as the only terrain ownership types with terrain-owned material pointers and chunk-owned render/debug resource pointers.
- [x] (2026-07-05 09:02+01:00) Restored debug plane rendering through the new per-chunk layout.
  Evidence: `TerrainChunk` now generates its own debug plane mesh, R16Float debug texture, and texture-backed material override; `Terrain::extract_render_objects()` appends ordinary render objects for chunks in debug plane mode; `DemoScene` assigns a terrain-owned debug material without `TerrainSceneResources`.
- [ ] Replace the stale terrain render bridge with direct extraction over chunk-owned clay and debug render resources.
- [ ] Update system and API contract docs for both debug rendering and clay mesh rendering.
- [x] (2026-07-04 20:12+01:00) Ran `npm run format:cpp` and `npm run test:cpp`; native C++ tests passed with the new terrain, R16Float texture, and non-filtering material tests.
- [x] (2026-07-04 20:22+01:00) Ran `npm run format:cpp`, `npm run test:cpp`, `npm run smoke:render`, and `npm run smoke:browser`; browser and native smoke passed with terrain debug planes visible.
- [x] (2026-07-04 21:10+01:00) Re-ran validation after the streaming/material-layout/opaque-capacity revisions: `npm run format:cpp`, `npm run test:cpp`, `npm run format:cpp:check`, `git diff --check`, `npm run smoke:browser`, and `npm run smoke:render` pass. `git diff --check` reports only existing line-ending warnings.
- [x] (2026-07-04 21:12+01:00) Refreshed durable terrain debug screenshots and reports from the latest passing native/browser smoke runs under `C:\dev\ofg\artifacts\terrain-debug`.
- [ ] Run milestone review and record outcomes.

## Surprises & Discoveries

- Observation: The repository already records that future terrain generation belongs behind an owning C++ subsystem rather than in `Game`.
  Evidence: `C:\dev\ofg\docs\SYSTEMS.md` says `Game` is orchestration glue and names terrain generation as feature-specific behavior that should not accumulate in `game.cpp`.

- Observation: The current worktree contains unrelated bloom/render changes.
  Evidence: `git status --short` on 2026-07-04 showed modified render, bloom, docs, and test files before this plan was created. This plan should avoid touching those files unless terrain integration truly requires it.

- Observation: The existing `Texture` resource currently exposes RGBA8 initialization only.
  Evidence: `C:\dev\ofg\cpp\include\ofg\resources\texture.hpp` provides `TexturePixelFormat::Rgba8` and `TexturePixelFormat::Rgba8Srgb`, and `Texture::init_from_rgba8_pixels(...)`. Milestone 1 must either extend `Texture` for half-precision height data or add a narrow terrain-owned half-float texture path.

- Observation: ImGui is already integrated in the active project direction.
  Evidence: The user clarified this on 2026-07-04 after the plan review. Terrain editor controls should use ImGui in later work, but this terrain renderer plan should not grow an editor UI.

- Observation: The shared smoke report field named `groundPixels` still works as the stable schema, but its classifier now needs to mean authored terrain/ground surface instead of only neutral checker pixels.
  Evidence: Native smoke first failed after terrain debug rendering with `Ground coverage too low: 0.007500`, then passed after native and browser classifiers accepted red/green height-debug terrain pixels as terrain surface coverage.

- Observation: Browser smoke exercises enough camera movement to expose lazy resource creation from terrain debug chunks.
  Evidence: After Milestone 1 switched to streamed chunk reconciliation, browser smoke first reported `Pipeline count changed after mode exercise: 24 -> 37` because per-chunk terrain materials had distinct bind-group layouts. After layout sharing, smoke then reported `Buffer count changed after mode exercise: 23 -> 24` because the opaque draw-uniform buffer grew when the visible object count crossed its previous capacity.

## Decision Log

- Decision: Do not implement CDLOD in this plan.
  Rationale: The user has a more precise future streaming model in mind, and CDLOD is unnecessary for proving terrain generation, chunk addressing, mesh output, and visual integration.
  Date/Author: 2026-07-04 / Codex

- Decision: The first terrain render path uses addressable chunks with exactly one LOD.
  Rationale: Chunk keys make future streaming explicit while keeping the first renderer milestone small enough to test and inspect.
  Date/Author: 2026-07-04 / Codex

- Decision: Add a scene-owned `Terrain` object, similar in ownership level to `Environment`.
  Rationale: Terrain is world state, not a renderer detail or temporary demo-scene helper. `Scene` should own terrain so future simulation, streaming, collision, and editor systems have a stable world-facing home.
  Date/Author: 2026-07-04 / User

- Decision: `Terrain` owns a map of `TerrainChunk` objects keyed by a unique `(LOD, X, Y, Z)` id.
  Rationale: Even while rendering only LOD0 surface chunks, the chunk address should match the user's intended streaming model and keep the door open for vertical chunking and later volumetric features.
  Date/Author: 2026-07-04 / User

- Decision: Treat a LOD0 terrain chunk as 32 by 32 by 32 cells, with a conceptual dual grid of 33 by 33 by 33 vertices.
  Rationale: At LOD0, one cell maps to one meter, so each chunk covers a 32 meter cube in world space. Milestone 1 uses only the X/Z heightfield surface of this chunk model, but the address and dimensions should not be 2D-only.
  Date/Author: 2026-07-04 / User

- Decision: Keep chunk cell dimensions out of `TerrainConfig`.
  Rationale: `32` cells per LOD0 chunk edge is an engine terrain constant, and LOD0 cell world size is simply one meter, so neither value should be per-terrain configuration.
  Date/Author: 2026-07-04 / User

- Decision: Milestone 1 renders one ordinary render object per generated terrain chunk through chunk-owned meshes.
  Rationale: Rendering comes before deeper terrain reasoning, and the renderer should only consume ordinary render objects. The normal terrain mode should point those render objects at chunk-owned clay render meshes. The debug plane mode may point them at chunk-owned debug plane meshes instead.
  Date/Author: 2026-07-04 / User, amended 2026-07-05 / User

- Decision: The Milestone 1 heightfield generator can be a few octaves of sine waves.
  Rationale: The first phase validates ownership, chunk addressing, texture generation, and rendering. Real noise, biome logic, and erosion would obscure those fundamentals.
  Date/Author: 2026-07-04 / User

- Decision: Milestone 2 remains the follow-on meshing/debug-hardening milestone, but it must not introduce new ownership classes.
  Rationale: The mesh path is the useful terrain renderer path and should use the existing opaque shader/material path so current sun shadows are cast onto it. Additional debug visualization can be retained for later ImGui switching only when it stays owned by `Terrain` or `TerrainChunk` and reuses the same `Terrain::extract_render_objects()` handoff.
  Date/Author: 2026-07-04 / User, amended 2026-07-05 / User

- Decision: Do not expose terrain debug/editor data through the browser facade in this plan.
  Rationale: Browser-facing terrain payloads would grow quickly and duplicate editor concerns. The terrain editor should be built through a later ImGui terrain editor plan, not through TypeScript DOM or WASM debug payload expansion.
  Date/Author: 2026-07-04 / User

- Decision: Keep terrain editor work separate from this renderer plan and build it on top of the already integrated ImGui path.
  Rationale: Terrain controls, visualization, and editor workflow should live in an in-engine editor surface rather than in TypeScript DOM or ad hoc WASM debug payloads, but the first terrain milestone should focus on generated chunk data and visible rendering.
  Date/Author: 2026-07-04 / User

- Decision: Terrain generation behavior should be object-owned, not free global functions.
  Rationale: `Terrain` owns its config and can expose sampling as a member function. `TerrainChunk` owns its generated heightfield data and stable references to its generated render mesh and optional debug plane data, so generation should update chunk state rather than return a separate `TerrainChunkMesh` data object.
  Date/Author: 2026-07-04 / User

- Decision: `Terrain` needs an update/tick function that accepts a generation origin.
  Rationale: Terrain should initially generate a small region, such as 5 by 5 chunks, around the player or another supplied origin. This establishes the future streaming shape without adding asynchronous queues or multiple LODs yet.
  Date/Author: 2026-07-04 / User

- Decision: `Terrain::tick(...)` owns the first streaming reconcile loop.
  Rationale: Scene should only tick terrain around an origin and should not reason about chunk membership. Each tick compares the current map with the wanted chunk set, drops chunks outside the wanted set, and creates and generates chunks that are missing.
  Date/Author: 2026-07-04 / User, implemented by Codex

- Decision: Remove the terrain render bridge and draw-slot mapping from the target architecture.
  Rationale: Terrain chunks are streamed terrain data, not scene mesh slots or renderer cache entries. The renderer should receive a transient list of ordinary render objects built from chunks that already exist. If a chunk is evicted, its mesh dies with that chunk; no slot may be retargeted to another chunk id.
  Date/Author: 2026-07-05 / User direction carried forward by Codex

- Decision: `Terrain` owns the terrain material pointer.
  Rationale: The first terrain renderer has one material, the clay opaque terrain material, and it belongs to terrain state. `Terrain` should store it as `Ptr<Material> m_material` and use it when extracting render objects. `Scene` may create or assign the material during setup, but it must not own per-chunk terrain render state.
  Date/Author: 2026-07-05 / User

- Decision: `Terrain` owns the debug plane material pointer.
  Rationale: The coloured heightfield debug plane is a terrain render mode, not a separate renderer subsystem. `Terrain` should store the shared debug plane material as `Ptr<Material> m_debug_plane_material`, and each debug render object should use draw/material property overrides to bind the chunk's own debug plane texture.
  Date/Author: 2026-07-05 / User

- Decision: `TerrainChunk` owns its generated render mesh.
  Rationale: Mesh lifetime should follow chunk lifetime. Each chunk should store `Ptr<Mesh> m_render_mesh`; if backing storage is needed, it still belongs to the chunk or terrain lifetime, not to a separate terrain scene, renderer slot pool, or scene mesh renderer.
  Date/Author: 2026-07-05 / User

- Decision: `TerrainChunk` owns optional debug plane resources.
  Rationale: Debug visualization can grow without making the renderer terrain-aware as long as it remains per-chunk data. Each chunk may store `Ptr<Mesh> m_debug_plane_mesh` and `Ptr<Texture> m_debug_plane_texture`; `Terrain::extract_render_objects()` chooses whether to expose those debug planes or the chunk render meshes.
  Date/Author: 2026-07-05 / User

- Decision: Mesh generation is the final step of chunk generation.
  Rationale: A generated chunk should be ready to render. `Terrain::extract_render_objects()` should package existing chunk meshes, not generate missing meshes, allocate resources, or repair stale render state while the renderer is asking what to draw.
  Date/Author: 2026-07-05 / User

- Decision: Terrain mesh vertices are chunk-local.
  Rationale: Large worlds should avoid storing large absolute positions in low-precision vertex data. The mesh vertices should be generated in the local coordinate space of the chunk, with the render object's model transform placing the chunk at its world origin.
  Date/Author: 2026-07-05 / User

- Decision: Share compatible material bind-group layouts by structural layout key.
  Rationale: Terrain debug rendering uses one texture/material per visible chunk. If each material owns a distinct bind-group layout, the opaque pipeline cache treats structurally identical terrain materials as separate pipeline layouts and creates pipelines lazily as chunks become visible. Sharing compatible layouts keeps pipeline creation bounded by shader/material schema instead of chunk count.
  Date/Author: 2026-07-04 / Codex

- Decision: Start the opaque pass with a 256-draw uniform capacity.
  Rationale: The default scene now includes 25 streamed terrain debug draws plus the validation object field. A tiny one-draw initial capacity caused ordinary camera exercises to grow durable draw-uniform buffers after warmup. A 256-draw buffer is small and covers the current default terrain/debug scene without steady-state buffer churn.
  Date/Author: 2026-07-04 / Codex

- Decision: Debug planes with no art/albedo texture and clay-material heightfield meshes are acceptable for this plan.
  Rationale: The first important proof is chunk identity, generated height data, seams, scale, camera readability, and render integration. Debug planes deliberately use a per-chunk heightfield texture, while clay meshes prove the proper opaque terrain path. Materials, biome masks, triplanar shading, and erosion-driven textures can follow after terrain is visible.
  Date/Author: 2026-07-04 / Codex, amended 2026-07-05 / User

- Decision: Milestone 2 remains part of this plan.
  Rationale: The first useful terrain renderer is the generated mesh path, so Milestone 1 should prove chunk visibility and Milestone 2 should convert the same data into clay-rendered terrain. It is not a stretch goal to remove from scope.
  Date/Author: 2026-07-04 / User

## Outcomes & Retrospective

Not started. At completion, summarize what terrain chunks render, what validation passed, and which terrain/editor decisions remain open.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`: TypeScript may continue to host the browser canvas, raw input, runtime loading, status text, and smoke helpers, but it must not own terrain simulation, chunk generation, mesh data, terrain editor state, scene graph state, renderer internals, GPU handles, or gameplay world data. This plan does not add TypeScript terrain editor controls.

This plan extends `OFG-BOOT-002 C++ Runtime Ownership`: C++ owns the scene-level `Terrain`, terrain configuration, terrain sampling behavior, the terrain chunk id, the `TerrainChunk` map, deterministic heightfield generation, optional chunk-owned debug texture data, chunk mesh construction, terrain ticking around a supplied generation origin, and direct terrain render-object extraction. `Terrain` stores the current clay terrain material pointer as `Ptr<Material> m_material` and the shared debug plane material pointer as `Ptr<Material> m_debug_plane_material`. `TerrainChunk` stores generated terrain data, its generated render mesh as `Ptr<Mesh> m_render_mesh`, and optional debug plane resources as `Ptr<Mesh> m_debug_plane_mesh` plus `Ptr<Texture> m_debug_plane_texture`. `Game` should only orchestrate scene setup and status plumbing; terrain-specific behavior belongs in `Scene`, `Terrain`, or `TerrainChunk`, not in a separate terrain scene or renderer-side chunk table.

This plan preserves `OFG-BOOT-003 WASM Facade`: no terrain debug or editor API should be added to the browser facade in this plan. The facade must not expose raw terrain object pointers, mutable scene ownership, mesh pointers, GPU handles, renderer internals, or bulk terrain data.

This plan preserves `OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline`: browser and native rendering should remain on WebGPU-backed renderer paths, request no optional WebGPU features, and keep smoke-compatible sky, camera, player fallback/model, and opaque render behavior. Replacing the checker ground with terrain render objects is a visible scene-contract change, so `tools/smoke-contract.json`, browser smoke pixel classification, native render smoke expectations, and any demo-scene object/count tests must be updated deliberately in the same milestone that changes the scene. Terrain may add a heightfield-debug shader path and a clay material path, but screenshots and smoke thresholds must be updated deliberately if pixel classification changes. Half-precision heightfield textures must be implemented using WebGPU core features only; the initial target format is `R16Float` with no mips, non-filtering sampling or integer `textureLoad`, clamp-to-edge addressing if a sampler is used, and no optional float-filtering feature request.

This plan preserves `OFG-BOOT-006 Resource Lifetime`: terrain textures, chunk render meshes, chunk debug plane meshes, shaders, and materials may be created during scene preparation, material assignment, chunk generation, or explicit terrain mutation, not recreated during every ordinary steady-state frame. `Terrain::tick(...)` should be idempotent for an unchanged origin chunk and complete chunk set. Repeated same-origin ticks and several rendered frames after warmup should not increase terrain `Texture`, `Mesh`, `Shader`, `Material`, pipeline, or bind-group creation counters except for deliberately newly generated chunks. Evicted chunks release their render mesh, debug plane mesh, and debug plane texture with the chunk; an old mesh or texture must never be retargeted to a different chunk id.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Modified implementation files must meet the coverage attention gate unless an explicit exception is recorded here with rationale.

Repository readability rules apply from `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md`: files need purpose comments, functions need comments or docstrings, larger functions over 50 lines need internal explanation, C++ uses the repo clang-format config, and files above 500 lines should draw scrutiny.

## Context and Orientation

The current app is a C++/WASM runtime with a TypeScript browser host. TypeScript in `C:\dev\ofg\src\app` creates the canvas, loads the generated WASM module, forwards raw input, polls debug status, and displays a small status overlay. C++ in `C:\dev\ofg\cpp` owns the scene graph, resources, renderer, player/camera behavior, and WebGPU runtime behavior.

The current browser scene is built by `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It creates generated materials and meshes, a scene-owned 5 by 5 terrain chunk surface, a fallback player box or loaded player model, several colored cubes, a camera, and a sun light. `Game::prepare()` in `C:\dev\ofg\cpp\src\game\game.cpp` builds that scene, and `Renderer::render()` draws render objects extracted from the current `Scene` and its terrain.

A terrain chunk is a 32 by 32 by 32 cell world volume at LOD0, addressed by integer `(LOD, X, Y, Z)` coordinates. Its conceptual dual grid has 33 by 33 by 33 vertices, meaning there is one more sample point than cell count along each axis so neighboring chunks can share their boundary samples. These dimensions are fixed terrain constants, not `TerrainConfig` fields. Milestone 1 only generates and renders a heightfield surface over the chunk's X/Z footprint, but the key and dimensions should be 3D from the beginning. World space follows OFG's left-handed convention from `OFG-BOOT-002`: `+X` is right, `+Y` is up, and `+Z` is forward. Terrain height maps to world `Y`. In debug plane mode, chunk-owned debug planes sit at `y == 0` and visualize height through the chunk-owned debug plane texture.

Chunk coordinates use floor division, not truncation toward zero: a world `x` in `[0, 32)` maps to chunk `0`, a world `x` in `[32, 64)` maps to chunk `1`, and a world `x` in `[-32, 0)` maps to chunk `-1`. The same rule applies to `z`. The initial surface set always uses `lod = 0` and `chunk_y = 0`. The `Terrain::tick(...)` origin selects which chunks should exist; it must not affect `Terrain::sample(...)` results for a given world coordinate.

The research direction in `C:\dev\ofg\docs\research\terrain-research-implementation.md` recommends a heightfield-first architecture, procedural field stack, climate/hydrology later, and sparse volumetric terrain only after the surface pipeline works. This plan implements the first visible foundation only: deterministic heightfield chunks, generated heightfield mesh rendering, and optional chunk-owned debug-plane rendering. Editor UI is deliberately deferred to a later terrain editor plan built on top of the already integrated ImGui path.

Out of scope for this plan: CDLOD, geometry clipmaps, multiple LOD rings, asynchronous streaming queues, browser terrain debug APIs, TypeScript terrain editor UI, ImGui terrain editor UI, biome classification, hydrology, erosion, caves, volumetric meshing, digging, collision, vegetation placement, triplanar terrain materials, save/load deltas, networking, and final editor authoring workflows.

## Plan of Work

Milestone 1 creates the scene-owned terrain model and renders addressable chunks through direct render-object extraction. Add a `Terrain` object owned by `Scene`, similar in ownership level to `Environment`. `Terrain` owns a map of `TerrainChunk` objects keyed by `TerrainChunkId{lod, x, y, z}` and owns the current clay terrain material pointer as `Ptr<Material> m_material`. If the height debug plane path is retained, `Terrain` also owns the shared debug plane material as `Ptr<Material> m_debug_plane_material`. `Terrain::tick(...)` accepts a generation origin, initially the player position or a fixed origin when no player is available, and reconciles a 5 by 5 LOD0 surface region around the origin chunk. That means X/Z radius 2, `lod = 0`, and `chunk_y = 0`, not a 5 by 5 by 5 volume. `Terrain::tick(...)` must be idempotent for the same origin chunk, unload chunks outside the wanted set, and create and generate chunks missing from the wanted set. The first integration should tick terrain during scene preparation so the initial scene renders immediately, and then from `Scene::update(...)` after player movement and before render extraction so future streaming has an obvious home. LOD0 chunks are fixed at 32 by 32 by 32 cells, with one cell equal to one meter. Generate a deterministic heightfield for each surface chunk using a few octaves of sine waves. The final step of generating a chunk is creating its chunk-owned `Ptr<Mesh> m_render_mesh` from generated vertices in the local coordinate space of that chunk; if debug planes are enabled, generation may also create or refresh the chunk-owned `Ptr<Mesh> m_debug_plane_mesh` and `Ptr<Texture> m_debug_plane_texture`. `Terrain::extract_render_objects()` then converts each existing renderable chunk into an ordinary `RenderObject`: normal mode uses the chunk render mesh and `Terrain::m_material`, while debug plane mode uses the chunk debug plane mesh, `Terrain::m_debug_plane_material`, and per-chunk property/material overrides that bind that chunk's debug plane texture. This milestone proves chunk ownership, addressing, origin-centered ticking, heightfield generation, local-space mesh generation, first streamed chunk reconciliation, and visible per-chunk rendering.

Milestone 2 preserves and exposes optional heightfield debug data without changing ownership. If the half-precision heightfield texture and debug shader path remain from the previous attempt, they should be retained as the chunk-owned `m_debug_plane_texture` plus chunk-owned `m_debug_plane_mesh`, using the terrain-owned `m_debug_plane_material`. They must not require a terrain scene, scene mesh slots, renderer-side chunk identity, or a separate resource bridge. The primary renderer path stays the clay opaque chunk mesh path through `Terrain::extract_render_objects()`, and debug plane mode is just another output of that same extraction function.

Milestone 3 updates docs and validates. Add a `TerrainGeneration` section to `C:\dev\ofg\docs\SYSTEMS.md`, update `C:\dev\ofg\docs\API_CONTRACTS.md` for scene-owned terrain, chunk ids, debug rendering, and meshing, and record screenshots under `C:\dev\ofg\artifacts\terrain-debug`. Run tests, coverage, smoke, and the required milestone review before marking implementation complete.

## Concrete Steps

Run commands from `C:\dev\ofg` unless a command says otherwise.

1. Inspect current interfaces before editing:

       rg -n "build_demo_scene|setup_demo_scene|debug_status_json|BrowserGame|RawBrowserGame|Mesh::init" cpp src tests

2. Add terrain headers and implementation:

       C:\dev\ofg\cpp\include\ofg\terrain\terrain.hpp
       C:\dev\ofg\cpp\include\ofg\terrain\terrain_chunk.hpp
       C:\dev\ofg\cpp\src\terrain\terrain.cpp
       C:\dev\ofg\cpp\src\terrain\terrain_chunk.cpp

   Start with the smallest set of files that keeps ownership readable. Add `terrain_heightfield.hpp/.cpp` only if heightfield conversion grows beyond chunk-local logic. Do not add or keep `terrain_scene.hpp/.cpp`; if those files exist from the previous attempt, remove them from the build and delete the bridge rather than expanding it.

   Update `C:\dev\ofg\cpp\CMakeLists.txt` to compile the new source and new test.

3. Add native tests:

       C:\dev\ofg\cpp\tests\terrain_chunk_test.cpp
       C:\dev\ofg\cpp\tests\terrain_test.cpp

   The tests should cover `Terrain` ownership behavior, chunk map lookup, unique chunk ids, origin-centered `Terrain::tick(...)` generation, negative coordinate floor-division mapping, chunk-boundary positions, deterministic sine-octave height generation, neighbor edge agreement, no duplicate chunks for repeated same-origin ticks, invalid configuration errors, and non-finite origin handling. Milestone 2 should add mesh-count and mesh-edge tests.

4. Add scene ownership:

       C:\dev\ofg\cpp\include\ofg\scene\scene.hpp
       C:\dev\ofg\cpp\src\scene\scene.cpp

   Add `Scene::terrain()` and `Scene::terrain() const` accessors, with terrain owned similarly to `Environment`.

5. Add or extend texture support for half-precision terrain heightfields:

       C:\dev\ofg\cpp\include\ofg\resources\texture.hpp
       C:\dev\ofg\cpp\src\resources\texture.cpp
       C:\dev\ofg\cpp\tests\texture_resource_test.cpp

   Prefer a narrow, well-tested `R16Float` path that works without optional WebGPU features. The CPU upload data should be little-endian IEEE 754 binary16 values or pass through one tested conversion function. The debug texture has 33 by 33 texels for the initial grid unless implementation records a better sampling choice. Use no mip levels. Use clamp-to-edge plus non-filtering sampling, or use integer `textureLoad` in the shader. Define zero height as black, negative heights as red, positive heights as green, and clamp debug color intensity through a documented height-scale divisor.

6. Remove the terrain scene bridge and add direct chunk render extraction:

       C:\dev\ofg\cpp\include\ofg\terrain\terrain.hpp
       C:\dev\ofg\cpp\src\terrain\terrain.cpp
       C:\dev\ofg\cpp\include\ofg\terrain\terrain_chunk.hpp
       C:\dev\ofg\cpp\src\terrain\terrain_chunk.cpp
       C:\dev\ofg\cpp\src\render\render_object.cpp, only if shared extraction helpers need minor adjustment
       C:\dev\ofg\cpp\src\render\renderer.cpp, only to append terrain render objects beside scene mesh-renderer objects
       C:\dev\ofg\cpp\src\render\demo_scene.cpp, only to assign the clay material to `Scene::terrain()`

   Delete `cpp/include/ofg/terrain/terrain_scene.hpp` and `cpp/src/terrain/terrain_scene.cpp` if they exist, and remove their CMake entries. `Terrain::extract_render_objects()` should be the only terrain render handoff. It should not create chunks, generate meshes, allocate renderer resources, repair missing meshes, or synchronize chunk ids into a renderer-side table. It may choose between normal clay mesh mode and debug plane mode, but both modes must produce ordinary `RenderObject` entries from resources already owned by `Terrain` and `TerrainChunk`.

7. Generate chunk-owned heightfield render meshes and clay rendering:

       C:\dev\ofg\cpp\src\terrain\terrain_chunk.cpp
       C:\dev\ofg\cpp\src\terrain\terrain.cpp
       C:\dev\ofg\cpp\tests\terrain_chunk_test.cpp

   Mesh generation should produce 1089 vertices and 6144 indices per LOD0 chunk for the default 32-cell grid, use finite local-space positions, normals, UVs, and indices, derive edge normals from world samples so adjacent chunks agree, and feed the existing opaque material/shader path with a basic clay material so shadows cast onto the terrain mesh. Vertex `x` and `z` positions should be local to the chunk, normally `0..32`, with height in local `y`; the render object's model transform places the chunk at its world origin. Debug plane mesh generation, if retained, should follow the same local-space rule and stay stored on the chunk as `m_debug_plane_mesh`.

8. Update docs:

       C:\dev\ofg\docs\API_CONTRACTS.md
       C:\dev\ofg\docs\SYSTEMS.md
       C:\dev\ofg\tools\smoke-contract.json, when visible smoke classification changes
       C:\dev\ofg\docs\coverage\latest.md, only if coverage command updates committed summaries

9. Format and validate:

       npm run format:cpp
       npm run format:cpp:check
       npm run test:cpp
       npm run test:ts
       npm run build:wasm
       npm run smoke:browser
       npm run smoke:render
       npm run smoke
       npm run coverage

10. For visual validation during and before finalization:

       npm run dev

   Report the printed local URL. Capture screenshots regularly for browser/render work. Store durable terrain screenshots under `C:\dev\ofg\artifacts\terrain-debug` and present them in chat. Required screenshot moments are the first visible chunk-owned clay terrain meshes and the final browser-smoke-equivalent terrain view. Capture optional debug texture/plane screenshots only if the debug path is retained as chunk-owned data without a terrain scene bridge.

## Milestone Review

After each implementation milestone:

1. Update changed API contracts and active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Milestone 1 review scope is `Scene` terrain ownership, `Terrain` and `TerrainChunk` API shape, chunk id stability, negative-coordinate chunk mapping, idempotent `Terrain::tick(...)`, sine-octave heightfield generation, `Terrain::m_material`, optional `Terrain::m_debug_plane_material`, `TerrainChunk::m_render_mesh`, optional `TerrainChunk::m_debug_plane_mesh` and `TerrainChunk::m_debug_plane_texture`, generation-time local-space mesh creation, `Terrain::extract_render_objects()`, removal of the terrain scene bridge, smoke-contract updates, resource churn after warmup, CMake integration, C++ tests, and screenshots.

Milestone 2 review scope is optional chunk-owned debug texture/plane preservation, debug render mode extraction, resource lifetime, renderer compatibility, bounded draw/object counts, and screenshots proving visible terrain mesh. It must explicitly verify that no terrain scene, renderer slot map, or visibility-time mesh generation was reintroduced.

Milestone 3 review scope is docs, coverage, smoke evidence, artifact paths, and removal of stale assumptions.

## Validation and Acceptance

Milestone 1 is accepted when C++ tests and visual evidence prove that:

- `Scene` owns a `Terrain` object and exposes it through `Scene::terrain()`;
- `Terrain` stores `TerrainChunk` objects in a map keyed by unique `(LOD, X, Y, Z)` ids;
- `Terrain::tick(...)` accepts a generation origin and creates the expected 5 by 5 LOD0 chunk region around it;
- `Terrain::tick(...)` uses floor-division world-to-chunk mapping, including negative coordinates and exact boundary positions;
- `Terrain::tick(...)` creates only X/Z radius-2 surface chunks with `lod = 0` and `chunk_y = 0`;
- `Terrain::tick(...)` does not affect `Terrain::sample(...)` values for the same world coordinate;
- moving the generation origin across a chunk boundary causes `Terrain::tick(...)` to unload chunks outside the wanted set and create the newly needed chunk ids without duplicating existing chunks;
- repeated same-origin ticks do not create duplicate chunks or recreate existing terrain resources;
- LOD0 chunks represent 32 by 32 by 32 one-meter cells;
- identical seed/config/chunk id values produce identical heightfield samples;
- different seeds change at least one sampled height in a representative chunk;
- adjacent surface chunks share equal edge heights at matching world coordinates;
- generated height values are finite;
- invalid config values, unsupported LODs, non-finite generation origins, and invalid chunk coordinate limits fail with clear `EngineError` messages;
- `Terrain` owns the current terrain material pointer as `Ptr<Material> m_material`;
- if debug plane rendering is retained, `Terrain` owns the shared debug plane material pointer as `Ptr<Material> m_debug_plane_material`;
- each generated `TerrainChunk` owns its render mesh as `Ptr<Mesh> m_render_mesh`;
- if debug plane rendering is retained, each generated `TerrainChunk` owns its debug plane mesh as `Ptr<Mesh> m_debug_plane_mesh` and its debug plane texture as `Ptr<Texture> m_debug_plane_texture`;
- each chunk's generation finishes by creating the render mesh when a terrain material is available;
- generated mesh vertex positions are local to the chunk, with the render object's model transform placing the chunk in world space;
- `Terrain::extract_render_objects()` returns ordinary render objects for existing renderable chunks and does not generate chunks, generate meshes, allocate terrain resources, or synchronize renderer slots;
- `Terrain::extract_render_objects()` can switch between clay mesh mode and debug plane mode without changing renderer behavior: clay mode uses `m_render_mesh` plus `m_material`, while debug plane mode uses `m_debug_plane_mesh` plus `m_debug_plane_material` and per-chunk property/material overrides that bind `m_debug_plane_texture`;
- the browser scene replaces the big quad with one render object per renderable terrain chunk;
- no `terrain_scene.cpp`, `TerrainScene`, renderer-side terrain chunk map, reusable mesh slot table, visibility-time mesh generation, or equivalent bridge remains in the active build or planned interface;
- optional heightfield texture CPU data uses a half-precision representation or a tested conversion path into a `R16Float` WebGPU texture if the debug plane texture path is retained;
- optional `R16Float` debug rendering uses no mips, no optional WebGPU features, non-filtering sampling or integer `textureLoad`, and clamp-to-edge addressing if a sampler is used;
- smoke contracts, pixel classifiers, and demo scene object/count expectations are updated for terrain replacing the checker ground;
- repeated rendered frames after terrain warmup do not increase terrain texture, mesh, shader, material, pipeline, or bind-group creation counts except for newly generated chunks;
- screenshots under `C:\dev\ofg\artifacts\terrain-debug` show visible per-chunk terrain meshes.

Milestone 2 is accepted when:

- each `TerrainChunk` can generate a heightfield mesh from the same height data used by the debug texture;
- generated vertices, normals, UVs, and indices are finite and valid;
- generated vertex positions are local to the chunk rather than large absolute world positions;
- tangents are either omitted by design or finite and valid if the clay material requires them;
- generated mesh counts match the fixed 32-cell grid resolution, initially 1089 vertices and 6144 32-bit indices per LOD0 chunk;
- mesh winding matches the existing renderer front-face convention;
- mesh UVs span `[0, 1]` over each chunk;
- adjacent chunks share matching edge heights in mesh form;
- adjacent chunk edge normals are consistent enough to avoid obvious seams, using `Terrain::sample(...)` outside the local chunk where needed;
- the browser scene shows a visible clay terrain mesh patch made from multiple addressable chunks;
- the clay terrain mesh uses the existing opaque shader/material path and receives current sun shadow sampling;
- the terrain is generated by C++ and submitted through the existing opaque renderer path;
- repeated ordinary frames do not recreate terrain mesh resources;
- any retained debug-plane code remains chunk-owned or terrain-owned, binds a per-chunk debug plane texture through render-object properties/overrides, and is not mediated by a terrain scene bridge;
- screenshots under `C:\dev\ofg\artifacts\terrain-debug` show the terrain mesh from at least one overview angle.

The final plan is accepted when:

- `npm run format:cpp` passes;
- `npm run format:cpp:check` passes;
- `npm run test:cpp` passes;
- `npm run test:ts` passes;
- `npm run build:wasm` passes;
- `npm run smoke:browser` passes with terrain visible;
- `npm run smoke:render` passes with terrain visible in the native render smoke output;
- `npm run smoke` passes, or any redundant subcommand is recorded here with rationale if skipped after both smoke paths have already passed;
- `npm run coverage` passes, or every exception is recorded here with rationale;
- changed implementation files do not appear in the default coverage attention output unless explicitly excepted;
- `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` describe scene-owned terrain, chunk ids, terrain-owned clay and debug materials, chunk-owned render/debug meshes, chunk-owned debug textures, local-space mesh vertices, direct render-object extraction, optional chunk-owned debug rendering, and the absence of a terrain scene bridge;
- this ExecPlan records the latest screenshot paths, milestone-review result, and remaining gaps.

## Idempotence and Recovery

Terrain generation should be deterministic for the same `TerrainConfig` and world coordinate regardless of generation origin, so tests and debug visualizations can be rerun without persistent setup. `Terrain::tick(...)` should be safe to call repeatedly with the same origin and should only generate missing chunk CPU data. Render mesh, debug plane mesh, and debug plane texture creation should happen during chunk generation, explicit terrain material/debug material assignment, or explicit terrain mutation, not as an accidental side effect of every render traversal. Restarting the runtime or dev server should rebuild the same chunk set from source.

If scene integration causes renderer smoke failures, keep the pure terrain generator and tests intact while temporarily disabling the terrain render-object append call behind a clearly named helper call. Do not revert unrelated bloom/render changes already present in the worktree. Do not reintroduce a terrain scene, mesh-slot map, or renderer-side terrain cache as a workaround.

If performance is poor, first reduce the fixed chunk radius, debug texture sample count, or mesh grid resolution. Do not introduce CDLOD, async streaming, or GPU compute in this plan as a rescue path.

## Artifacts and Notes

Expected durable artifacts:

- `C:\dev\ofg\artifacts\terrain-debug\terrain-overview.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-clay-mesh.png`
- `C:\dev\ofg\artifacts\terrain-debug\report.json`, if the smoke or screenshot helper records structured terrain debug evidence

Expected durable artifacts when debug plane mode is retained:

- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-browser.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-report.json`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-browser-report.json`

The initial fixed values can be revised during implementation, but a reasonable starting point is:

- `TerrainChunkId{lod = 0, chunk_x, chunk_y = 0, chunk_z}`;
- 32 by 32 by 32 cells per LOD0 chunk, with a conceptual dual of 33 by 33 by 33 vertices;
- debug plane textures use 33 by 33 X/Z samples unless implementation records a better debug-texture sampling choice;
- LOD0 world chunk size is 32 meters per axis because each LOD0 cell is one meter;
- initial terrain tick radius is 2 chunks around the origin, generating a 5 by 5 LOD0 surface region;
- one simple clay opaque material for generated heightfield meshes;
- one flat debug plane material/shader using the per-chunk half-float heightfield texture, if debug plane mode is retained;
- height scale tuned for readability rather than realism.

Initial footprint budget for the 5 by 5 LOD0 surface set:

| Item | Per chunk | 25 chunks |
| --- | ---: | ---: |
| Heightfield CPU samples, 33 by 33 `float` heights | 4,356 bytes | 108,900 bytes |
| `R16Float` debug texture texels, before upload padding | 2,178 bytes | 54,450 bytes |
| Mesh vertices, 1089 vertices at about 32 bytes each without tangents | about 34 KiB | about 851 KiB |
| Mesh indices, 6144 32-bit indices | 24 KiB | 600 KiB |
| Total expected terrain data before resource overhead | about 65 KiB | about 1.6 MiB |

If the implementation uses a larger vertex format or required tangents, update this table during Milestone 2. The first terrain render should submit 25 terrain chunks for the initial radius-2 region, plus the existing non-terrain demo objects that remain in the scene. Draw/object counts may differ from this only if the renderer batches chunks or the plan records a deliberate radius change.

## Interfaces and Dependencies

The C++ terrain interface should end with stable names close to:

    namespace ofg {
    // Fixed engine terrain dimensions, not TerrainConfig fields.
    // Names may change to match local constant style during implementation.
    inline constexpr std::int32_t k_terrain_chunk_lod0_cells_per_edge = 32;
    inline constexpr std::int32_t k_terrain_chunk_lod0_vertices_per_edge = 33;
    inline constexpr std::int32_t k_terrain_initial_surface_radius_chunks = 2;

    struct TerrainChunkId {
        std::int32_t m_lod;
        std::int32_t m_chunk_x;
        std::int32_t m_chunk_y;
        std::int32_t m_chunk_z;
    };

    struct TerrainConfig {
        std::uint64_t m_seed;
        float m_height_scale;
    };

    struct TerrainSample {
        float m_height;
    };

    struct TerrainTickContext {
        math::Vec3 m_generation_origin;
    };

    enum class TerrainRenderMode {
        ClayMesh,
        HeightDebugPlane,
    };

    class TerrainChunk {
    public:
        [[nodiscard]] TerrainChunkId id() const noexcept;
        [[nodiscard]] std::span<const TerrainSample> heightfield_samples() const noexcept;
        [[nodiscard]] Mesh* render_mesh() noexcept;
        [[nodiscard]] Mesh* debug_plane_mesh() noexcept;
        [[nodiscard]] Texture* debug_plane_texture() noexcept;
        void generate_heightfield(const Terrain& terrain);
        void generate_render_mesh(const Terrain& terrain);
        void generate_debug_plane(const Terrain& terrain);
        void generate(const Terrain& terrain);

    private:
        TerrainChunkId m_id;
        std::vector<TerrainSample> m_heightfield_samples;
        Ptr<Mesh> m_render_mesh;
        Ptr<Mesh> m_debug_plane_mesh;
        Ptr<Texture> m_debug_plane_texture;
    };

    class Terrain {
    public:
        [[nodiscard]] const TerrainConfig& config() const noexcept;
        void set_config(TerrainConfig config);
        void set_material(Material* material) noexcept;
        [[nodiscard]] Material* material() noexcept;
        [[nodiscard]] const Material* material() const noexcept;
        void set_debug_plane_material(Material* material) noexcept;
        [[nodiscard]] Material* debug_plane_material() noexcept;
        [[nodiscard]] const Material* debug_plane_material() const noexcept;
        void set_render_mode(TerrainRenderMode mode) noexcept;
        [[nodiscard]] TerrainRenderMode render_mode() const noexcept;
        void tick(const TerrainTickContext& context);
        [[nodiscard]] TerrainSample sample(float world_x, float world_z) const;
        [[nodiscard]] TerrainChunkId chunk_id_containing(float world_x, float world_z) const;
        [[nodiscard]] TerrainChunk* find_chunk(TerrainChunkId id) noexcept;
        [[nodiscard]] const TerrainChunk* find_chunk(TerrainChunkId id) const noexcept;
        [[nodiscard]] TerrainChunk* get_or_create_chunk(TerrainChunkId id);
        [[nodiscard]] const std::map<TerrainChunkId, TerrainChunk>& chunks() const noexcept;
        void extract_render_objects(std::vector<RenderObject>& output) const;

    private:
        TerrainConfig m_config;
        std::map<TerrainChunkId, TerrainChunk> m_chunks;
        Ptr<Material> m_material;
        Ptr<Material> m_debug_plane_material;
        TerrainRenderMode m_render_mode;
    };

    }

Exact names may change if implementation finds a better local pattern, and `Ptr<T>` should be replaced with the repository's actual resource handle type if different. The concepts should remain: scene-owned terrain, addressable 3D chunk id, fixed LOD0 chunk dimensions outside `TerrainConfig`, deterministic config owned by `Terrain`, origin-centered ticking through `Terrain`, sine-octave sampling through `Terrain`, floor-division chunk lookup, `get_or_create_chunk(...)` returning a pointer, terrain-owned clay and debug materials, chunk-owned render mesh, chunk-owned debug plane mesh, chunk-owned debug plane texture, local-space mesh vertices, and direct extraction of ordinary render objects. No `TerrainScene`, `TerrainChunkMesh`, renderer-side slot table, or other terrain-specific render bridge should be introduced.

`Terrain::extract_render_objects()` may use the repository's existing `RenderObject`, `PropertyBag`, and material override mechanisms, or a narrow extension of those mechanisms, to bind each chunk's debug plane texture while still submitting a normal render object. The important constraint is that the per-chunk texture remains owned by `TerrainChunk`, the shared debug material remains owned by `Terrain`, and the renderer does not need terrain-specific chunk knowledge.

No new npm dependency is planned. No new graphics engine dependency is planned. A vendored noise implementation may be considered only if the license is clear, the file is documented under `third_party` or local source, and the plan is updated before adoption.
