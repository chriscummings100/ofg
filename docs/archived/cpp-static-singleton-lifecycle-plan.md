# Refactor C++ systems to static lifecycle singletons

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Replace the current renderer ownership shape with three simple static C++ system facades: `Game`, `Renderer`, and `Resources`. The user-facing C++ API should read like engine-level calls rather than dependency-threaded object access:

    Game::init(gpu, color_format);
    while (!Game::prepare()) {
      // keep the host responsive
    }
    Game::update(time_ms);
    Game::render(encoder, target);
    while (!Game::release()) {
      // keep the host responsive
    }
    Game::destroy();

Resource creation should be equally direct:

    Shader& shader = Resources::create_shader("opaque");
    shader.init_from_wgsl(...);
    Texture& texture = Resources::create_texture("checker");
    texture.init_from_rgba8_pixels(...);
    Mesh& mesh = Resources::create_mesh("cube");
    mesh.init(...);
    Material& material = Resources::create_material("cube material");
    material.init(...);

`Resources::create_*` allocates and stores a labeled resource; explicit `init_*` methods make that object valid. The important behavior change is not just naming. `init()` and `destroy()` are single-call lifecycle edges. `prepare()` and `release()` are incremental state machines that may return `false` for multiple frames before returning `true`. The first implementation may still complete most stages immediately, but the state-machine shape must make future non-blocking resource loading, shader compilation, GPU teardown, and scene preparation possible without another public API rewrite. Failures should use C++ exceptions, not caller-populated error strings. In this plan, a boolean return from `prepare()` or `release()` means only "not done yet" or "done"; it must not be overloaded to mean "failed".

After completion, browser and native smoke should still show the same plane-and-cubes demo scene, but the current `Game` object should no longer be passed a `ResourceArena`, `DrawList`, `RenderView`, or `Renderer`. `Game` orchestrates initialization, preparation, release, and destruction order for the three systems; each facade owns its own private singleton instance. `Renderer` owns its pass list internally and renders an explicit scene passed by `Game`. `Resources` owns resource allocation. The existing `DrawList` type may remain as a renderer/pass-internal transient render queue until a later scene-query plan replaces it.

## Progress

