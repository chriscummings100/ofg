# Terrain Chunk Renderer v0

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

This plan starts the procedural terrain system by making terrain real in the renderer. The first user-visible result is a rendered scene where C++ owns a scene-level `Terrain` object, generates deterministic addressable `TerrainChunk` data, writes a half-precision heightfield debug texture per chunk, and renders one debug quad per chunk at `y == 0`. The next visible result is a generated heightfield mesh rendered with a simple clay material through the existing opaque shader path while preserving the debug-plane path for later editor switching.

The goal is not to solve final async or multi-LOD terrain streaming yet. The goal is to establish the first stable terrain contract: a `Scene` owns terrain like it owns `Environment`, terrain owns a streamed map of addressable chunks keyed by `(LOD, X, Y, Z)`, C++ can generate the same heightfield every time, adjacent surface chunks line up, and the result is visible in the game before we try to reason about richer generation.

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
- [x] (2026-07-04 20:20+01:00) Rendered one heightfield debug quad per generated terrain chunk, replaced the demo checker ground renderer, added `terrain_scene` resource realization, added the height-debug WGSL shader, updated demo-scene tests/counts, and broadened native/browser smoke surface classification for red/green terrain debug output.
- [x] (2026-07-04 20:45+01:00) Revised Milestone 1 streaming ownership: `Terrain::tick(...)` now reconciles the wanted 5 by 5 chunk window by unloading unwanted chunks and generating missing chunks, while the debug bridge reuses fixed draw slots for the current streamed chunks.
- [x] (2026-07-04 21:05+01:00) Fixed renderer resource churn exposed by streamed terrain browser smoke: compatible material bind-group layouts now share a cache key so terrain chunk textures do not force one pipeline per material, and the opaque pass starts with a bounded 256-draw uniform capacity instead of growing during camera-mode exercises.
- [ ] Generate heightfield meshes and render them with a clay material while retaining debug-plane code.
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

- Decision: Milestone 1 renders one flat debug quad per terrain chunk at `y == 0`, textured by a half-precision heightfield.
  Rationale: Rendering comes before deeper terrain reasoning. The debug plane makes chunk boundaries, addressing, and height generation visible before actual meshing exists.
  Date/Author: 2026-07-04 / User

- Decision: The Milestone 1 heightfield generator can be a few octaves of sine waves.
  Rationale: The first phase validates ownership, chunk addressing, texture generation, and rendering. Real noise, biome logic, and erosion would obscure those fundamentals.
  Date/Author: 2026-07-04 / User

- Decision: Milestone 2 adds generated heightfield meshes and renders them with a basic clay material, while keeping the debug-plane code.
  Rationale: The mesh is the useful terrain renderer path, but the debug texture plane remains valuable for later UI switching and generator inspection. The clay terrain mesh should use the existing opaque shader/material path, not a custom unlit shader, so current sun shadows are cast onto it.
  Date/Author: 2026-07-04 / User, amended by User note 2026-07-04

- Decision: Do not expose terrain debug/editor data through the browser facade in this plan.
  Rationale: Browser-facing terrain payloads would grow quickly and duplicate editor concerns. The terrain editor should be built through a later ImGui terrain editor plan, not through TypeScript DOM or WASM debug payload expansion.
  Date/Author: 2026-07-04 / User

- Decision: Keep terrain editor work separate from this renderer plan and build it on top of the already integrated ImGui path.
  Rationale: Terrain controls, visualization, and editor workflow should live in an in-engine editor surface rather than in TypeScript DOM or ad hoc WASM debug payloads, but the first terrain milestone should focus on generated chunk data and visible rendering.
  Date/Author: 2026-07-04 / User

- Decision: Terrain generation behavior should be object-owned, not free global functions.
  Rationale: `Terrain` owns its config and can expose sampling as a member function. `TerrainChunk` owns its generated heightfield data and stable references to its generated debug texture, debug plane, and mesh resources, so mesh generation should update chunk state rather than return a separate `TerrainChunkMesh` data object.
  Date/Author: 2026-07-04 / User

