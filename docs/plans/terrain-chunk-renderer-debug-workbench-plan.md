# Terrain Chunk Renderer and Debug Workbench v0

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

This plan starts the procedural terrain system by making terrain real in the renderer. The first user-visible result is a browser scene where C++ generates deterministic, addressable heightfield mesh chunks and renders a small single-LOD patch set that a developer can fly around. The companion browser debug workbench is the beginning of a terrain editor: it exposes seed and chunk parameters, requests C++-owned debug data, and visualizes terrain fields without owning world simulation or renderer internals.

The goal is not to solve final terrain streaming yet. The goal is to establish the first stable terrain contract: an external caller can name a chunk by address, C++ can generate the same mesh every time, adjacent chunks line up, and the result is visible in the game.

## Progress

- [x] (2026-07-04 10:25+01:00) Reviewed `docs/research/terrain-research-overview.md`, `docs/research/terrain-research-implementation.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, and `docs/SYSTEMS.md`.
- [x] (2026-07-04 10:25+01:00) Captured the user direction that CDLOD is out of scope for this first step, while addressable single-LOD mesh chunks and visible rendering are in scope.
- [ ] Create the C++ terrain generation module and doctests.
- [ ] Render a fixed set of generated terrain chunks in the current scene.
- [ ] Add the first browser terrain debug workbench controls and visualization.
- [ ] Update system and API contract docs.
- [ ] Run validation, capture browser screenshots, run milestone review, and record outcomes.

## Surprises & Discoveries

- Observation: The repository already records that future terrain generation belongs behind an owning C++ subsystem rather than in `Game`.
  Evidence: `C:\dev\ofg\docs\SYSTEMS.md` says `Game` is orchestration glue and names terrain generation as feature-specific behavior that should not accumulate in `game.cpp`.

- Observation: The current worktree contains unrelated bloom/render changes.
  Evidence: `git status --short` on 2026-07-04 showed modified render, bloom, docs, and test files before this plan was created. This plan should avoid touching those files unless terrain integration truly requires it.

## Decision Log

- Decision: Do not implement CDLOD in this plan.
  Rationale: The user has a more precise future streaming model in mind, and CDLOD is unnecessary for proving terrain generation, chunk addressing, mesh output, and visual integration.
  Date/Author: 2026-07-04 / Codex

- Decision: The first terrain render path uses addressable chunks with exactly one LOD.
  Rationale: Chunk keys make future streaming explicit while keeping the first renderer milestone small enough to test and inspect.
  Date/Author: 2026-07-04 / Codex

- Decision: Terrain generation and terrain mesh construction are C++ owned; TypeScript owns only debug controls and visualization.
  Rationale: This preserves `OFG-BOOT-001`, `OFG-BOOT-002`, and `OFG-BOOT-003`, where TypeScript hosts the browser shell and C++ owns game-world data.
  Date/Author: 2026-07-04 / Codex

- Decision: Untextured or flat-material terrain is acceptable for this plan.
  Rationale: The first important proof is geometry, seams, scale, camera readability, and render integration. Materials, biome masks, triplanar shading, and erosion-driven textures can follow after terrain is visible.
  Date/Author: 2026-07-04 / Codex

## Outcomes & Retrospective

Not started. At completion, summarize what terrain chunks render, what the workbench can inspect, what validation passed, and which terrain decisions remain open.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`: TypeScript may add DOM controls, a heatmap canvas, debug readouts, and smoke helpers, but it must not own terrain simulation, chunk generation, mesh data, scene graph state, renderer internals, GPU handles, or gameplay world data.

This plan extends `OFG-BOOT-002 C++ Runtime Ownership`: C++ owns the terrain chunk key, terrain sampling configuration, deterministic heightfield generation, chunk mesh construction, terrain scene integration, and the terrain debug payload exposed to the browser facade. `Game` should only orchestrate terrain scene setup and status plumbing; terrain-specific behavior belongs in a terrain subsystem or terrain scene helper.

This plan changes `OFG-BOOT-003 WASM Facade` narrowly: the browser facade may expose a read-only terrain debug sample method that returns C++-generated terrain data in a compact JSON shape. The facade must not expose raw terrain object pointers, mutable scene ownership, mesh pointers, GPU handles, or renderer internals.