- [x] (2026-06-28 00:00Z) Discussed and accepted the target architecture: public static facades for `Game`, `Renderer`, and `Resources`; `init`/`destroy` single-call lifecycle; incremental `prepare`/`release`; and exceptions for failure.
- [x] (2026-06-28 00:00Z) Re-read `PLANS.md`, `AGENTS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and current `Game`, `Renderer`, and `ResourceArena` headers before drafting.
- [x] (2026-06-28 00:00Z) Drafted this ExecPlan at `C:\dev\ofg\docs\plans\cpp-static-singleton-lifecycle-plan.md`.
- [x] (2026-06-28 00:00Z) Reviewed this plan with correctness, completeness, clarity, efficiency, and performance sub-agents, then folded the accepted review decisions back into the plan.
- [x] (2026-06-28 17:05Z) Milestone 1: introduced exception/status foundations and the static `Game` lifecycle facade without changing visible render output.
- [x] (2026-06-28 17:33Z) Milestone 2: introduced static `Resources` around the current stable storage model without converting every resource type at once.
- [x] (2026-06-28 20:40Z) Milestone 3: converted resource creation/init APIs type-by-type, preserving resource ownership through `Resources`.
- [x] (2026-06-28 20:05Z) Milestone 4: introduced a minimal draw-list-backed `Scene` view and reshaped `Renderer` into a static facade that owns internal passes and accepts an explicit scene from `Game`.
- [x] (2026-06-28 21:35Z) Milestone 5: migrated demo scene ownership so `Game` owns `Scene` render objects and `Renderer` builds a transient private draw list from scene data.
- [x] (2026-06-28 21:30Z) Milestone 6: moved `webgpu_common` to generic GPU common helpers, centralized depth target helpers, updated docs/contracts/coverage, completed final validation, and archived the plan.

## Surprises & Discoveries

- Observation: The current renderer-resource implementation already proves the visual and GPU-resource behavior this plan must preserve.
  Evidence: The archived renderer/resource plan records passing `npm test`, `npm run smoke`, `npm run coverage`, and `npm run build:cloudflare` with the plane-and-cubes demo scene.

- Observation: The current public C++ APIs are still error-string based and object-instance based.
  Evidence: `cpp/include/ofg/game/game.hpp`, `cpp/include/ofg/render/renderer.hpp`, and resource headers expose calls such as `Game::create(..., std::string& error)`, `Renderer::create(..., std::string& error)`, and `Texture::from_rgba8_pixels(..., std::string& error)`.

- Observation: `Renderer` is currently a one-pass wrapper rather than a pass graph owner.
  Evidence: `cpp/include/ofg/render/renderer.hpp` stores one `std::unique_ptr<OpaquePass> m_opaque_pass`, and `cpp/src/render/renderer.cpp` forwards prepare, resize, render, and counters directly to that pass.

- Observation: Some WebGPU helper naming and placement still reflects the earliest renderer split.
  Evidence: `cpp/include/ofg/render/webgpu_common.hpp` and `cpp/src/render/webgpu_common.cpp` are used by resources, native smoke, game target validation, tests, and render code, not just by renderer modules.

- Observation: The first plan review showed that lifecycle shape and browser disposal need to be explicit before implementation starts.
  Evidence: Reviewers found that current TypeScript `dispose()` immediately deletes the Embind object, current native smoke relies on local `std::unique_ptr<Game>` cleanup, and static singletons need clear one-live-runtime and reverse-order teardown rules.

- Observation: Browser WASM exception handling works with WebAssembly exception mode in the current Emscripten/Dawn build.
  Evidence: `cpp/CMakeLists.txt` now applies `-fwasm-exceptions` to `ofg_cpp` and `ofg_cpp_wasm`; `npm run build:wasm`, `npm run smoke:browser`, and `npm run smoke:browser:cpp` all passed. The generated sizes after Milestone 1 were `assets\wasm\ofg_cpp\ofg_cpp.js` 91,719 bytes and `assets\wasm\ofg_cpp\ofg_cpp.wasm` 284,846 bytes.

- Observation: The static lifecycle refactor made the active ownership docs stale earlier than the final documentation milestone.
  Evidence: Milestone review found `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` still described the old shared object shape, so Milestone 1 updated those active docs immediately while leaving broader final-doc cleanup to Milestone 6.

- Observation: Two touched implementation files are in the line-count concern range but below the hard split threshold.
  Evidence: `cpp/src/web/browser_game.cpp` is 618 lines and `cpp/src/native/render_smoke.cpp` is 881 lines after Milestone 4. They remain readable enough for this plan, but future browser/native lifecycle or smoke expansion should split them before they approach 1000 lines.

- Observation: `Resources` can own the active `ResourceArena` without changing the current visual resource factories yet.
  Evidence: `Game::prepare_impl()` now initializes and prepares `Resources`, builds the demo through `Resources::arena()`, and `Game::release_impl()` releases/destroys `Resources`. `npm run smoke:render` and `npm run smoke:browser:cpp` both passed with refreshed screenshots.

- Observation: The first `Resources` implementation needed explicit lifecycle-edge tests to satisfy coverage.
  Evidence: The first `npm run coverage:cpp` after adding `Resources` failed with `cpp\src\resources\resources.cpp: 70.00%`. Adding tests for lifecycle names, release-after-access failure, failed prepare retries, and destroy-drains-live-singleton lifted `resources.cpp` to 91.43% and the coverage gate passed.

- Observation: Milestone 3 had one extra public resource helper to convert beyond texture/shader/material/mesh.
  Evidence: `PropertyBag::validate_for_scope` and `PropertyBag::pack_uniforms_for_scope` still required caller-populated `std::string& error`; they now throw `EngineError` and return either void or packed bytes directly. `rg -n "std::string& error" cpp/include/ofg/resources cpp/src/resources` now returns no matches.

- Observation: Render-side error-string APIs remain visible after the resource conversion, but they are outside Milestone 3.
  Evidence: The remaining focused audit finds `update_demo_scene(..., std::string& error)` in `cpp/include/ofg/render/demo_scene.hpp` / `cpp/src/render/demo_scene.cpp`. Full render/renderer error-string conversion is scheduled for Milestones 4 and 5.

- Observation: Milestone 4 could install the scene boundary without designing the final scene object model.
  Evidence: `cpp/include/ofg/scene/scene.hpp` is a small borrowed view over the current `RenderView` plus `DrawList`. `Game::render_impl()` constructs this view and calls `Renderer::render(encoder, target, scene)`, while Milestone 5 remains responsible for moving draw-list construction behind renderer/scene queries.

- Observation: Renderer coverage needed extra lifecycle edge tests after the static facade conversion.
  Evidence: The first `npm run coverage:cpp` for Milestone 4 failed with `cpp\src\render\renderer.cpp: 83.75%`. Adding tests for no-singleton counters/release, invalid color-format init, resize/render before prepare, prepare after release, and failed prepare retries raised `renderer.cpp` to 92.50%; `opaque_pass.cpp` reported 91.19% and `pipeline_cache.cpp` 96.58%.

- Observation: Renderer/pass/cache public APIs no longer require caller-populated error strings after Milestone 4.
  Evidence: `rg -n "std::string& error" cpp/include/ofg/render cpp/src/render` now finds only the planned Milestone 5 draw-list/demo-scene helpers: `DrawList::validate`, `resolve_material`, and `update_demo_scene`.

- Observation: Milestone 5 moved the public scene boundary from draw-list-backed to render-object-backed without changing the visual contract.
  Evidence: `cpp/include/ofg/scene/scene.hpp` stores a main `RenderView` plus `std::vector<RenderObject>`, `Game` stores `Scene m_scene`, and `Renderer::render_impl()` builds its private `m_draw_list` from `scene.render_objects()`. `npm run smoke:render` and `npm run smoke:browser:cpp` both passed with refreshed artifacts.

- Observation: Render and resource public APIs no longer require caller-populated error strings after Milestone 5.
  Evidence: `rg -n "std::string& error" cpp/include/ofg/render cpp/src/render cpp/include/ofg/resources cpp/src/resources` returns no matches. Remaining error-string APIs are in game/runtime/math/platform/test boundary helpers and are recorded as boundary or compatibility adapters.

- Observation: M5 changed enough renderer coverage lines that the committed coverage summary needs refreshing during final validation.
  Evidence: `npm run coverage:cpp` passed after M5 with `cpp\src\render\demo_scene.cpp` 97.71%, `draw_list.cpp` 96.36%, `opaque_pass.cpp` 91.45%, and `renderer.cpp` 93.14%.

- Observation: The generic GPU helper move needed one final hardening pass during Milestone 6 review.
  Evidence: The new `gpu::create_depth_texture` and `gpu::create_depth_view` helpers now validate null handles, zero dimensions, and undefined formats before calling WebGPU. Focused tests cover those early failures, and `npm run coverage:cpp` passed with `cpp\src\gpu\common.cpp` at 96.00% line coverage.

## Decision Log

- Decision: Use static public facades over private `std::unique_ptr` instances of the same class.
  Rationale: The desired API is `Game::update(...)`, `Resources::create_shader(...)`, and `Renderer::render(...)`, not `Game::current().resources()...`. Keeping the actual storage in `std::unique_ptr<Game>`, `std::unique_ptr<Resources>`, and `std::unique_ptr<Renderer>` avoids C++ static destruction surprises around WebGPU handles while preserving singleton ergonomics.
  Date/Author: 2026-06-28 / User and Codex

- Decision: `init()` and `destroy()` are single-call lifecycle edges, while `prepare()` and `release()` are incremental and may need many calls.
  Rationale: Future asset loading and GPU teardown should not block the browser frame loop. This lifecycle lets the host keep pumping browser/device work while systems make bounded progress.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Use exceptions for failure instead of public `std::string& error` parameters.
  Rationale: Error-string plumbing makes the static public API clumsy and can blur progress-state booleans with failure booleans. Exceptions let public methods express failures directly, while `prepare()` and `release()` can reserve `false` for "still working".
  Date/Author: 2026-06-28 / User and Codex

- Decision: Catch exceptions at browser/native platform boundaries and convert them into debug status or process-level diagnostics.
  Rationale: Lower-level C++ classes should be able to throw without every child class carrying error-string plumbing. `Game` keeps last-error storage and static wrappers may catch implementation exceptions to record status before rethrowing. Browser-facing Embind methods and native smoke entry points catch exceptions and convert them into debug status or report diagnostics instead of relying on exceptions crossing the language boundary.
  Date/Author: 2026-06-28 / User and Codex

- Decision: `Resources` should own creation and storage; resource constructors stay minimal and resource initialization becomes explicit methods.
  Rationale: Constructor/destructor auto-registration hides ownership and makes failure/move/destruction order harder to reason about. `Resources::create_texture(...)` plus `Texture::init_from_rgba8_pixels(...)` keeps allocation and initialization clear.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Keep `DrawList` as an internal renderer/pass queue for this plan rather than removing it outright.
  Rationale: The long-term direction is scene queries, but the current renderer is already validated through draw lists. This plan should move draw lists behind `Renderer` and `OpaquePass` first, then leave richer scene-query design to a later pass once the ownership/lifecycle skeleton is stable.
  Date/Author: 2026-06-28 / Codex

- Decision: Browser `dispose()` drains `Game::release()` synchronously for this plan.
  Rationale: The public lifecycle should gain an incremental release shape now, but the TypeScript/browser facade does not need an async disposal contract yet. Synchronous draining preserves today's simple Embind `dispose()` and `delete()` behavior while still forcing release code through the new state machine.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Only one live `Game`/`Renderer`/`Resources` singleton set is supported per process or WASM module.
  Rationale: These systems are explicitly singletons. A second `init()` while a singleton is live should throw a clear `EngineError`; dispose/recreate is supported only after release/destroy completes.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Teardown must happen in the reverse order of startup.
  Rationale: Renderer passes may reference resource-owned materials, buffers, textures, bind groups, and shaders. Releasing/destroying `Renderer` before `Resources`, then clearing `Game` scene/runtime state before borrowed browser/native device handles are released, keeps ownership legible.
  Date/Author: 2026-06-28 / User and Codex

- Decision: `Renderer` should receive the scene explicitly from `Game`.
  Rationale: This avoids `Renderer` reaching back into static `Game`, keeps multi-scene rendering possible later, and still gives the public renderer call a simple shape.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Do not require strict per-call prepare/release budgets in this refactor.
  Rationale: The goal is to install the state-machine architecture now because it is hard to add later. Smooth frame-rate budgeting during loading/release is a future tuning problem. This plan should still require repeated-call safety and clear progress stages.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Pin browser C++ exception support with `-fwasm-exceptions`.
  Rationale: The Milestone 1 browser boundary catches C++ exceptions inside the WASM module before converting them to debug status. `-fwasm-exceptions` kept that behavior direct and passed the Emscripten build plus browser smokes.
  Date/Author: 2026-06-28 / Codex

- Decision: Update active ownership docs during Milestone 1 rather than waiting for final docs.
  Rationale: `Game::create(..., std::string& error)` has been removed from live code, and active contracts should not continue to describe the old object-owned boundary while later milestones build on the static lifecycle.
  Date/Author: 2026-06-28 / Codex

- Decision: Keep `Resources::arena()` as a narrow Milestone 2 compatibility bridge.
  Rationale: The milestone goal was to move top-level resource storage behind the static `Resources` facade without converting every texture, shader, material, and mesh factory at once. Public `Resources::create_*` plus explicit resource `init_*` methods remain the Milestone 3 target.
  Date/Author: 2026-06-28 / Codex

- Decision: Convert `PropertyBag` to exception-based validation during Milestone 3.
  Rationale: Although it is a helper, `PropertyBag` is part of the public resource API. Leaving it as the only caller-filled error-string API in `cpp/include/ofg/resources` would make the resource exception contract misleading.
  Date/Author: 2026-06-28 / Codex

- Decision: Make the first `Scene` type a borrowed `RenderView` plus `DrawList` view.
  Rationale: The important Milestone 4 API change is that `Renderer` receives a scene explicitly from `Game` and does not reach back into `Game`. A full render-object/query scene belongs in Milestone 5; adding it during the renderer lifecycle change would expand the milestone without improving the static facade contract.
  Date/Author: 2026-06-28 / Codex

- Decision: Convert `Renderer`, `OpaquePass`, and `PipelineCache` to throwing APIs during Milestone 4, while leaving `DrawList` and `update_demo_scene` as temporary bool/error adapters for Milestone 5.
  Rationale: The renderer facade should match the accepted exception contract now, but `DrawList` is still a transient migration queue and `update_demo_scene` will change when draw-list construction moves behind scene rendering.
  Date/Author: 2026-06-28 / Codex

## Outcomes & Retrospective

Milestone 1 complete. `Game` now exposes the static lifecycle surface `init`, `prepare`, `resize`, `update`, `render`, `release`, `destroy`, `state`, `last_error`, `record_error`, `record_gpu_error`, `debug_status_json`, and `status`. The private singleton keeps the old `ResourceArena`, `DemoScene`, `DrawList`, `RenderView`, and `Renderer` internals for this milestone, but callers no longer own a `Game` handle. `ofg::EngineError` is the public engine exception type, static `Game` wrappers record `last_error`/lifecycle status before rethrowing where appropriate, and `RuntimeDebugStatus` now includes `lifecycleState`.

`BrowserGame` now creates the static `Game` singleton after browser device setup, drives `Game::prepare()` from frame processing, catches exceptions at Embind/callback boundaries, and drains `Game::release()`/`Game::destroy()` during dispose before browser WebGPU handles are released. Native render smoke uses a stack guard so static `Game` is released and destroyed before borrowed Dawn device/queue handles unwind. TypeScript parses the new `lifecycleState` field.

Milestone review:
- Scope: Milestone 1 static `Game` lifecycle, exception boundary, browser/native callers, debug-status contract, active docs, coverage summaries, and visual artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent review was not spawned because the available multi-agent tool requires an explicit user request for delegation/sub-agents.
- Required findings fixed: active `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` were updated to remove stale old-`Game` ownership claims; TypeScript missing-field test was updated for the new `lifecycleState` parser order.
- Follow-ups recorded: split `cpp/src/web/browser_game.cpp` before more browser lifecycle behavior is added; split `cpp/src/native/render_smoke.cpp` before the next native-smoke expansion; convert remaining resource/renderer `std::string& error` APIs in later milestones.
- Rejected findings: none.
- Validation rerun: `git -c safe.directory=C:/dev/ofg diff --check` after docs/coverage updates; full milestone validation listed below passed.
- Remaining risk: `Game::prepare_impl()` still performs synchronous renderer/resource creation and still routes through internal error-string helpers. That is intentional for Milestone 1 and is scheduled for `Resources`/`Renderer` conversion in Milestones 2-4.

Milestone 2 complete. `Resources` now exposes the static lifecycle surface `init`, `prepare`, `release`, `destroy`, `state`, `gpu_context`, and `arena`. The private singleton owns the active `ResourceArena` and borrowed `GpuContext`; `Game::prepare_impl()` initializes and prepares it before demo-scene construction, and `Game::release_impl()` tears down renderer/demo state before draining and destroying `Resources`. Type-specific resource factories still exist for the moment, but the arena ownership has moved out of `Game`.

Milestone review:
- Scope: Milestone 2 static `Resources` facade, Game orchestration changes, resource lifecycle tests, active ownership docs, coverage summary, and render/browser C++ smoke artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. `docs/ARCHITECTURE.md` is referenced by the review skill but is not present in this repository, so the review used `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `PLANS.md`, and this ExecPlan as the active contract set.
- Required findings fixed: expanded the new `cpp/src/resources/resources.cpp` top-of-file purpose comment; updated `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan to remove stale claims that `Game` owns the resource arena; added resource lifecycle edge tests after the first C++ coverage run exposed insufficient `Resources` coverage.
- Follow-ups recorded: convert `Resources::arena()` callers to `Resources::create_*` plus explicit resource `init_*` methods in Milestone 3; keep the existing browser/native file split pressure visible before adding more lifecycle code.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check` and `git -c safe.directory=C:/dev/ofg diff --check` after docs/comment updates; full milestone validation listed below passed.
- Remaining risk: `Resources` still exposes the compatibility arena and resource creation still goes through error-string factory helpers. That is intentional for Milestone 2 and is the direct target of Milestone 3.