- Decision: `Terrain` needs an update/tick function that accepts a generation origin.
  Rationale: Terrain should initially generate a small region, such as 5 by 5 chunks, around the player or another supplied origin. This establishes the future streaming shape without adding asynchronous queues or multiple LODs yet.
  Date/Author: 2026-07-04 / User

- Decision: `Terrain::tick(...)` owns the first streaming reconcile loop.
  Rationale: Scene should only tick terrain around an origin and should not reason about chunk membership. Each tick compares the current map with the wanted chunk set, drops chunks outside the wanted set, and creates and generates chunks that are missing.
  Date/Author: 2026-07-04 / User, implemented by Codex

- Decision: The Milestone 1 debug render bridge reuses fixed draw slots for the current chunk set.
  Rationale: The current scene graph has no entity deletion API, so repeatedly creating scene entities for streamed chunks would grow stale renderers. Reusing draw slots keeps the visible debug terrain tracking the authoritative `Terrain` chunk map without moving chunk lifecycle decisions into `Scene`.
  Date/Author: 2026-07-04 / Codex

- Decision: Share compatible material bind-group layouts by structural layout key.
  Rationale: Terrain debug rendering uses one texture/material per visible chunk. If each material owns a distinct bind-group layout, the opaque pipeline cache treats structurally identical terrain materials as separate pipeline layouts and creates pipelines lazily as chunks become visible. Sharing compatible layouts keeps pipeline creation bounded by shader/material schema instead of chunk count.
  Date/Author: 2026-07-04 / Codex

- Decision: Start the opaque pass with a 256-draw uniform capacity.
  Rationale: The default scene now includes 25 streamed terrain debug draws plus the validation object field. A tiny one-draw initial capacity caused ordinary camera exercises to grow durable draw-uniform buffers after warmup. A 256-draw buffer is small and covers the current default terrain/debug scene without steady-state buffer churn.
  Date/Author: 2026-07-04 / Codex

- Decision: Debug planes with no art/albedo texture and clay-material heightfield meshes are acceptable for this plan.
  Rationale: The first important proof is chunk identity, generated height data, seams, scale, camera readability, and render integration. The Milestone 1 debug planes still deliberately use a heightfield texture; materials, biome masks, triplanar shading, and erosion-driven textures can follow after terrain is visible.
  Date/Author: 2026-07-04 / Codex

- Decision: Milestone 2 remains part of this plan.
  Rationale: The first useful terrain renderer is the generated mesh path, so Milestone 1 should prove chunk visibility and Milestone 2 should convert the same data into clay-rendered terrain. It is not a stretch goal to remove from scope.
  Date/Author: 2026-07-04 / User

## Outcomes & Retrospective