This plan preserves `OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline`: browser and native rendering should remain on the current draw-list renderer path, request no optional WebGPU features, and keep smoke-compatible sky, camera, player fallback/model, and opaque render behavior. Terrain may change the visible scene contract, but screenshots and smoke thresholds must be updated deliberately if pixel classification changes.

This plan preserves `OFG-BOOT-006 Resource Lifetime`: terrain mesh resources may be created during scene preparation or explicit terrain debug mutation, not recreated during every ordinary steady-state frame. Repeated rendering should reuse existing `Mesh` and `Material` resources.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Modified implementation files must meet the coverage attention gate unless an explicit exception is recorded here with rationale.

Repository readability rules apply from `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md`: files need purpose comments, functions need comments or docstrings, larger functions over 50 lines need internal explanation, C++ uses the repo clang-format config, and files above 500 lines should draw scrutiny.

## Context and Orientation

The current app is a C++/WASM runtime with a TypeScript browser host. TypeScript in `C:\dev\ofg\src\app` creates the canvas, loads the generated WASM module, forwards raw input, polls debug status, and displays a small status overlay. C++ in `C:\dev\ofg\cpp` owns the scene graph, resources, renderer, player/camera behavior, and WebGPU runtime behavior.

The current browser scene is built by `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It creates generated materials and meshes, a checker ground plane, a fallback player box or loaded player model, several colored cubes, a camera, and a sun light. `Game::prepare()` in `C:\dev\ofg\cpp\src\game\game.cpp` builds that scene, and `Renderer::render()` draws mesh renderers from the current `Scene`.

A terrain chunk is one rectangular heightfield mesh tile addressed by integer chunk coordinates. For this plan, a chunk key has one supported level of detail, `lod = 0`, plus `chunk_x` and `chunk_z`. World space follows OFG's left-handed convention from `OFG-BOOT-002`: `+X` is right, `+Y` is up, and `+Z` is forward. Terrain height maps to the `Y` axis, and chunk grids lie in the `X/Z` plane.

The research direction in `C:\dev\ofg\docs\research\terrain-research-implementation.md` recommends a heightfield-first architecture, procedural field stack, climate/hydrology later, and sparse volumetric terrain only after the surface pipeline works. This plan implements the first visible foundation only: deterministic heightfield chunks and the start of a terrain workbench.

Out of scope for this plan: CDLOD, geometry clipmaps, multiple LOD rings, asynchronous streaming queues, biome classification, hydrology, erosion, caves, volumetric chunks, digging, collision, vegetation placement, triplanar terrain materials, save/load deltas, networking, and final editor authoring workflows.

## Plan of Work

Milestone 1 creates the terrain generation core. Add `C:\dev\ofg\cpp\include\ofg\terrain` and `C:\dev\ofg\cpp\src\terrain` with a small public API: `TerrainChunkKey`, `TerrainConfig`, `TerrainSample`, `TerrainChunkMesh`, and generation functions. The generator samples deterministic world-space height values from seed and coordinates, builds a fixed grid such as 32 cells by 32 cells, outputs `MeshVertex` data and indices, and computes normals. Tests prove deterministic output, finite bounded values, vertex/index counts, stable world coordinate mapping, and exact shared-edge height agreement between neighboring chunks.

Milestone 2 renders terrain chunks in the scene. Add a terrain scene helper rather than placing terrain logic directly in `game.cpp`. During scene preparation, create one flat terrain material and a fixed patch of chunks, likely 3x3 or 5x5 around the origin. Each generated chunk becomes a `Mesh` resource and an entity with a `MeshRenderer`. The checker ground can be removed, shrunk, or left out of the way so the terrain surface is easy to see. The camera placement and far clip may need adjustment so the terrain patch is visible and flyable.

Milestone 3 exposes a read-only terrain debug endpoint through the existing WASM facade. Add a `BrowserGame` method that returns JSON for a terrain debug grid or a named generated chunk. The payload should include seed, chunk key or origin, sample dimensions, field names, min/max/mean statistics, and a compact array of sample values. `src/app/wasmRuntime.ts` should validate and wrap this method with TypeScript types and tests using a fake raw runtime. The endpoint is diagnostic, not a mutable editor API.

Milestone 4 adds the browser terrain debug workbench. Add a small fixed overlay or side panel with controls for seed, chunk origin, visible radius, sample spacing or chunk size display, and field selection. Render a heatmap or simple 2D canvas visualization of the C++ debug payload. Keep the UI dense and tool-like. It should not be a marketing page or a card-heavy landing view. It should not own terrain generation logic.

Milestone 5 updates docs and validates. Add a `TerrainGeneration` section to `C:\dev\ofg\docs\SYSTEMS.md`, update `C:\dev\ofg\docs\API_CONTRACTS.md` for the terrain debug facade extension, and record screenshots under `C:\dev\ofg\artifacts\terrain-debug`. Run tests, coverage, smoke, and the required milestone review before marking implementation complete.

## Concrete Steps

Run commands from `C:\dev\ofg` unless a command says otherwise.

1. Inspect current interfaces before editing:

       rg -n "build_demo_scene|setup_demo_scene|debug_status_json|BrowserGame|RawBrowserGame|Mesh::init" cpp src tests

2. Add terrain headers and implementation:

       C:\dev\ofg\cpp\include\ofg\terrain\terrain_chunk.hpp
       C:\dev\ofg\cpp\src\terrain\terrain_chunk.cpp

   Update `C:\dev\ofg\cpp\CMakeLists.txt` to compile the new source and new test.

3. Add native tests:

       C:\dev\ofg\cpp\tests\terrain_chunk_test.cpp

   The tests should cover deterministic generation, neighbor edge agreement, invalid configuration errors, normal generation, and mesh counts.

4. Integrate terrain with the scene through a helper:

       C:\dev\ofg\cpp\include\ofg\terrain\terrain_scene.hpp
       C:\dev\ofg\cpp\src\terrain\terrain_scene.cpp

   Keep `Game::prepare_impl()` small by calling the helper rather than embedding chunk-generation details.

5. Add the browser debug facade:

       C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
       C:\dev\ofg\cpp\src\web\browser_game.cpp
       C:\dev\ofg\cpp\src\web\embind_module.cpp
       C:\dev\ofg\src\app\wasmRuntime.ts
       C:\dev\ofg\tests\ts\wasmRuntime.test.ts

6. Add the browser workbench:

       C:\dev\ofg\src\app\terrainDebugPanel.ts
       C:\dev\ofg\tests\ts\terrainDebugPanel.test.ts
       C:\dev\ofg\src\app\main.ts
       C:\dev\ofg\src\app\styles.css

7. Update docs:

       C:\dev\ofg\docs\API_CONTRACTS.md
       C:\dev\ofg\docs\SYSTEMS.md
       C:\dev\ofg\docs\coverage\latest.md, only if coverage command updates committed summaries

8. Format and validate:

       npm run format:cpp
       npm run format:cpp:check
       npm run test:cpp
       npm run test:ts
       npm run build:wasm
       npm run smoke:browser
       npm run coverage

9. For visual validation during and before finalization:

       npm run dev

   Report the printed local URL. Capture screenshots regularly for browser/render/UI work. Store durable terrain screenshots under `C:\dev\ofg\artifacts\terrain-debug` and present them in chat.

## Milestone Review

After each implementation milestone:

1. Update changed API contracts and active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Milestone 1 review scope is the terrain generator API, deterministic chunk mesh output, CMake integration, and C++ tests.

Milestone 2 review scope is scene integration, resource lifetime, renderer compatibility, and screenshots proving visible terrain.

Milestone 3 review scope is the WASM facade shape, TypeScript parser/wrapper tests, and ownership boundaries.

Milestone 4 review scope is UI ergonomics, debug visualization correctness, screenshot evidence, and TypeScript coverage.

Milestone 5 review scope is docs, coverage, smoke evidence, artifact paths, and removal of stale assumptions.

## Validation and Acceptance

The terrain generation core is accepted when C++ tests prove that:

- identical seed/config/key values produce identical vertices and indices;
- different seeds change at least one sampled height in a representative chunk;
- adjacent chunks share equal edge heights at matching world coordinates;
- generated vertices, normals, tangents, UVs, and indices are finite and valid;
- invalid config values fail with clear `EngineError` messages;
- generated mesh counts match the chosen fixed grid resolution.

The rendered terrain milestone is accepted when:

- the browser scene shows a visible terrain patch made from multiple addressable chunks;
- the terrain is generated by C++ and submitted through the existing scene mesh-renderer path;
- repeated ordinary frames do not recreate terrain mesh resources;
- screenshots under `C:\dev\ofg\artifacts\terrain-debug` show the terrain from at least one overview angle.

The workbench milestone is accepted when:

- TypeScript can request a terrain debug payload from C++ through the wrapped runtime;
- seed/chunk controls update the visualization;
- a heatmap or equivalent 2D visualization shows the selected C++ field;
- tests validate parser behavior, error cases, and UI update logic without relying on generated WASM internals.

The final plan is accepted when:

- `npm run format:cpp` passes;
- `npm run format:cpp:check` passes;
- `npm run test:cpp` passes;
- `npm run test:ts` passes;
- `npm run build:wasm` passes;
- `npm run smoke:browser` passes with terrain visible;
- `npm run coverage` passes, or every exception is recorded here with rationale;
- changed implementation files do not appear in the default coverage attention output unless explicitly excepted;
- `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` describe the new terrain ownership and debug facade;
- this ExecPlan records the latest screenshot paths, milestone-review result, and remaining gaps.

## Idempotence and Recovery

Terrain generation should be deterministic and stateless for the same `TerrainConfig` and `TerrainChunkKey`, so tests and debug visualizations can be rerun without persistent setup. The initial terrain scene integration should create resources during scene preparation; restarting the runtime or dev server should rebuild the same chunk set from source.

If scene integration causes renderer smoke failures, keep the pure terrain generator and tests intact while temporarily disabling terrain scene setup behind a clearly named helper call. Do not revert unrelated bloom/render changes already present in the worktree. If TypeScript debug UI causes test or smoke instability, keep the C++ terrain renderer path working and isolate the UI behind a small module that can be repaired independently.

If performance is poor, first reduce the fixed chunk radius or grid resolution. Do not introduce CDLOD, async streaming, or GPU compute in this plan as a rescue path.

## Artifacts and Notes

Expected durable artifacts:

- `C:\dev\ofg\artifacts\terrain-debug\terrain-overview.png`
- `C:\dev\ofg\artifacts\terrain-debug\terrain-workbench.png`
- `C:\dev\ofg\artifacts\terrain-debug\report.json`, if the smoke or screenshot helper records structured terrain debug evidence

The initial fixed values can be revised during implementation, but a reasonable starting point is:

- `TerrainChunkKey{lod = 0, chunk_x, chunk_z}`;
- 32 cells per chunk, producing 33 by 33 vertices;
- world chunk size around 32 to 64 units;
- terrain patch radius of 1 or 2 chunks around origin;
- one flat terrain material using the existing opaque shader path;
- height scale tuned for readability rather than realism.

## Interfaces and Dependencies

The C++ terrain interface should end with stable names close to:

    namespace ofg {
    struct TerrainChunkKey {
        std::int32_t m_lod;
        std::int32_t m_chunk_x;
        std::int32_t m_chunk_z;
    };

    struct TerrainConfig {
        std::uint64_t m_seed;
        std::uint32_t m_cells_per_edge;
        float m_chunk_world_size;
        float m_height_scale;
    };

    struct TerrainSample {
        float m_base_elevation;
        float m_mountainness;
        float m_height;
    };

    struct TerrainChunkMesh {
        TerrainChunkKey m_key;
        std::vector<MeshVertex> m_vertices;
        std::vector<std::uint32_t> m_indices;
    };

    TerrainSample sample_terrain(const TerrainConfig& config, float world_x, float world_z);
    TerrainChunkMesh generate_terrain_chunk_mesh(const TerrainConfig& config, TerrainChunkKey key);
    std::string terrain_debug_grid_json(const TerrainConfig& config, TerrainChunkKey key, std::uint32_t samples_per_edge);
    }

Exact names may change if implementation finds a better local pattern, but the concepts should remain: addressable chunk key, deterministic config, sample output, mesh output, and read-only debug serialization.

The TypeScript runtime wrapper should expose a method close to:

    terrainDebugGrid(request: TerrainDebugGridRequest): TerrainDebugGrid

The raw Embind shape can remain JSON-based for this first diagnostic API to avoid exposing mutable pointers or bulk binary ownership before the terrain editor direction is clearer.

No new npm dependency is planned. No new graphics engine dependency is planned. A vendored noise implementation may be considered only if the license is clear, the file is documented under `third_party` or local source, and the plan is updated before adoption.