Milestone 3 complete. `Resources::create_texture`, `create_shader`, `create_material`, and `create_mesh` now allocate labeled resources in the `Resources` singleton's stable storage and inject the borrowed `GpuContext`. Texture, shader, material, and mesh GPU data is created by explicit initialization or mutation methods: `Texture::init_from_rgba8_pixels` / `update_pixels`, `Shader::init_from_wgsl` / `replace_source`, `Material::init` / `set_property`, and `Mesh::init` / replacement methods. These APIs now throw `EngineError` on validation or WebGPU creation failures instead of returning `std::optional` or caller-populated error strings. `PropertyBag` validation and uniform packing were also converted to throwing APIs so the public resource layer is consistent.

The demo scene now builds resources through `Resources::create_*` plus explicit init calls. Mips remain part of the texture contract: the generated checker texture still requests `MipMapPolicy::GenerateCpuFullChain`, and texture tests cover full-chain generation for square and odd dimensions. `Resources::arena()` remains as an internal stable-storage compatibility/diagnostics accessor, but resource creation no longer flows through public static factories.

Milestone review:
- Scope: Milestone 3 resource allocation/init API conversion, `PropertyBag` exception conversion, demo scene resource construction, resource tests, active ownership docs, coverage summary, and render/browser C++ smoke artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. `docs/ARCHITECTURE.md` is still absent, so the review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan.
- Required findings fixed: updated `Resources::create_*` and `Resources::arena()` invalid-lifecycle diagnostics from "prepared or preparing" to "live Resources singleton before release" because allocation is allowed after `init()` and before `prepare()`.
- Follow-ups recorded: renderer/draw-list/demo-scene bool/error APIs remain for Milestones 4 and 5; `cpp/src/web/browser_game.cpp` and `cpp/src/native/render_smoke.cpp` remain in the line-count concern range and should be split before further growth.
- Rejected findings: none.
- Validation rerun: `npm run coverage:cpp`, `npm run build:wasm`, `npm run format:cpp:check`, `git -c safe.directory=C:/dev/ofg diff --check`, and focused resource API audits after the review fix. Full Milestone 3 validation listed below passed.
- Remaining risk: `Renderer`, `OpaquePass`, `DrawList`, and `update_demo_scene` still expose caller-populated error strings and `Game` still owns the current draw list/render view/renderer instance. That is intentional after Milestone 3 and is the direct target of Milestones 4 and 5.

Milestone 4 complete. `Renderer` now exposes the static lifecycle surface `init`, `prepare`, `resize`, `render`, `release`, `destroy`, `state`, and `counters`, backed by a private singleton. The private renderer creates and owns its internal pass list; the current list contains one `OpaquePass`. `Game` no longer stores a `std::unique_ptr<Renderer>` and now initializes, prepares, resizes, renders through, releases, and destroys the static `Renderer` facade. Teardown is in reverse ownership order: `Renderer` is released/destroyed before `Game` clears draw-list/demo state and before `Resources` is released/destroyed.

`cpp/include/ofg/scene/scene.hpp` now provides a minimal explicit scene boundary. For this milestone it borrows the current `RenderView` and `DrawList`; `Game::render_impl()` passes that scene to `Renderer::render`. `Renderer`, `OpaquePass`, and `PipelineCache` now throw `EngineError` instead of returning bool/error values. The pass still uses temporary adapters around `DrawList::validate`, `resolve_material`, and `update_demo_scene`; moving draw-list construction behind renderer scene queries remains Milestone 5.

Milestone review:
- Scope: Milestone 4 static `Renderer` lifecycle, minimal `Scene` boundary, renderer/pass/cache exception APIs, `Game` renderer orchestration, renderer tests, active docs, coverage summary, and render/browser C++ smoke artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. `docs/ARCHITECTURE.md` is still absent, so the review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan.
- Required findings fixed: the first C++ coverage run exposed insufficient `renderer.cpp` coverage; focused lifecycle edge tests were added and `npm run coverage:cpp` then passed. No further required findings were found in the local review.
- Follow-ups recorded: `DrawList::validate`, `resolve_material`, and `update_demo_scene` still use bool/error adapters and are scheduled for Milestone 5; `cpp/src/web/browser_game.cpp` is 618 lines and `cpp/src/native/render_smoke.cpp` is 881 lines, so both should be split before more browser/native lifecycle or smoke growth.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run coverage:cpp`, `npm run smoke:render`, `npm run smoke:browser:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and focused renderer/error-string audits after review fixes. Full Milestone 4 validation listed below passed.
- Remaining risk: `Scene` is intentionally still draw-list-backed and `Game` still owns the current `DrawList`/`RenderView`; Milestone 5 must move draw-list construction behind the renderer/scene boundary so passes can query renderable objects rather than consuming a game-provided draw list.