Not started. At completion, summarize what terrain chunks render, what validation passed, and which terrain/editor decisions remain open.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`: TypeScript may continue to host the browser canvas, raw input, runtime loading, status text, and smoke helpers, but it must not own terrain simulation, chunk generation, mesh data, terrain editor state, scene graph state, renderer internals, GPU handles, or gameplay world data. This plan does not add TypeScript terrain editor controls.

This plan extends `OFG-BOOT-002 C++ Runtime Ownership`: C++ owns the scene-level `Terrain`, terrain configuration, terrain sampling behavior, the terrain chunk id, the `TerrainChunk` map, deterministic heightfield generation, heightfield debug texture data, chunk mesh construction, terrain ticking around a supplied generation origin, and terrain scene integration. `TerrainChunk` stores generated terrain data plus stable references to any generated `Resources`-owned texture and mesh resources, using the existing resource handle/pointer pattern internally and exposing raw pointer convenience accessors only where that matches local code. `Game` should only orchestrate scene setup and status plumbing; terrain-specific behavior belongs in `Scene`, `Terrain`, `TerrainChunk`, renderer terrain helpers, or terrain scene setup code.

This plan preserves `OFG-BOOT-003 WASM Facade`: no terrain debug or editor API should be added to the browser facade in this plan. The facade must not expose raw terrain object pointers, mutable scene ownership, mesh pointers, GPU handles, renderer internals, or bulk terrain data.

This plan preserves `OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline`: browser and native rendering should remain on WebGPU-backed renderer paths, request no optional WebGPU features, and keep smoke-compatible sky, camera, player fallback/model, and opaque render behavior. Replacing the checker ground with terrain debug planes is a visible scene-contract change, so `tools/smoke-contract.json`, browser smoke pixel classification, native render smoke expectations, and any demo-scene object/count tests must be updated deliberately in the same milestone that changes the scene. Terrain may add a heightfield-debug shader path and later a clay material path, but screenshots and smoke thresholds must be updated deliberately if pixel classification changes. Half-precision heightfield textures must be implemented using WebGPU core features only; the initial target format is `R16Float` with no mips, non-filtering sampling or integer `textureLoad`, clamp-to-edge addressing if a sampler is used, and no optional float-filtering feature request.

This plan preserves `OFG-BOOT-006 Resource Lifetime`: terrain textures, shared debug-plane mesh resources, terrain mesh resources, shaders, and materials may be created through `Resources` during scene preparation or explicit terrain mutation, not recreated during every ordinary steady-state frame. `Terrain::tick(...)` should be idempotent for an unchanged origin chunk and complete chunk set. Repeated same-origin ticks and several rendered frames after warmup should not increase terrain `Texture`, `Mesh`, `Shader`, `Material`, pipeline, or bind-group creation counters except for deliberately newly generated chunks.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Modified implementation files must meet the coverage attention gate unless an explicit exception is recorded here with rationale.

Repository readability rules apply from `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md`: files need purpose comments, functions need comments or docstrings, larger functions over 50 lines need internal explanation, C++ uses the repo clang-format config, and files above 500 lines should draw scrutiny.

## Context and Orientation

The current app is a C++/WASM runtime with a TypeScript browser host. TypeScript in `C:\dev\ofg\src\app` creates the canvas, loads the generated WASM module, forwards raw input, polls debug status, and displays a small status overlay. C++ in `C:\dev\ofg\cpp` owns the scene graph, resources, renderer, player/camera behavior, and WebGPU runtime behavior.

The current browser scene is built by `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It creates generated materials and meshes, a scene-owned 5 by 5 terrain chunk debug surface, a fallback player box or loaded player model, several colored cubes, a camera, and a sun light. `Game::prepare()` in `C:\dev\ofg\cpp\src\game\game.cpp` builds that scene, and `Renderer::render()` draws mesh renderers from the current `Scene`.

A terrain chunk is a 32 by 32 by 32 cell world volume at LOD0, addressed by integer `(LOD, X, Y, Z)` coordinates. Its conceptual dual grid has 33 by 33 by 33 vertices, meaning there is one more sample point than cell count along each axis so neighboring chunks can share their boundary samples. These dimensions are fixed terrain constants, not `TerrainConfig` fields. Milestone 1 only generates and renders a heightfield surface over the chunk's X/Z footprint, but the key and dimensions should be 3D from the beginning. World space follows OFG's left-handed convention from `OFG-BOOT-002`: `+X` is right, `+Y` is up, and `+Z` is forward. Terrain height maps to world `Y`. The first rendered debug planes sit at `y == 0` and visualize height through texture color.

Chunk coordinates use floor division, not truncation toward zero: a world `x` in `[0, 32)` maps to chunk `0`, a world `x` in `[32, 64)` maps to chunk `1`, and a world `x` in `[-32, 0)` maps to chunk `-1`. The same rule applies to `z`. The initial surface set always uses `lod = 0` and `chunk_y = 0`. The `Terrain::tick(...)` origin selects which chunks should exist; it must not affect `Terrain::sample(...)` results for a given world coordinate.