Milestone 5 complete. `Scene` now stores the main `RenderView` and a stable-order list of `RenderObject` values. `Game` owns that `Scene` directly instead of owning a top-level `DrawList` or `RenderView`; `update_demo_scene` rebuilds render objects and camera state into `Game`'s scene while generated textures, materials, shader, and meshes stay resource-owned. `Renderer` owns a private `DrawList m_draw_list`, clears and rebuilds it from `scene.render_objects()` during `Renderer::render`, validates it with throwing draw-list APIs, and feeds that transient queue into the current opaque pass.

`DrawList::validate`, `resolve_material`, and `update_demo_scene` now throw `EngineError` instead of requiring caller-populated error strings. The scene and draw-list tests cover render-object population, invalid scene object validation, material override resolution, and steady-state renderer counter stability. The visual smoke output remains the plane-and-cubes scene.

Milestone review:
- Scope: Milestone 5 render-object `Scene`, `Game` scene ownership, renderer-private draw-list construction, draw-list/demo-scene exception APIs, tests, active docs, coverage summary, and native/browser C++ smoke artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tooling was available, but not used because the active multi-agent tool contract requires an explicit user request for delegated/sub-agent work. `docs/ARCHITECTURE.md` is still absent, so the review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan.
- Required findings fixed: active docs still described `Scene` as draw-list-backed after M5; `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` were updated to describe render objects plus renderer-private transient draw-list construction. `scene.hpp` also gained an explicit `<cstddef>` include for `std::size_t`.
- Follow-ups recorded: `cpp/src/web/browser_game.cpp` is 617 lines and `cpp/src/native/render_smoke.cpp` is 880 lines, so both should be split before more browser/native lifecycle or smoke growth. Milestone 6 still needs to rename `webgpu_common` and centralize depth helpers.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run build:wasm`, `npm run smoke:render`, `npm run smoke:browser:cpp`, `npm run coverage:cpp`, `npm run format:cpp:check`, `git -c safe.directory=C:/dev/ofg diff --check`, and focused ownership/error-string audits. Full Milestone 5 validation listed below passed.
- Remaining risk: `DrawList` remains an internal renderer/pass queue, which is intentional for this plan. Richer scene queries, multiple scenes, and non-opaque pass filtering should be planned separately after this ownership/lifecycle foundation is complete.

Milestone 6 complete. The old renderer-named WebGPU helper module has moved from `cpp/include/ofg/render/webgpu_common.hpp` / `cpp/src/render/webgpu_common.cpp` to `cpp/include/ofg/gpu/common.hpp` / `cpp/src/gpu/common.cpp`, keeping namespace `ofg::gpu` because the helpers are still WebGPU C API utilities. `OpaquePass` no longer owns local depth texture/view creation helpers; it calls `gpu::create_depth_texture` and `gpu::create_depth_view` while retaining ownership of the returned handles. The common helper module validates invalid depth-helper inputs before calling WebGPU, and `cpp/tests/gpu_common_test.cpp` covers string helpers, enum labels, real depth target creation, and precondition failures.

`cpp/CMakeLists.txt` now builds `src/gpu/common.cpp`, `tools/cpp-coverage.mjs` includes `cpp/src/gpu`, and `docs/SYSTEMS.md` plus `docs/coverage/latest.md` describe the generic GPU helper path and current coverage. The final active-source audits find no `webgpu_common` references and no render/resource public APIs requiring `std::string& error`. Browser smoke and native smoke still render the plane-and-cubes scene.

Milestone review:
- Scope: Milestone 6 GPU common helper rename, centralized depth target helpers, coverage tooling/docs, active system docs, final validation, and smoke artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tooling was not used because delegated sub-agent review requires an explicit user request in this environment. `docs/ARCHITECTURE.md` is still absent, so the review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan.
- Required findings fixed: the new common depth helpers initially delegated null-handle/zero-size validation to WebGPU; they now throw `EngineError` before WebGPU calls, and focused tests cover invalid device, size, texture, and format inputs. The first version of those tests produced local `[[nodiscard]]` warnings through doctest macros; wrapping the calls in small lambdas removed the warnings.
- Follow-ups recorded: `cpp/src/native/render_smoke.cpp` is 790 lines and `cpp/src/web/browser_game.cpp` is 548 lines after formatting and should be split before further smoke/browser lifecycle growth. `DrawList` intentionally remains a renderer/pass-internal queue until a later scene-query/pass-filtering plan.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm test`, `npm run coverage`, `npm run smoke`, `npm run build:cloudflare`, `npm run format:cpp:check`, focused stale-name/error-string audits, and `git -c safe.directory=C:/dev/ofg diff --check` after the review fix. Full final validation listed below passed.
- Remaining risk: the lifecycle architecture is now in place, but prepare/release stages still complete synchronously in practice. Future non-blocking resource loading and richer scene queries should build on this API rather than changing it again.

## Contract and Quality Baseline

This plan intentionally updates `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` is preserved. TypeScript still owns DOM boot, canvas sizing, fatal-error display, WASM module loading, and smoke helpers. TypeScript must not own renderer resources, game state, scene queries, resource loading, render passes, or WebGPU draw submission.

`OFG-BOOT-002 C++ Runtime Ownership` is intentionally revised. C++ still owns frame state, debug status, demo-scene data, high-level renderer resources, draw submission, browser WebGPU behavior, and native Dawn offscreen rendering. The update is that C++ exposes static system facades. `Game` owns the lifecycle and top-level scene/update flow; `Resources` owns resource creation/storage; `Renderer` owns render passes and transient render queues.

`OFG-BOOT-003 WASM Facade` is preserved but must be reworked internally. TypeScript should still call a narrow browser runtime object. Browser runtime creation calls `Game::init` and drives `Game::prepare` from frame processing until ready. It calls `Game::update` and `Game::render` only once ready. Browser `dispose()` drains `Game::release()` synchronously until it returns `true`, then calls `Game::destroy()` before releasing borrowed WebGPU handles or allowing the Embind object to be deleted. Embind-facing calls catch C++ exceptions and record them in debug status.

`OFG-BOOT-004 Renderer Compatibility` is preserved visually. Browser and native smoke must still validate the same plane-and-cubes output, clear color, textured checker ground, colored cube categories, resource layer, and opaque shader path. Internally, `Renderer` should own its pass list and build transient draw lists from scene data rather than receiving a game-owned draw list. `Game` passes an explicit scene to the renderer; `Renderer` must not reach back into static `Game` for scene state.

`OFG-BOOT-005 WebGPU Baseline` is preserved. The renderer must still request no optional GPU features and no manual limits above adapter defaults. The current draw-list visual remains the same after this refactor.

`OFG-BOOT-006 Resource Lifetime` is intentionally refined. Durable GPU resources are still created during initialization, preparation, explicit mutation, scene-dirty preparation, or resize, not ordinary steady-state frames. `Renderer::render` should encode commands and write per-frame uniform data; it should not create pipelines, grow durable buffers, or create depth targets on a normal unchanged frame. Resource creation now flows through `Resources`; explicit `init_*`, mutation, prepare, release, and destructor paths must preserve WebGPU handle lifetime. `Resources` owns the borrowed `GpuContext` for the active device lifetime and injects it into resources it creates or prepares; resource code should not use an unrelated ambient global device.

`OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage` are preserved. Generated outputs remain under `artifacts`, `.deploy`, `dist`, `dist-test`, and `assets/wasm/ofg_cpp`. Coverage must pass for modified implementation files unless this plan records an explicit exception.