The research direction in `C:\dev\ofg\docs\research\terrain-research-implementation.md` recommends a heightfield-first architecture, procedural field stack, climate/hydrology later, and sparse volumetric terrain only after the surface pipeline works. This plan implements the first visible foundation only: deterministic heightfield chunks, debug-plane rendering, and generated heightfield mesh rendering. Editor UI is deliberately deferred to a later terrain editor plan built on top of the already integrated ImGui path.

Out of scope for this plan: CDLOD, geometry clipmaps, multiple LOD rings, asynchronous streaming queues, browser terrain debug APIs, TypeScript terrain editor UI, ImGui terrain editor UI, biome classification, hydrology, erosion, caves, volumetric meshing, digging, collision, vegetation placement, triplanar terrain materials, save/load deltas, networking, and final editor authoring workflows.

## Plan of Work

Milestone 1 creates the scene-owned terrain model and renders heightfield debug textures. Add a `Terrain` object owned by `Scene`, similar in ownership level to `Environment`. `Terrain` owns a map of `TerrainChunk` objects keyed by `TerrainChunkId{lod, x, y, z}`. `Terrain::tick(...)` accepts a generation origin, initially the player position or a fixed origin when no player is available, and reconciles a 5 by 5 LOD0 surface region around the origin chunk. That means X/Z radius 2, `lod = 0`, and `chunk_y = 0`, not a 5 by 5 by 5 volume. `Terrain::tick(...)` must be idempotent for the same origin chunk, unload chunks outside the wanted set, and create and generate chunks missing from the wanted set. The first integration should call terrain ticking during scene preparation so the initial scene renders immediately, and then from `Scene::update(...)` after player movement and before render extraction so future streaming has an obvious home. LOD0 chunks are fixed at 32 by 32 by 32 cells, with one cell equal to one meter. Generate a deterministic heightfield for each surface chunk using a few octaves of sine waves. A terrain render-resource helper should realize the corresponding `Resources`-owned debug textures and retarget a fixed set of debug draw slots to the current chunk set when render resources are available. Store the heightfield in a half-precision texture. Render one quad per terrain chunk at `y == 0`, replacing the current big checker quad, and use a terrain debug shader that colors negative height red and positive height green. This milestone proves chunk ownership, addressing, origin-centered ticking, heightfield generation, half-float texture upload, first streamed chunk reconciliation, and visible per-chunk rendering.

Milestone 2 adds heightfield meshing. Each `TerrainChunk` generates a mesh from the same heightfield data, with vertices raised to the generated height and normals derived from neighboring world samples through `Terrain::sample(...)`. The target topology is 33 by 33 vertices, 32 by 32 quads, 2048 triangles, and 6144 32-bit indices per chunk unless implementation records a deliberate alternative. Vertices should cover the chunk X/Z footprint, UVs should span `[0, 1]`, winding should match the existing renderer front-face convention, and tangents should be generated only if the selected clay material path requires them. Render the heightfield meshes with a simple clay material through the existing opaque shader path instead of the debug planes, so terrain receives current sun shadow sampling like other opaque geometry. Do not delete the debug-plane code, shader, or texture data path; later ImGui UI work will switch between clay mesh view and heightfield debug view.

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

   Start with the smallest set of files that keeps ownership readable. Add `terrain_heightfield.hpp/.cpp` only if heightfield conversion grows beyond chunk-local logic, and add `terrain_scene.hpp/.cpp` when scene-resource wiring would otherwise bloat `demo_scene.cpp` or `game.cpp`.

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