Quality constraints from `C:\dev\ofg\AGENTS.md` apply. New or changed C++ files need maintained top-of-file comments; every function written should have a purpose comment; functions over 50 lines should have useful internal comments. Non-generated files in the 500-1000 line range should be watched and files over 1000 lines should be split.

## Context and Orientation

The repository root is `C:\dev\ofg`. The active runtime is C++/WASM with a TypeScript browser host. C++ code lives under `C:\dev\ofg\cpp`; browser TypeScript lives under `C:\dev\ofg\src`; tools live under `C:\dev\ofg\tools`.

The current game facade is `C:\dev\ofg\cpp\include\ofg\game\game.hpp` and `C:\dev\ofg\cpp\src\game\game.cpp`. It is constructed through the static `Game::init(GpuContext, WGPUTextureFormat)` call and then driven by repeated `Game::prepare()`, per-frame `Game::update()`/`Game::render()`, repeated `Game::release()`, and `Game::destroy()`.

The current `Game` singleton owns `GameRuntime`, `DemoScene`, and the active `Scene`. It no longer owns a renderer object, top-level `DrawList`, or top-level `RenderView`; it initializes and drives the static `Renderer` facade and passes its owned `Scene` into `Renderer::render`. The active high-level resource storage has moved behind `C:\dev\ofg\cpp\include\ofg\resources\resources.hpp` and `C:\dev\ofg\cpp\src\resources\resources.cpp`, where `Resources` owns a private singleton, the borrowed `GpuContext`, and an internal `ResourceArena`. `Resources::arena()` remains only as a narrow compatibility and diagnostics accessor. Browser and native frame drivers own WebGPU target acquisition and command-buffer submit, but they no longer hold or create a `Game` object.

The current renderer is `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp` and `C:\dev\ofg\cpp\src\render\renderer.cpp`. It is a static facade backed by a private singleton and owns a list of passes it creates internally. The current pass is `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp` and `C:\dev\ofg\cpp\src\render\opaque_pass.cpp`; `Renderer` builds a private transient `DrawList` from `Scene` render objects before invoking that pass.

The current resource owner is the static `Resources` facade backed by `C:\dev\ofg\cpp\include\ofg\resources\resource_arena.hpp` and `C:\dev\ofg\cpp\src\resources\resource_arena.cpp` as internal stable storage. `Resources::create_texture`, `Resources::create_shader`, `Resources::create_material`, and `Resources::create_mesh` allocate labeled resource objects and inject the active borrowed `GpuContext`; explicit methods such as `Texture::init_from_rgba8_pixels`, `Shader::init_from_wgsl`, `Material::init`, and `Mesh::init` validate data and create GPU state.

The current demo scene is `C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp` and `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It builds generated textures, a shader, materials, a ground mesh, a cube mesh, and updates the active scene's main render view plus render objects each frame.

The current generic WebGPU helpers live at `C:\dev\ofg\cpp\include\ofg\gpu\common.hpp` and `C:\dev\ofg\cpp\src\gpu\common.cpp`. They provide WebGPU string-view helpers, public enum labels, and reusable depth texture/view helpers while leaving WebGPU handle ownership with the renderer, resources, browser, or native smoke code that creates the handles.

Definitions used in this plan:

Static facade: a class whose public methods are static and forward to a private singleton instance of the same class. For example, `Game::update(time_ms)` forwards to `Game::s_game->update_impl(time_ms)`.

Singleton instance: the private `std::unique_ptr` owned by the class, such as `static std::unique_ptr<Game> s_game`. It is initialized by `init()` and cleared by `destroy()`.

Prepare stage: a small enum value stored by `Game`, `Resources`, or `Renderer` that records which phase of non-blocking startup is currently in progress.

Release stage: a small enum value stored by `Game`, `Resources`, or `Renderer` that records which phase of non-blocking teardown is currently in progress.

Engine exception: a C++ exception type, likely `ofg::EngineError`, thrown when engine code detects invalid input, impossible state, WebGPU creation failure, validation failure, or resource/renderer lifecycle misuse.

Scene: a minimal render-facing boundary owned by `Game` and passed explicitly to `Renderer`. It currently stores a main `RenderView` plus stable-order `RenderObject` values that name a mesh, model matrix, draw properties, material overrides, and sort origin. A full world scene graph or ECS is out of scope for this plan.

## Plan of Work

Milestone 1 introduces the lifecycle and exception foundation. Add a small exception type such as `C:\dev\ofg\cpp\include\ofg\core\engine_error.hpp` and `C:\dev\ofg\cpp\src\core\engine_error.cpp` if a source file is useful. Decide and pin the CMake/Emscripten exception mode after a tiny build check. Native Clang tests already use exceptions through doctest, but browser WASM builds must explicitly support catching C++ exceptions inside the module. Convert `Game` first: add `Game::init`, `Game::prepare`, `Game::resize`, `Game::update`, `Game::render`, `Game::release`, `Game::destroy`, `Game::state`, `Game::debug_status_json`, `Game::status`, `Game::last_error`, and `Game::record_error` as static methods. Internally, `Game` should store `static std::unique_ptr<Game> s_game` and private `*_impl` methods. Static wrappers may catch lower-level exceptions to record `last_error` and failed status before rethrowing to C++ callers. Debug status JSON should keep its current fields stable and add or map lifecycle state clearly enough that TypeScript and smoke can distinguish preparing, ready, releasing, released, and failed states. Keep the current visual behavior and current `ResourceArena`/`Renderer` internals for this milestone. Update `BrowserGame` and native smoke so they call the static lifecycle and catch exceptions at every boundary that can call engine code: create, resize, frame/update/render, debug status, dispose, and callback-driven error paths. Browser `dispose()` must synchronously drain `Game::release()` before `Game::destroy()` and Embind deletion. Native smoke must use a cleanup guard so static `Game` state is destroyed before its borrowed Dawn queue/device handles go away, even on exceptions.

Milestone 2 introduces static `Resources` without converting every resource type at once. Add `Resources` under `C:\dev\ofg\cpp\include\ofg\resources\resources.hpp` and `C:\dev\ofg\cpp\src\resources\resources.cpp`. The class has a static facade and private `std::unique_ptr<Resources> s_resources`. It initially owns the same stable vectors that `ResourceArena` owns today, stores the active borrowed `GpuContext`, provides `Resources::init(GpuContext)`, `Resources::prepare()`, `Resources::release()`, `Resources::destroy()`, and can keep narrow compatibility methods that accept already-created resources while demo/renderer code is migrated. A second `Resources::init` while live throws. `Resources::prepare()` and `Resources::release()` should be implemented as repeated-call-safe state machines even if each current stage completes immediately. The old `ResourceArena` name can remain as a compatibility alias only within this milestone if it materially reduces churn.

Milestone 3 converts resource creation and initialization APIs type-by-type. `Resources::create_texture`, `Resources::create_shader`, `Resources::create_material`, and `Resources::create_mesh` allocate labeled resources and return stable references. `Texture`, `Shader`, `Material`, and `Mesh` gain minimal constructors plus explicit initialization methods such as `Texture::init_from_rgba8_pixels`, `Shader::init_from_wgsl`, `Material::init`, and `Mesh::init`. `Resources` injects its stored `GpuContext` into resources during creation or preparation; resource code must not reach for an unrelated global device. Convert resource validation and GPU creation failures from error strings to `EngineError`. Remove public static resource factory functions or keep them only as private/test compatibility wrappers until this milestone ends.

Milestone 4 introduces a minimal scene type and reshapes `Renderer` into a static pass owner while preserving a valid draw source. Add a small `Scene` type under `C:\dev\ofg\cpp\include\ofg\scene\`. The first implementation may be a borrowed `RenderView` plus `DrawList` view so the static renderer contract changes before the full scene-query model arrives. `Renderer` should store `static std::unique_ptr<Renderer> s_renderer` and expose static `init`, `prepare`, `resize`, `render`, `release`, `destroy`, and `counters`. `Renderer::render` receives the current scene explicitly, for example `Renderer::render(encoder, target, scene)`. The private renderer object creates its own pass or passes; it may keep a concrete list of opaque passes until a second pass makes an abstract `RenderPass` interface worthwhile. The requirement is internal pass ownership, not premature abstraction. Convert renderer and pass errors to exceptions. Ordinary steady-state frames should keep durable renderer counters stable; first render for a new scene/material combination may still populate missing cached pipelines.

Milestone 5 migrates the demo scene and draw-list ownership behind renderer boundaries. `Game` owns the scene and updates it from the demo scene each frame. `Game::render_impl` passes its owned scene explicitly to `Renderer::render`; `Renderer` must not reach back into static `Game` for scene access. `DrawList` remains as an internal pass queue for now, built from the scene by renderer/pass code. The common path should reuse storage or retain capacity for per-frame render queues rather than adding obvious heap churn during every frame.

Milestone 6 centralizes common GPU helpers and finishes docs, tests, and smoke. Move `ofg/render/webgpu_common.hpp` and `.cpp` to a generic path such as `ofg/gpu/common.hpp` and `cpp/src/gpu/common.cpp`. Keep namespace `ofg::gpu`. Move reusable depth target helpers out of `OpaquePass` into the GPU common module, with names that do not imply a renderer-specific wrapper around all WebGPU types. Update `cpp/CMakeLists.txt`, tests, docs, includes, `tools/cpp-coverage.mjs`, and `COVERAGE.md` or coverage docs as needed so new `cpp/src/gpu` and `cpp/src/scene` files are checked. Update `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `README.md`, and `AGENTS.md` where needed. Run final test, smoke, coverage, Cloudflare packaging, and screenshot validation. Archive this plan when complete.

After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

Work from `C:\dev\ofg`.

Milestone 1 likely touches:

    cpp/include/ofg/core/engine_error.hpp
    cpp/src/core/engine_error.cpp
    cpp/include/ofg/game/game.hpp
    cpp/src/game/game.cpp
    cpp/include/ofg/web/browser_game.hpp
    cpp/src/web/browser_game.cpp
    cpp/src/native/render_smoke.cpp
    cpp/src/native/render_smoke_main.cpp
    cpp/tests/game_runtime_test.cpp
    cpp/tests/static_lifecycle_test.cpp if a separate lifecycle test file is clearer
    cpp/CMakeLists.txt
    tools/build-cpp-wasm.mjs if exception-mode probing belongs in tooling

Milestone 1 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run build:wasm
    npm run smoke:browser
    npm run smoke:browser:cpp
    record generated `assets\wasm\ofg_cpp\ofg_cpp.wasm` and `ofg_cpp.js` sizes after pinning exception mode
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2 likely touches:

    cpp/include/ofg/resources/resource_arena.hpp
    cpp/src/resources/resource_arena.cpp
    cpp/include/ofg/resources/resources.hpp
    cpp/src/resources/resources.cpp
    cpp/src/render/demo_scene.cpp
    cpp/tests/resource_arena_test.cpp or renamed resources test
    cpp/tests/resource_gpu_test.cpp
    tools/cpp-coverage.mjs