6. Add the terrain debug shader and flat chunk plane render path:

       C:\dev\ofg\cpp\src\render\shaders\terrain_height_debug.wgsl.hpp
       C:\dev\ofg\cpp\src\terrain\terrain_scene.cpp
       C:\dev\ofg\cpp\src\render\demo_scene.cpp, only for replacing the existing big quad setup with terrain scene setup

   Keep `Game::prepare_impl()` small by calling helpers rather than embedding chunk-generation details. Prefer one shared debug-plane mesh reused by all terrain chunks, with per-chunk transform and heightfield texture state, unless the existing scene/material model makes a per-chunk mesh materially simpler.

7. In Milestone 2, add heightfield meshing and clay rendering:

       C:\dev\ofg\cpp\src\terrain\terrain_chunk.cpp
       C:\dev\ofg\cpp\src\terrain\terrain_scene.cpp
       C:\dev\ofg\cpp\tests\terrain_chunk_test.cpp

   Preserve the Milestone 1 debug-plane code path for later ImGui switching. Mesh generation should produce 1089 vertices and 6144 indices per LOD0 chunk for the default 32-cell grid, use finite positions/normals/UVs, derive edge normals from world samples so adjacent chunks agree, and feed the existing opaque material/shader path with a basic clay material so shadows cast onto the terrain mesh.

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

   Report the printed local URL. Capture screenshots regularly for browser/render work. Store durable terrain screenshots under `C:\dev\ofg\artifacts\terrain-debug` and present them in chat. Required screenshot moments are the first visible debug planes, the first visible clay mesh, and the final browser-smoke-equivalent terrain view.

## Milestone Review

After each implementation milestone:

1. Update changed API contracts and active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Milestone 1 review scope is `Scene` terrain ownership, `Terrain` and `TerrainChunk` API shape, chunk id stability, negative-coordinate chunk mapping, idempotent `Terrain::tick(...)`, sine-octave heightfield generation, half-float texture handling, debug shader integration, flat per-chunk debug rendering, smoke-contract updates, resource churn after warmup, CMake integration, C++ tests, and screenshots.

Milestone 2 review scope is heightfield mesh generation, topology counts, edge-normal behavior, clay material rendering, preservation of debug-plane code, resource lifetime, renderer compatibility, bounded draw/object counts, and screenshots proving visible terrain mesh.

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
- heightfield texture CPU data uses a half-precision representation or a tested conversion path into a `R16Float` WebGPU texture;
- the `R16Float` debug path uses no mips, no optional WebGPU features, non-filtering sampling or integer `textureLoad`, and clamp-to-edge addressing if a sampler is used;
- zero height renders black, negative height renders red, and positive height renders green through a documented normalization/clamping rule;
- the browser scene replaces the big quad with one `y == 0` debug quad per terrain chunk;
- the heightfield debug shader colors negative heights red and positive heights green;
- smoke contracts, pixel classifiers, and demo scene object/count expectations are updated for terrain replacing the checker ground;
- repeated rendered frames after terrain warmup do not increase terrain texture, mesh, shader, material, pipeline, or bind-group creation counts except for newly generated chunks;
- screenshots under `C:\dev\ofg\artifacts\terrain-debug` show the per-chunk debug planes.

Milestone 2 is accepted when:

- each `TerrainChunk` can generate a heightfield mesh from the same height data used by the debug texture;
- generated vertices, normals, UVs, and indices are finite and valid;
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
- Milestone 1 debug-plane code remains available for later UI switching;
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
- `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` describe scene-owned terrain, chunk ids, debug rendering, and meshing;
- this ExecPlan records the latest screenshot paths, milestone-review result, and remaining gaps.

## Idempotence and Recovery

Terrain generation should be deterministic for the same `TerrainConfig` and world coordinate regardless of generation origin, so tests and debug visualizations can be rerun without persistent setup. `Terrain::tick(...)` should be safe to call repeatedly with the same origin and should only generate missing chunk CPU data. The initial terrain scene integration should create or realize resources during explicit terrain scene preparation or terrain resource synchronization, not as an accidental side effect of every render traversal. Restarting the runtime or dev server should rebuild the same chunk set from source.