Milestone 2 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run coverage:cpp
    npm run build:wasm
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 3 likely touches:

    cpp/include/ofg/resources/resources.hpp
    cpp/src/resources/resources.cpp
    cpp/include/ofg/resources/texture.hpp
    cpp/src/resources/texture.cpp
    cpp/include/ofg/resources/shader.hpp
    cpp/src/resources/shader.cpp
    cpp/include/ofg/resources/material.hpp
    cpp/src/resources/material.cpp
    cpp/include/ofg/resources/mesh.hpp
    cpp/src/resources/mesh.cpp
    cpp/src/render/demo_scene.cpp
    cpp/tests/*resource*_test.cpp
    tools/cpp-coverage.mjs

Milestone 3 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run coverage:cpp
    npm run build:wasm
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 4 likely touches:

    cpp/include/ofg/render/renderer.hpp
    cpp/src/render/renderer.cpp
    cpp/include/ofg/render/opaque_pass.hpp
    cpp/src/render/opaque_pass.cpp
    cpp/include/ofg/render/render_pass.hpp if an interface is added
    cpp/include/ofg/scene/scene.hpp or cpp/include/ofg/game/scene.hpp
    cpp/src/scene/scene.cpp or cpp/src/game/scene.cpp
    cpp/tests/renderer_test.cpp
    cpp/tests/pipeline_cache_test.cpp
    cpp/tests/scene_test.cpp if a separate scene test file is clearer
    tools/cpp-coverage.mjs

Milestone 4 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run smoke:render
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 5 likely touches:

    cpp/include/ofg/game/game.hpp
    cpp/src/game/game.cpp
    cpp/include/ofg/render/demo_scene.hpp
    cpp/src/render/demo_scene.cpp
    cpp/include/ofg/render/draw_list.hpp
    cpp/src/render/draw_list.cpp
    cpp/include/ofg/scene/scene.hpp or cpp/include/ofg/game/scene.hpp if the scene shape needs refinement
    cpp/src/scene/scene.cpp or cpp/src/game/scene.cpp if the scene shape needs refinement
    cpp/tests/demo_scene_test.cpp
    cpp/tests/draw_list_test.cpp
    cpp/tests/renderer_test.cpp
    tools/cpp-coverage.mjs

Milestone 5 validation:

    npm test
    npm run smoke
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 6 likely touches:

    cpp/include/ofg/gpu/common.hpp
    cpp/src/gpu/common.cpp
    cpp/CMakeLists.txt
    tools/cpp-coverage.mjs
    COVERAGE.md or docs/coverage summaries if coverage docs need current paths
    docs/API_CONTRACTS.md
    docs/SYSTEMS.md
    README.md
    AGENTS.md

Milestone 6 final validation:

    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    rg -n "std::string& error" cpp/include cpp/src
    git -c safe.directory=C:/dev/ofg diff --check

For browser or visual work, keep a dev server available for human review:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, use the alternate URL printed by the tool. Capture screenshots under `C:\dev\ofg\artifacts\browser-smoke` or another clear subdirectory under `C:\dev\ofg\artifacts` after the first successful static-lifecycle browser render and before finalizing.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

Milestone reviews for this plan must explicitly check:

- No public engine-facing API still requires caller-populated error strings unless recorded as a temporary migration exception.
- Temporary migration exception: after Milestone 1, existing internal resource and renderer helper APIs may still expose `std::string& error` while the static `Game` API and browser/native entry points move to exceptions. Those remaining public resource/renderer error-string APIs must be converted, privatized, or explicitly justified by the end of Milestones 3 and 4.
- `prepare()` and `release()` booleans are not used to report fatal errors.
- `prepare()` and `release()` are repeated-call safe and expose clear progress stages, even if the first implementation completes each stage immediately.
- Browser/native boundaries catch `std::exception` for every Embind/native entry point that touches engine code and preserve useful debug/report output.
- `Game`, `Renderer`, and `Resources` singleton instances are initialized and destroyed in deterministic reverse order: `Game::init` creates or initializes `Resources`, then `Renderer`, then scene state; release/destroy tears down scene state, then `Renderer`, then `Resources`.
- A second live `init()` throws clearly, and dispose/recreate works after release/destroy completes.
- WebGPU handles are released before the borrowed browser/native device and queue are released.
- Browser `dispose()` drains `Game::release()` synchronously before `Game::destroy()` and before Embind deletion.
- Native smoke uses a guard or equivalent cleanup path so static singleton state is destroyed before Dawn queue/device handles on exceptions.
- `Renderer` creates and owns passes internally.
- `Renderer::render` does not create durable GPU resources on ordinary steady-state frames; pipeline and buffer counters stay stable across repeated unchanged frames.
- `Game` no longer owns a top-level `DrawList` or `RenderView` as a public renderer boundary once Milestone 5 is complete.
- `Game` owns scene state and passes that scene explicitly to `Renderer`; `Renderer` does not reach back into static `Game`.
- New `cpp/src/gpu` or `cpp/src/scene` implementation files are included in coverage gates.

## Validation and Acceptance

The plan is accepted when the public C++ engine API is static and simple:

    Game::init(...);
    Game::prepare();
    Game::resize(...);
    Game::update(...);
    Game::render(...);
    Game::release();
    Game::destroy();

    Resources::init(...);
    Resources::prepare();
    Resources::create_shader(...);
    Resources::create_texture(...);
    Resources::create_material(...);
    Resources::create_mesh(...);
    Resources::release();
    Resources::destroy();

    Renderer::init(...);
    Renderer::prepare();
    Renderer::resize(...);
    Renderer::render(..., const Scene& scene);
    Renderer::release();
    Renderer::destroy();

Public engine APIs should throw `ofg::EngineError` or another documented standard-exception-derived type on failure. Lower-level classes should be free to throw; they should not be forced to thread error strings through every call. Static `Game` wrappers may catch implementation exceptions to store `last_error`, update lifecycle status, and then rethrow for higher-level C++ callers. Browser/native platform boundaries catch exceptions and convert them into debug status or smoke reports. Public engine APIs should not require `std::string& error`, except for narrow boundary or compatibility adapters explicitly recorded in this plan while the migration is in progress.

`prepare()` and `release()` must be safe to call repeatedly. Calling `prepare()` after completion should return `true`. Calling `release()` after completion should return `true`. Invalid lifecycle order should throw a clear exception or be explicitly documented if it is idempotent.

`init()` may be called only when no singleton instance is live; a second live init throws. `destroy()` is `noexcept` and idempotent. It must clear any partially initialized singleton and may be called after failed prepare/release.

Debug status remains a public browser/smoke contract. The final status JSON must preserve existing smoke-required fields such as initialization, frame count, counters, and last error, and it must expose or clearly map the new lifecycle states: preparing, ready, releasing, released, and failed.

The first implementation does not need strict per-frame prepare/release budgets. It does need named stages, progress state, and tests proving repeated calls are valid so future non-blocking loading can add budgets without changing the public API.

Steady-state rendering must not create durable GPU resources. After preparation and resize are complete, repeated ordinary frames should not increase pipeline or durable buffer counters.

The final error-string audit is a search, not a command that must produce no output blindly. Run `rg -n "std::string& error" cpp/include cpp/src` near the end. Remaining matches must be private helpers, test/native/browser boundary adapters, or explicitly recorded exceptions. No user-facing engine API should require caller-populated error strings.

Browser smoke and native smoke must still render the plane-and-cubes scene. Native smoke output should still include `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`. Browser smoke should still produce a screenshot and report under `C:\dev\ofg\artifacts\browser-smoke`. Focused C++ browser smoke should still validate WebGPU initialization/status behavior plus demo-scene pixels.

Coverage acceptance follows `C:\dev\ofg\COVERAGE.md` and `OFG-BOOT-009`: modified implementation files must pass the default coverage attention gate, currently 90% line coverage, unless this plan records an explicit exception with rationale.

Final validation must pass:

    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    rg -n "std::string& error" cpp/include cpp/src
    git -c safe.directory=C:/dev/ofg diff --check

## Idempotence and Recovery

The migration should preserve visible output after every milestone. If one milestone creates a temporary adapter, keep the adapter narrow and remove it by the milestone that no longer needs it.

Static singleton state must be reset in tests. Add scoped test helpers if needed so one test cannot leak `Game`, `Renderer`, or `Resources` state into another test. A failed `prepare()` or `release()` that throws should leave enough state for `destroy()` to clean up safely.

Browser runtime tests and smoke should cover dispose/recreate after static singleton teardown. The static singleton policy supports one live runtime at a time; second live creation throws until the previous runtime has been released and destroyed.

If browser WASM exceptions require a different Emscripten mode than expected, record the exact flag decision and measured WASM-size impact in Surprises & Discoveries before proceeding. Do not silently weaken the exception API back to public error strings.

If the resource API conversion becomes too large for one milestone, keep old static factories as private or test-only compatibility helpers for one milestone, but the final accepted plan should expose `Resources::create_*` plus explicit resource `init_*` methods as the normal API.

If `cpp/src/web/browser_game.cpp` grows substantially while adding lifecycle catch/drain logic, either split out lifecycle/dispose helpers in that milestone or record a follow-up before the file approaches the 1000-line threshold.

If the scene boundary starts expanding toward a full ECS or asset system, stop and record a new plan. This plan should introduce only the minimal scene/render-object shape needed to move draw-list ownership out of `Game` and behind `Renderer`.

Generated directories `C:\dev\ofg\dist`, `C:\dev\ofg\dist-test`, `C:\dev\ofg\.deploy`, `C:\dev\ofg\artifacts`, and `C:\dev\ofg\assets\wasm\ofg_cpp` can be regenerated by existing npm scripts. Do not manually preserve generated files as source of truth.

## Artifacts and Notes

Expected durable implementation artifacts include:

    C:\dev\ofg\cpp\include\ofg\core\engine_error.hpp
    C:\dev\ofg\cpp\include\ofg\resources\resources.hpp
    C:\dev\ofg\cpp\src\resources\resources.cpp
    C:\dev\ofg\cpp\include\ofg\render\render_pass.hpp if needed
    C:\dev\ofg\cpp\include\ofg\gpu\common.hpp
    C:\dev\ofg\cpp\src\gpu\common.cpp
    C:\dev\ofg\cpp\include\ofg\scene\scene.hpp or C:\dev\ofg\cpp\include\ofg\game\scene.hpp

Expected visual artifacts:

    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png

Record command transcripts here in concise form as milestones complete.

Milestone 1 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp:check
    Result: passed.

    npm run test:cpp
    Result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed.

    npm run build:wasm
    Result: passed. Generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.
    Sizes after exception-mode pin: `ofg_cpp.js` 91,719 bytes; `ofg_cpp.wasm` 284,846 bytes.

    npm run test:ts
    Result: passed. Mocha reported 19 passing tests.

    npm run smoke:browser
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\browser-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    npm run smoke:render
    Result: passed. PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run coverage:cpp
    Result: passed. Checked touched files included `cpp/src/game/game_runtime.cpp` and `cpp/src/runtime/runtime_debug_status.cpp` at 100.00% line coverage.

    npm run coverage:ts
    Result: passed. `src/app/wasmRuntime.ts` reported 94.75% line coverage.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed.

Milestone 2 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp:check
    Result: passed.

    npm run test:cpp
    Result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed.

    npm run coverage:cpp
    First result: failed because `cpp\src\resources\resources.cpp` was 70.00%.
    Fix: added focused `Resources` lifecycle edge tests.
    Final result: passed. `cpp\src\resources\resources.cpp` reported 91.43% line coverage, and all checked C++ files met the 90% gate.

    npm run build:wasm
    Result: passed. Generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.

    npm run smoke:render
    Result: passed. PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed.

Milestone 3 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp
    Result: passed.

    npm run test:cpp
    First result after converting `PropertyBag`: failed on a doctest macro expression that used a comma in a lambda capture list inside `CHECK_THROWS_WITH_AS`.
    Fix: changed the assertion lambdas to capture by reference with `[&]`.
    Final result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed.

    npm run coverage:cpp
    Result: passed. Resource coverage after the exception API conversion: `cpp\src\resources\material.cpp` 91.57%, `mesh.cpp` 95.00%, `property_bag.cpp` 94.56%, `resources.cpp` 90.43%, `shader.cpp` 94.84%, and `texture.cpp` 93.73%.

    npm run build:wasm
    Result: passed. Generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.

    npm run smoke:render
    Result: passed. PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    npm run format:cpp:check
    Result: passed.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed.

    rg -n "std::string& error" cpp/include/ofg/resources cpp/src/resources
    Result: no matches.

Milestone 4 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp
    Result: passed.

    npm run test:cpp
    Result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed. An earlier wrapper attempt timed out while a cold Dawn build was still running; after that build completed, direct CTest passed, and the full project command was rerun with a longer timeout successfully.

    npm run build:wasm
    Result: passed. Generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.

    npm run smoke:render
    Result: passed. PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    npm run coverage:cpp
    First result: failed because `cpp\src\render\renderer.cpp` was 83.75%.
    Fix: added focused renderer lifecycle edge tests.
    Final result: passed. Render coverage after the static renderer conversion: `cpp\src\render\opaque_pass.cpp` 91.19%, `pipeline_cache.cpp` 96.58%, and `renderer.cpp` 92.50%.

    npm run format:cpp:check
    Result: passed.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed with CRLF warnings only.

    rg -n "Renderer::create|std::unique_ptr<Renderer>|renderer->|m_renderer" cpp/include cpp/src cpp/tests
    Result: no old owned-renderer API matches; remaining `std::unique_ptr<Renderer>` matches are the private static singleton storage.

    rg -n "std::string& error" cpp/include/ofg/render cpp/src/render
    Result: remaining matches are the planned Milestone 5 `DrawList`/`update_demo_scene` migration helpers only.

Milestone 5 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp
    Result: passed.

    npm run test:cpp
    Result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed. A first run after the draw-list exception conversion emitted three local `[[nodiscard]]` warnings in `draw_list_test.cpp`; wrapping those throwing checks in lambdas removed the warnings, and the full command was rerun successfully.

    npm run build:wasm
    Result: passed. Generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.

    npm run smoke:render
    Result: passed. PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    npm run coverage:cpp
    Result: passed. Render coverage after the scene ownership migration: `cpp\src\render\demo_scene.cpp` 97.71%, `draw_list.cpp` 96.36%, `opaque_pass.cpp` 91.45%, and `renderer.cpp` 93.14%.

    npm run format:cpp:check
    Result: passed.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed with CRLF warnings only.

    rg -n "m_draw_list|m_render_view|DrawList m_|RenderView m_|Scene\([^\)]*DrawList|draw_list\(\)" cpp/include/ofg/game cpp/src/game cpp/include/ofg/scene cpp/src/render cpp/tests docs/API_CONTRACTS.md docs/SYSTEMS.md
    Result: only renderer-private `m_draw_list`, `Scene::m_main_view`, and renderer implementation draw-list construction remain.

    rg -n "std::string& error" cpp/include/ofg/render cpp/src/render cpp/include/ofg/scene cpp/include/ofg/resources cpp/src/resources
    Result: no matches.

Milestone 6 validation, run from `C:\dev\ofg` on 2026-06-28:

    npm run format:cpp
    Result: passed.

    npm run test:cpp
    Result: passed. CTest ran `ofg_cpp_tests` with 100% tests passed. A first run after adding depth-helper precondition tests emitted local `[[nodiscard]]` warnings through doctest macros; wrapping those throwing checks in lambdas removed the warnings, and the command was rerun successfully.

    npm test
    Result: passed after the final depth-helper hardening fix. CTest passed and Mocha reported 19 passing TypeScript tests.

    npm run coverage
    Result: passed after the final depth-helper hardening fix. `cpp\src\gpu\common.cpp` reported 96.00% line coverage, all checked C++ files met the 90% gate, and the TypeScript coverage gate passed with `src/app/wasmRuntime.ts` at 94.75% line coverage.

    npm run smoke
    Result: passed after the final depth-helper hardening fix. Browser screenshot/report: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\browser-smoke\report.json`. Native PNG/report: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`.

    npm run smoke:browser:cpp
    Result: passed earlier in Milestone 6. Screenshot/report: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.

    npm run build:cloudflare
    Result: passed after the final depth-helper hardening fix. Packaged `.deploy`; generated WASM size was 288,181 bytes (281.4 KiB).

    npm run dev
    Result: running for human review. Primary URL: `http://127.0.0.1:5173`. Logs: `C:\dev\ofg\artifacts\dev-server.log`.

    npm run format:cpp:check
    Result: passed.

    rg -n "webgpu_common|ofg/render/webgpu_common|src/render/webgpu_common" cpp docs/API_CONTRACTS.md docs/SYSTEMS.md tools README.md AGENTS.md COVERAGE.md docs/coverage
    Result: no matches in active source/docs.

    rg -n "std::string& error" cpp/include/ofg/render cpp/src/render cpp/include/ofg/resources cpp/src/resources
    Result: no matches.

    rg -n "std::string& error" cpp/include cpp/src
    Result: remaining matches are in game-runtime, render-target, math, browser parsing, and boundary/helper compatibility code, not public render/resource APIs.

    git -c safe.directory=C:/dev/ofg diff --check
    Result: passed with CRLF warnings only.

## Interfaces and Dependencies

Final `Game` interface sketch:

    enum class GameLifecycleState {
      Uninitialized,
      Initialized,
      Preparing,
      Ready,
      Releasing,
      Released,
      Failed,
    };

    class Game {
     public:
      static void init(GpuContext gpu, WGPUTextureFormat color_format);
      static bool prepare();
      static void resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
      static void update(double time_ms);
      static void render(WGPUCommandEncoder encoder, RenderTarget target);
      static bool release();
      static void destroy() noexcept;
      static GameLifecycleState state() noexcept;
      static const std::string& last_error() noexcept;
      static void record_error(std::string message) noexcept;
      static std::string debug_status_json();
      static const RuntimeDebugStatus& status();

     private:
      bool prepare_impl();
      bool release_impl();
      void resize_impl(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
      void update_impl(double time_ms);
      void render_impl(WGPUCommandEncoder encoder, RenderTarget target);

      static std::unique_ptr<Game> s_game;
    };

Final `Resources` interface sketch:

    enum class ResourcesLifecycleState {
      Uninitialized,
      Initialized,
      Preparing,
      Ready,
      Releasing,
      Released,
      Failed,
    };

    class Resources {
     public:
      static void init(GpuContext gpu);
      static bool prepare();
      static bool release();
      static void destroy() noexcept;

      static Texture& create_texture(std::string label);
      static Shader& create_shader(std::string label);
      static Material& create_material(std::string label);
      static Mesh& create_mesh(std::string label);

      static std::span<const std::unique_ptr<Texture>> textures() noexcept;
      static std::span<const std::unique_ptr<Shader>> shaders() noexcept;
      static std::span<const std::unique_ptr<Material>> materials() noexcept;
      static std::span<const std::unique_ptr<Mesh>> meshes() noexcept;

     private:
      bool prepare_impl();
      bool release_impl();
      GpuContext m_gpu;
      static std::unique_ptr<Resources> s_resources;
    };

Final resource initialization sketch:

    class Texture {
     public:
      Texture(GpuContext gpu, std::string label);
      void init_from_rgba8_pixels(
          std::uint32_t width,
          std::uint32_t height,
          TextureColorSpace color_space,
          std::vector<std::byte> pixels,
          MipMapPolicy mip_map_policy);
      void update_pixels(std::vector<std::byte> pixels);
    };

    class Shader {
     public:
      Shader(GpuContext gpu, std::string label);
      void init_from_wgsl(
          std::string wgsl_source,
          ShaderParameterLayout parameter_layout,
          std::vector<PipelineDefinition> pipelines);
      void replace_source(std::string wgsl_source);
    };

`Resources::create_*` constructs each resource with the active `Resources` `GpuContext` or records enough owner context for later `prepare()`. Callers do not pass a device to every resource initialization method, and resource code does not read a global device directly.

Minimal scene sketch:

    struct RenderObject {
      Mesh* mesh = nullptr;
      math::Mat4 model = math::mat4_identity();
      PropertyBag properties;
      std::vector<MaterialOverride> material_overrides;
      math::Vec3 sort_origin;
    };

    class Scene {
     public:
      Scene() = default;
      const RenderView& main_view() const noexcept;
      void set_main_view(RenderView main_view) noexcept;
      void add_render_object(RenderObject object);
      void clear() noexcept;
      std::span<const RenderObject> render_objects() const noexcept;
      std::size_t size() const noexcept;
    };

`Renderer` converts `Scene::render_objects()` into its private transient `DrawList` until a later scene-query plan introduces pass filtering or richer world-scene ownership.

Final `Renderer` interface sketch:

    enum class RendererLifecycleState {
      Uninitialized,
      Initialized,
      Preparing,
      Ready,
      Releasing,
      Released,
      Failed,
    };

    class Renderer {
     public:
      static void init(GpuContext gpu, WGPUTextureFormat color_format);
      static bool prepare();
      static void resize(std::uint32_t width, std::uint32_t height);
      static void render(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene);
      static bool release();
      static void destroy() noexcept;
      static RendererCounters counters() noexcept;

     private:
      bool prepare_impl();
      bool release_impl();
      void render_impl(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene);

      static std::unique_ptr<Renderer> s_renderer;
      std::vector<std::unique_ptr<OpaquePass>> m_passes; // Concrete until a second pass justifies an interface.
    };

Final boundary rule:

    try {
      Game::update(time_ms);
      Game::render(encoder, target);
    } catch (const std::exception& error) {
      Game::record_error(error.what());
      // Browser/native boundary returns a recoverable failure or writes a smoke report.
    }

No exception should cross from C++ into TypeScript as the ordinary diagnostic path.