If scene integration causes renderer smoke failures, keep the pure terrain generator and tests intact while temporarily disabling terrain scene setup behind a clearly named helper call. Do not revert unrelated bloom/render changes already present in the worktree.

If performance is poor, first reduce the fixed chunk radius, debug texture sample count, or mesh grid resolution. Do not introduce CDLOD, async streaming, or GPU compute in this plan as a rescue path.

## Artifacts and Notes

Expected durable artifacts:

- `C:\dev\ofg\artifacts\terrain-debug\terrain-overview.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-browser.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-report.json`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-height-debug-planes-browser-report.json`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-clay-mesh.png`
- `C:\dev\ofg\artifacts\terrain-debug\report.json`, if the smoke or screenshot helper records structured terrain debug evidence

The initial fixed values can be revised during implementation, but a reasonable starting point is:

- `TerrainChunkId{lod = 0, chunk_x, chunk_y = 0, chunk_z}`;
- 32 by 32 by 32 cells per LOD0 chunk, with a conceptual dual of 33 by 33 by 33 vertices;
- Milestone 1 heightfield texture uses 33 by 33 X/Z samples unless implementation records a better debug-texture sampling choice;
- LOD0 world chunk size is 32 meters per axis because each LOD0 cell is one meter;
- initial terrain tick radius is 2 chunks around the origin, generating a 5 by 5 LOD0 surface region;
- one flat debug quad material/shader using the half-float heightfield texture;
- one simple clay opaque material for Milestone 2 heightfield meshes;
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

    class TerrainChunk {
    public:
        [[nodiscard]] TerrainChunkId id() const noexcept;
        [[nodiscard]] std::span<const TerrainSample> heightfield_samples() const noexcept;
        [[nodiscard]] Texture* heightfield_debug_texture() noexcept;
        [[nodiscard]] Mesh* debug_plane_mesh() noexcept;
        [[nodiscard]] Mesh* heightfield_mesh() noexcept;
        void generate_heightfield(const Terrain& terrain);
        void realize_debug_texture();
        void realize_debug_plane_mesh();
        void generate_heightfield_mesh(const Terrain& terrain);

    private:
        TerrainChunkId m_id;
        std::vector<TerrainSample> m_heightfield_samples;
        Ptr<Texture> m_heightfield_debug_texture;
        Ptr<Mesh> m_debug_plane_mesh;
        Ptr<Mesh> m_heightfield_mesh;
    };

    class Terrain {
    public:
        [[nodiscard]] const TerrainConfig& config() const noexcept;
        void set_config(TerrainConfig config);
        void tick(const TerrainTickContext& context);
        [[nodiscard]] TerrainSample sample(float world_x, float world_z) const;
        [[nodiscard]] TerrainChunkId chunk_id_containing(float world_x, float world_z) const;
        [[nodiscard]] TerrainChunk* find_chunk(TerrainChunkId id) noexcept;
        [[nodiscard]] const TerrainChunk* find_chunk(TerrainChunkId id) const noexcept;
        [[nodiscard]] TerrainChunk* get_or_create_chunk(TerrainChunkId id);
    };

    }

Exact names may change if implementation finds a better local pattern, and `Ptr<T>` should be replaced with the repository's actual resource handle type if different. The concepts should remain: scene-owned terrain, addressable 3D chunk id, fixed LOD0 chunk dimensions outside `TerrainConfig`, deterministic config owned by `Terrain`, origin-centered ticking through `Terrain`, sine-octave sampling through `Terrain`, floor-division chunk lookup, `get_or_create_chunk(...)` returning a pointer, half-float debug texture output referenced by `TerrainChunk`, and mesh output stored as chunk state in Milestone 2.

No new npm dependency is planned. No new graphics engine dependency is planned. A vendored noise implementation may be considered only if the license is clear, the file is documented under `third_party` or local source, and the plan is updated before adoption.
