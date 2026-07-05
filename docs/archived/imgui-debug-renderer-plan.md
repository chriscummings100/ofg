# Integrate Dear ImGui as the C++ Debug Renderer

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If `C:\dev\ofg\PLANS.md` is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

OFG needs an in-engine debug UI rendered by C++ on top of the WebGPU scene. After this change, running the browser app or native render smoke should show a small Dear ImGui based "OFG Debug" overlay drawn by the C++ renderer after the scene has been tone-mapped into the platform render target. The first useful tool in that overlay should be a quick-and-dirty debug variable menu: C++ code can declare global bool, int, and float values such as `DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)`, `DEBUG_INT("render/shadows/cascade_index", g_shadow_debug_cascade_index, 0)`, or `DEBUG_FLOAT("render/exposure/debug_scale", g_debug_exposure_scale, 1.0f)`, read them from anywhere through implicit scalar conversion, and let the global `DebugMenu` registry expose them to ImGui. Browser/TypeScript scalar editing is useful future work but is not in scope for this implementation.

The important architectural outcome is that Dear ImGui is integrated as a renderer-layer debug facility, not as a TypeScript DOM overlay. TypeScript may collect and forward raw browser events, but C++ owns Dear ImGui context lifetime, debug-variable storage, menu structure, UI state, input interpretation, draw-data generation, WebGPU rendering, and debug capture decisions.

## Progress

- [x] (2026-07-04 12:59Z) Planning context gathered: read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\cpp\CMakeLists.txt`, renderer/game/browser frame code, runtime status code, and upstream Dear ImGui WebGPU backend notes.
- [x] (2026-07-04 12:59Z) Confirmed this is still planning phase only. No implementation files, vendored source, CMake build graph, tests, or runtime behavior have been changed by this plan.
- [x] (2026-07-04 13:20Z) Added user requirement for a simple `DebugMenu` / `DEBUG_BOOL` registry as the first ImGui-powered debug tool, with the existing shadow-map debug overlay as the pilot toggle.
- [x] (2026-07-04 15:15Z) Expanded the first debug variable registry to include `DebugInt` / `DEBUG_INT` and `DebugFloat` / `DEBUG_FLOAT` alongside bools.
- [x] (2026-07-04 15:45Z) Ran the `review-plan` skill with correctness, completeness, clarity, efficiency, and performance reviewers; folded the accepted feedback into this plan.
- [x] (2026-07-04 16:38Z) Milestone 1 complete: vendored Dear ImGui `v1.92.8` / `8936b58fe26e8c3da834b8f60b06511d537b4c63` under `C:\dev\ofg\cpp\third_party\imgui`, added the build-only `ofg_imgui` CMake target, and validated `npm run build:wasm`, `npm run test:cpp`, and `npm run format:cpp:check`.
- [x] (2026-07-04 16:38Z) Milestone 1 review complete: ran the repo-local `milestone-review` process locally against the vendoring/CMake diff. No required findings, no follow-ups, and no rejected findings. Sub-agents were available but not used because delegation was not explicitly requested for this milestone review.
- [x] (2026-07-04 16:59Z) Milestone 2 complete: added GPU-free `DebugMenu`, `DebugBool`, `DebugInt`, `DebugFloat`, `DEBUG_BOOL`, `DEBUG_INT`, `DEBUG_FLOAT`, typed C++ get/set APIs, registration-time path parsing/diagnostics, unregister-on-destruction for scoped scalars, and a cached grouped/sorted menu tree rebuilt only when the registry generation changes.
- [x] (2026-07-04 16:59Z) Milestone 2 validation complete: ran `npm run format:cpp`, `npm run test:cpp`, `npm run coverage:cpp`, extra `npm run build:wasm`, `npm run format:cpp:check`, and `git -c safe.directory=C:/dev/ofg diff --check -- ...`. Coverage now checks `cpp\src\debug`; `debug_menu.cpp` reported 97.92% line coverage and `debug_scalars.cpp` reported 92.31%.
- [x] (2026-07-04 16:59Z) Milestone 2 review complete: ran the repo-local `milestone-review` process locally. Required finding fixed: the C++ coverage gate initially omitted `cpp\src\debug`, so `tools\cpp-coverage.mjs` was updated, tests were expanded, and coverage was rerun successfully. No follow-ups and no rejected findings.
- [x] (2026-07-04 18:11Z) Milestone 3 complete: added renderer-owned `DebugUi`, created/destroyed the Dear ImGui context and WebGPU backend with the renderer lifecycle, rendered the cached `DebugMenu` tree after tone mapping and the existing shadow preview, exposed `debugUi` runtime diagnostics through C++ JSON and TypeScript parsing, and taught browser/native smoke to assert the visible ImGui overlay separately from scene pixels.
- [x] (2026-07-04 18:11Z) Milestone 3 validation complete: ran `npm run test:cpp`, `npm run test:ts`, `npm run format:cpp:check`, `npm run smoke:browser:cpp`, `npm run smoke:render`, and `npm run smoke:browser`. Browser screenshot artifact: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`; full app browser artifact directory: `C:\dev\ofg\artifacts\browser-smoke`; native render artifact: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`; native report: `C:\dev\ofg\artifacts\render-smoke\report.json`.
- [x] (2026-07-04 18:24Z) Milestone 3 review complete: ran the repo-local `milestone-review` process locally with contract, code-quality, legacy, correctness, and validation passes. Required finding fixed: `docs\API_CONTRACTS.md` was stale after adding renderer-owned `DebugUi` and browser-facing `debugUi` status, so OFG-BOOT-002/003/004/006 were updated and `npm run smoke:browser` plus `git -c safe.directory=C:/dev/ofg diff --check` were rerun successfully. No follow-ups and no rejected findings. Sub-agents were not used because delegated review was not explicitly requested.
- [x] (2026-07-04 19:07Z) Milestone 4 complete: added raw `DebugUiInput`, forwarded it through `BrowserGame`/Embind/`wasmRuntime.ts`, fed focus/mouse/buttons/wheel/DOM key codes/text/F1 visibility edges into ImGuiIO, blocked browser pointer lock while the debug UI is visible or capturing mouse, and masked gameplay controls before `Scene::update` when ImGui reports capture.
- [x] (2026-07-04 19:07Z) Milestone 4 validation complete: ran `npm run test:ts`, `npm run test:cpp`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run format:cpp:check`, and `git -c safe.directory=C:/dev/ofg diff --check`. Full browser smoke now probes F1 hide/show and verifies `debugUi.wantsCaptureMouse` when the mouse is over the ImGui window.
- [x] (2026-07-04 19:22Z) Milestone 4 review complete: ran the repo-local `milestone-review` process locally with contract, code-quality, legacy, correctness, and validation passes. Required findings fixed: none. Follow-up recorded: `cpp\src\web\browser_game.cpp` is over 1000 lines and `src\app\wasmRuntime.ts` is under 1000 but above the 600-line split-pressure threshold after the raw debug-input facade additions, so the browser facade should be split before further significant growth. Rejected findings: none. Validation rerun: `git -c safe.directory=C:/dev/ofg diff --check` passed after the final Milestone 4 documentation update. Sub-agents were not used because delegated review was not explicitly requested.
- [x] (2026-07-04 19:48Z) Milestone 5 complete: converted the shadow-map cascade preview to `DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)`, made `Renderer::render_impl` read that global debug variable, removed the one-off `ControlInput`/`Game`/`Renderer` toggle path, removed the TypeScript/WASM `toggleShadowDebugOverlay` field, and strengthened smoke to require at least two registered debug menu entries.
- [x] (2026-07-04 19:48Z) Milestone 5 validation complete: ran `npm run format:cpp`, `npm run test:cpp`, `npm run test:ts`, `npm run smoke:browser:cpp`, and `npm run smoke:render`. `npm run test:ts` rebuilt WASM through `npm run build`; browser C++ smoke artifact: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`; native smoke artifact: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`.
- [x] (2026-07-04 20:00Z) Milestone 5 review complete: ran the repo-local `milestone-review` process locally with contract, code-quality, legacy, correctness, and validation passes. Required findings fixed: none. Follow-up recorded: `cpp\src\native\render_smoke.cpp` is now over 1000 lines and should be split before further significant native-smoke growth. Rejected findings: none. Validation rerun: `git -c safe.directory=C:/dev/ofg diff --check` passed, old shadow-toggle symbols were absent from non-vendored OFG source, and browser/native smoke reports both showed `debugUi.menuTreeGeneration` equal to 2. Sub-agents were not used because delegated review was not explicitly requested.
- [x] (2026-07-04 20:28Z) Milestone 6 complete: updated `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\COVERAGE.md`, `C:\dev\ofg\docs\coverage\latest.md`, committed coverage summary copies, smoke assertions, and final screenshots. Local review server is running at `http://127.0.0.1:5173`; final screenshot copies are under `C:\dev\ofg\artifacts\debug-ui`.
- [x] (2026-07-04 20:28Z) Final validation complete: ran `npm run format:cpp:check`, `npm test`, `npm run smoke`, `npm run coverage`, and `git -c safe.directory=C:/dev/ofg diff --check`. Coverage passed with `debug_menu.cpp` at 97.92%, `debug_scalars.cpp` at 92.31%, renderer files above the configured gate after existing defensive-line exclusions, TypeScript checked files above 90%, and a documented `cpp\src\debug\debug_ui.cpp` smoke-tested exception.
- [x] (2026-07-04 20:36Z) Milestone 6 review complete: ran the repo-local `milestone-review` process locally with contract, code-quality, legacy, correctness, and validation passes. Required findings fixed: none. Follow-ups already recorded: split `cpp\src\web\browser_game.cpp`, `src\app\wasmRuntime.ts`, and `cpp\src\native\render_smoke.cpp` before further significant growth. Rejected findings: none. Validation rerun: no command rerun needed after the review note because only this ExecPlan line changed; immediately before it, `git -c safe.directory=C:/dev/ofg diff --check` passed, final `npm run coverage` passed, and final smoke reports showed `debugUi.menuTreeGeneration` equal to 2 with the browser capture probe succeeding. Sub-agents were not used because delegated review was not explicitly requested.

## Surprises & Discoveries

- Observation: `Renderer::render_impl` already has a clean final-target boundary. It renders scene color and depth, runs bloom, then calls `ToneMapPass::render(...)` into the platform `RenderTarget`.
  Evidence: `C:\dev\ofg\cpp\src\render\renderer.cpp` calls `m_tone_map_pass->render(...)` near the end of `Renderer::render_impl`.

- Observation: `Game` is a lifecycle facade and should not become the owner of ImGui behavior.
  Evidence: `C:\dev\ofg\docs\GUIDES.md` says facade and lifecycle files such as `game.cpp` should stay thin; feature-specific behavior belongs in owning subsystems.

- Observation: Upstream Dear ImGui provides a WebGPU renderer backend that supports Emscripten with the Dawn WebGPU port used by OFG.
  Evidence: upstream `backends/imgui_impl_wgpu.h` states Emscripten defaults to the Dawn backend and requires `--use-port=emdawnwebgpu` with Emscripten 4.0.10 or newer; OFG already compiles browser C++ with `--use-port=emdawnwebgpu`.

- Observation: OFG currently has raw game-control input, not the richer mouse/key/text event stream that an interactive Dear ImGui platform backend needs.
  Evidence: `C:\dev\ofg\cpp\include\ofg\core\control_input.hpp` only carries movement axes, look deltas, fast/slow, and camera-cycle edge.

- Observation: The active renderer tree already has a dedicated shadow-map debug overlay pass and a special-case control-input toggle for it.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\shadow_debug_pass.hpp` defines `ShadowDebugPass`; `C:\dev\ofg\cpp\src\render\renderer.cpp` has `Renderer::set_shadow_debug_overlay_enabled(...)`; `C:\dev\ofg\cpp\src\game\game.cpp` toggles that renderer flag from `ControlInput::m_toggle_shadow_debug_overlay`.

- Observation: The milestone-review skill references `C:\dev\ofg\docs\ARCHITECTURE.md`, but this repository does not currently have that file.
  Evidence: `Get-Content -Raw docs\ARCHITECTURE.md` failed with "Cannot find path"; the active review used `C:\dev\ofg\AGENTS.md`, `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, and this ExecPlan instead.

- Observation: Adding a new portable source directory requires updating the C++ coverage wrapper's checked-file filter, otherwise the coverage command can pass without printing the new implementation file.
  Evidence: the first Milestone 2 coverage run passed but did not list `C:\dev\ofg\cpp\src\debug\debug_menu.cpp`; after adding `cpp\src\debug` to `C:\dev\ofg\tools\cpp-coverage.mjs`, coverage reported `debug_menu.cpp` at 97.92% and `debug_scalars.cpp` at 92.31%.

- Observation: A visible ImGui overlay changes screenshot pixel histograms enough that scene-only coverage checks should not count the debug UI as world pixels.
  Evidence: the first `npm run smoke:browser:cpp` attempt failed with background coverage at 0.15440031152647976 while the screenshot clearly showed the ImGui panel rendered over the scene. After compacting the window and excluding the left debug-UI column from scene sampling, `npm run smoke:browser:cpp` and `npm run smoke:render` both passed while `debugUi` status assertions verified the overlay directly.

- Observation: The raw debug-input bridge grew the existing browser facade files into split-pressure territory.
  Evidence: during the Milestone 4 review, `C:\dev\ofg\cpp\src\web\browser_game.cpp` measured 1092 lines and `C:\dev\ofg\src\app\wasmRuntime.ts` measured 883 lines. The review recorded this as a follow-up rather than splitting them inside the input milestone.

- Observation: Native render smoke has crossed the critical large-file threshold after adding debug UI and debug-menu assertions.
  Evidence: during the Milestone 5 review, `C:\dev\ofg\cpp\src\native\render_smoke.cpp` measured 1011 lines. The current milestone kept the validation local, but future native-smoke work should split report writing and diagnostics helpers before adding more behavior.

- Observation: `DebugUi` cannot reasonably meet native line coverage through doctest alone because its useful behavior requires an ImGui context, WebGPU backend, command encoder, and render target.
  Evidence: the first final `npm run coverage` run failed with `C:\dev\ofg\cpp\src\debug\debug_ui.cpp` at 46.57% line coverage even though `npm run smoke:browser`, `npm run smoke:browser:cpp`, and `npm run smoke:render` validate the overlay path. `C:\dev\ofg\tools\cpp-coverage.mjs` and `C:\dev\ofg\COVERAGE.md` now exclude only `debug_ui.cpp`; `debug_menu.cpp` and `debug_scalars.cpp` remain checked by the native coverage gate.

## Decision Log

- Decision: Use upstream Dear ImGui core plus upstream `imgui_impl_wgpu` as the renderer backend, and write an OFG-specific platform/input bridge rather than using GLFW or SDL.
  Rationale: OFG has no engine dependency and already owns browser setup through TypeScript plus WebGPU handles through C++. Pulling in GLFW/SDL would add a window/input framework that does not match the browser host. The upstream WebGPU renderer backend directly matches OFG's WebGPU layer.
  Date/Author: 2026-07-04 / Codex

- Decision: Integrate the overlay as a `Renderer` subsystem, with only thin orchestration/status plumbing in `Game`.
  Rationale: `docs/API_CONTRACTS.md` says C++ owns renderer internals and browser/native frame drivers own acquisition/submission. The overlay should be another renderer pass, not DOM UI or gameplay state.
  Date/Author: 2026-07-04 / Codex

- Decision: Implement the GPU-free `DebugMenu` registry before the first visible ImGui overlay, then render that real registry non-interactively before adding input capture.
  Rationale: The first overlay should exercise the actual debug-variable menu rather than a throwaway panel, while still de-risking WebGPU backend setup before browser input policy changes.
  Date/Author: 2026-07-04 / Codex

- Decision: Keep Dear ImGui visible by default with a compact debug menu panel during initial integration.
  Rationale: The feature must be observable in screenshots and smoke artifacts. A small menu backed by real registered variables is easier to validate than a hidden debug renderer that only proves itself through code.
  Date/Author: 2026-07-04 / Codex

- Decision: Add a C++ `DebugMenu` singleton plus `DebugBool`, `DebugInt`, and `DebugFloat` variable wrappers before building fancier editors.
  Rationale: A registry of named scalar values gives the ImGui integration an immediate use and lets renderer systems depend on ordinary C++ globals instead of adding one-off runtime status or input fields for every debug toggle or tuning value.
  Date/Author: 2026-07-04 / Codex

- Decision: Provide `DEBUG_BOOL(path, variable, default_value)`, `DEBUG_INT(path, variable, default_value)`, and `DEBUG_FLOAT(path, variable, default_value)` as the first declaration macros, expanding to global scalar wrapper objects with implicit scalar conversion and assignment from the matching type.
  Rationale: The user-facing call site should stay cheap enough to drop near renderer, gameplay, resource, or system code without wiring boilerplate. The wrappers keep registration and storage behavior consistent across the scalar types OFG already knows it will need.
  Date/Author: 2026-07-04 / Codex

- Decision: Use slash-separated paths as the public debug-menu hierarchy and as the stable browser facade keys.
  Rationale: One path can drive C++ lookup, TypeScript lookup, and ImGui submenu construction. For example, `render/shadows/show_debug_overlay` should render as `render` -> `shadows` -> `show_debug_overlay`.
  Date/Author: 2026-07-04 / Codex

- Decision: Validate and split debug variable paths during registration, then rebuild the cached sorted menu tree only when the registry generation changes.
  Rationale: Debug UI rendering should not repeatedly split strings, sort paths, or rebuild hierarchy every frame. Registration is rare, but it may happen from different translation units or lazy subsystems at different times. Each registration should store parsed path segments and mark the registry dirty; a cheap per-frame `prepare_frame()` / `refresh_tree_if_dirty()` step can then regroup entries such as `render/foo` and `render/bar` under the same `render` menu before ImGui rendering.
  Date/Author: 2026-07-04 / Codex

- Decision: Keep `DebugMenu` and scalar wrappers independent from Dear ImGui; put ImGui rendering in `DebugUi` or a separate `debug_menu_imgui.*` helper.
  Rationale: The registry needs to work in tests and runtime code without a GPU, an ImGui context, or the WebGPU backend. ImGui is one editor frontend for the registry, not part of the registry itself.
  Date/Author: 2026-07-04 / Codex

- Decision: Defer browser/TypeScript scalar editing to a follow-up plan.
  Rationale: The first implementation should prove C++ globals, the registry, ImGui editing, input capture, and the shadow overlay conversion. Browser scalar editing is useful but would add API and JSON/Embind scope before the core path is proven.
  Date/Author: 2026-07-04 / Codex

- Decision: Treat `DEBUG_*` declarations as `.cpp`-only by default.
  Rationale: Namespace-scope macros in headers create ODR and static initialization traps. Cross-translation-unit access should use explicit `extern DebugBool`, `extern DebugInt`, or `extern DebugFloat` declarations in a header and exactly one macro definition in a `.cpp`.
  Date/Author: 2026-07-04 / Codex

- Decision: Keep debug scalar wrapper method definitions in `debug_scalars.cpp` and registry/tree behavior in `debug_menu.cpp`.
  Rationale: The split keeps each new OFG-owned source file below the repo's large-file concern threshold while preserving one public header for the small DebugMenu API.
  Date/Author: 2026-07-04 / Codex

- Decision: Keep ImGui backend GPU allocations out of existing `RendererCounters` and expose them through dedicated `DebugUiStatus` fields instead.
  Rationale: Existing renderer counters have exact smoke-test meaning for OFG-owned durable pass resources. Dear ImGui's upstream WebGPU backend owns its internal buffers, font texture, sampler, bind groups, shader modules, and pipeline, so treating them as separate debug UI diagnostics avoids silently changing the semantics of current renderer resource counters.
  Date/Author: 2026-07-04 / Codex

- Decision: Browser and native scene-pixel smoke ignore the left debug-UI column and validate ImGui through `debugUi` status instead.
  Rationale: The overlay is intentionally visible by default and may occlude arbitrary scene pixels. Pixel smoke should continue proving the world render on unobscured pixels, while `debugUi.visible`, overlay pass count, draw-list counts, upload byte counts, buffer capacity, and font texture diagnostics prove the overlay path.
  Date/Author: 2026-07-04 / Codex

- Decision: Make the current shadow-map overlay the first debug variable, replacing the special-case renderer toggle path once the debug menu exists.
  Rationale: It is already a renderer-only debugging aid with a clear boolean state. Moving it into `DebugMenu` proves the system on a real feature and avoids growing a second set of debug controls.
  Date/Author: 2026-07-04 / Codex

- Decision: Defer splitting the browser facade files until a focused facade-refactor milestone, but stop treating further growth in `browser_game.cpp` / `wasmRuntime.ts` as routine.
  Rationale: Milestone 4 changed the C++/TypeScript boundary in one coherent slice and validated that shape. Splitting the files now would mix a larger ownership refactor into the raw-input/capture milestone, but the files are large enough that future facade work should extract debug input/status parsing helpers before adding more runtime surface.
  Date/Author: 2026-07-04 / Codex

- Decision: Remove the old shadow-overlay `KeyM` control path instead of keeping a keyboard shortcut for the first debug variable.
  Rationale: Browser/TypeScript scalar editing is intentionally deferred, and keeping `KeyM` as a hidden special case would leave two ways to control the same renderer debug aid. The shadow preview is now controlled through the ImGui-rendered `DebugMenu` checkbox at `render/shadows/show_debug_overlay`.
  Date/Author: 2026-07-04 / Codex

## Outcomes & Retrospective

Dear ImGui is now integrated as a renderer-owned C++ debug overlay. The browser app, focused browser C++ fixture, and native render smoke all show the "OFG Debug" panel rendered into the WebGPU final target after tone mapping. The overlay renders the real `DebugMenu` tree, and smoke diagnostics prove the menu has two registrations after the shadow overlay conversion: `debug/ui/show_metrics` and `render/shadows/show_debug_overlay`.

The debug-variable registry supports global `DEBUG_BOOL`, `DEBUG_INT`, and `DEBUG_FLOAT` declarations with typed C++ get/set APIs, implicit scalar reads, registration-time path parsing, cached grouped/sorted menu trees, late-registration regrouping, and diagnostics for invalid or duplicate paths. The existing shadow-map cascade preview is now controlled by `DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)` instead of the old one-off control-input and renderer-setter path.

Interactive input is browser-collected but C++ interpreted. TypeScript forwards raw `DebugUiInput` snapshots, blocks pointer lock while the debug UI is visible or capturing mouse, and C++ feeds ImGuiIO plus masks gameplay controls when the previous ImGui frame wanted mouse or keyboard capture. Full browser smoke verifies F1 hide/show, mouse capture over the panel, and release of capture away from the panel.

Final screenshot artifacts:

- `C:\dev\ofg\artifacts\debug-ui\final-browser-overlay.png`
- `C:\dev\ofg\artifacts\debug-ui\final-cpp-fixture-overlay.png`
- `C:\dev\ofg\artifacts\debug-ui\final-native-overlay.png`

Remaining gaps are deliberate follow-ups: browser/TypeScript scalar listing and editing APIs are deferred, Dear ImGui docking/multi-viewport/custom fonts are not enabled, and large browser/native facade files need focused splits before further significant growth.

## Contract and Quality Baseline

This plan preserves and extends the following contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`:

OFG-BOOT-001 TypeScript Host Ownership: preserve. TypeScript may continue to own DOM boot, canvas lookup, raw browser event collection, and WASM method calls. TypeScript must not own Dear ImGui widgets, debug panel state, debug-variable storage, renderer draw data, GPU resources, or debug-renderer settings. Browser-side scalar editing is explicitly deferred to a follow-up plan.

OFG-BOOT-002 C++ Runtime Ownership: extend. C++ will own Dear ImGui context lifetime, the OFG debug UI model, `DebugMenu`, `DebugBool` / `DebugInt` / `DebugFloat` storage, debug-variable registration, ImGui draw-data generation, the WebGPU ImGui renderer backend, and whether gameplay controls should be ignored because ImGui wants mouse or keyboard capture.

OFG-BOOT-003 WASM Facade: extend narrowly. The browser facade may gain methods for raw debug input snapshots/events and status fields for debug UI visibility/capture. It must not expose GPU handles, renderer internals, ImGui pointers, raw debug-variable pointers, or mutable scene/resource objects to TypeScript. Stable browser debug-menu scalar queries/mutations such as `get_debug_bool(path)` and `set_debug_float(path, value)` are future work, not part of this implementation.

OFG-BOOT-004 Renderer Compatibility: extend. Browser and native smoke must continue to validate equivalent scene rendering, and must also either validate the visible C++ debug overlay or explicitly render with it disabled in tests that are only about scene pixels. This plan prefers validating the visible overlay.

OFG-BOOT-005 WebGPU Baseline: preserve. Dear ImGui must not require optional WebGPU features or manually request limits above adapter defaults. It may create ordinary buffers, texture views, bind groups, samplers, shader modules, and render pipelines needed by the upstream WebGPU backend.

OFG-BOOT-006 Resource Lifetime: extend. Dear ImGui GPU resources must be durable across steady-state frames and released during renderer release. Ordinary frames may update ImGui vertex/index buffers and font texture data as expected by ImGui, but must not recreate the context or pipeline every frame. `DebugMenu` and global debug scalar values are process/runtime debug state, not GPU resources, and must remain usable without an active WebGPU device.

OFG-BOOT-007 Generated Artifacts: preserve. Vendored source is source-controlled under `C:\dev\ofg\cpp\third_party\imgui`; generated screenshots, reports, builds, and coverage stay under ignored artifact directories.

OFG-BOOT-009 Coverage: preserve. New OFG-owned implementation files should meet the default coverage attention gate. Third-party Dear ImGui source should be excluded from OFG per-file coverage attention by target structure or coverage tooling, with the rationale recorded here if tooling needs an explicit exception.

## Context and Orientation

Dear ImGui is an immediate-mode C++ GUI library. "Immediate-mode" means the application calls UI functions each frame to describe the current controls and windows; Dear ImGui stores the necessary interaction state internally and emits draw lists. Dear ImGui itself does not know how to talk to OFG's browser window or WebGPU device without two backend pieces:

The platform/input backend feeds `ImGuiIO` with display size, delta time, mouse state, keyboard state, text input, wheel input, and optional cursor/clipboard behavior.

The renderer backend consumes `ImDrawData` and records GPU commands that draw textured triangles. Upstream Dear ImGui provides `imgui_impl_wgpu.cpp` for WebGPU.

Current OFG render flow:

`C:\dev\ofg\cpp\src\web\browser_game.cpp` owns browser WebGPU surface acquisition and queue submission. Each frame it creates a command encoder and calls `Game::render(...)` with a `RenderTarget`.

`C:\dev\ofg\cpp\src\native\render_smoke.cpp` owns native Dawn device creation, offscreen render-target creation, readback, PNG writing, and smoke report writing. It also calls `Game::render(...)`.

`C:\dev\ofg\cpp\src\game\game.cpp` owns runtime lifecycle orchestration, scene update, debug-status aggregation, and calls `Renderer::render(...)`.

`C:\dev\ofg\cpp\src\render\renderer.cpp` owns the C++ render sequence. It extracts render objects, culls, renders shadows, renders opaque geometry and sky into an HDR scene-color target, runs bloom through `TempBuffer`, and tone maps into the platform `RenderTarget`.

The ImGui overlay should sit after tone mapping by opening a final WebGPU render pass on `RenderTarget::m_view` with `loadOp = Load`, then calling `ImGui_ImplWGPU_RenderDrawData(...)`. This preserves all existing scene rendering and draws the debug UI in final display space.

The intended per-frame Dear ImGui sequence is:

    if debug_ui_visible and target size is nonzero:
        ImGui_ImplWGPU_NewFrame()
        ImGui::NewFrame()
        DebugMenu::instance().refresh_tree_if_dirty()
        render_debug_menu_imgui(DebugMenu::instance().tree())
        ImGui::Render()
        begin final-target load/store render pass
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass)
        end final-target render pass

The intended teardown order is: stop rendering the debug UI, call `ImGui_ImplWGPU_Shutdown()`, destroy the ImGui context owned by `DebugUi`, release any OFG-owned debug UI wrapper resources, then release renderer passes and borrowed WebGPU handles in their existing order.

The first debug-menu variable family is scalar bool/int/float values. `DebugBool`, `DebugInt`, and `DebugFloat` are OFG-owned wrappers around a scalar value with a stable slash-separated path. They should be safe to declare at namespace scope through:

    DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)
    DEBUG_INT("render/shadows/cascade_index", g_shadow_debug_cascade_index, 0)
    DEBUG_FLOAT("render/exposure/debug_scale", g_debug_exposure_scale, 1.0f)

Each macro should create a global variable named by the caller. The object should be readable from any C++ code with natural scalar syntax such as `if (g_show_shadow_debug_overlay) { ... }`, `int cascade = g_shadow_debug_cascade_index;`, or `float scale = g_debug_exposure_scale;`. It should be settable from C++ with either `.set(value)` or assignment if implementation keeps that ergonomic, and registered with `DebugMenu::instance()` during construction. `DebugMenu` should expose typed APIs such as `get_bool(path)`, `set_bool(path, value)`, `bool_entries()`, `get_int(path)`, `set_int(path, value)`, `int_entries()`, `get_float(path)`, `set_float(path, value)`, and `float_entries()` so ImGui and tests can use the same registry.

Static initialization order must be handled deliberately. The preferred approach is a function-local singleton that is safe for global debug scalar constructors to call, and either intentionally survives process shutdown or makes unregistering safe during static destruction. Global registration should not require WebGPU or Dear ImGui to be initialized.

Path rules should be simple and strict: paths are ASCII string keys, use `/` for hierarchy, must be nonempty, should not start or end with `/`, and should not contain empty segments. A path identifies one logical variable regardless of type, so `DEBUG_BOOL("x/y", ...)` and `DEBUG_FLOAT("x/y", ...)` collide. Duplicate paths are code bugs; because global constructors throwing during startup is risky, registration should record duplicate/invalid-path diagnostics in `DebugMenu` and tests should fail through explicit validation rather than crashing during static initialization.

Registration should parse each path into durable path segments once and increment a registry generation when a valid new entry is accepted. The menu renderer should not split paths or sort all entries every frame. Instead, `DebugMenu` should expose a cheap per-frame or pre-render step that checks whether the cached tree generation is stale; only stale trees are rebuilt, grouped, and sorted. This handles globals and later lazy registrations from different code paths while keeping ordinary debug UI frames cheap.

Registration failure behavior should be deterministic. Invalid paths create an inert wrapper that keeps its local default value but is not visible in `DebugMenu`; a diagnostic records the path and reason. Duplicate or cross-type duplicate paths keep the first registered variable live, make the later wrapper inert, and record a diagnostic with both the duplicate path and the attempted type. `get_*` returns `std::nullopt` for missing or wrong-type paths. `set_*` returns false for missing, wrong-type, invalid, or inert entries and true only when a live entry of the matching type changes or accepts the value.

The first ImGui editors should stay intentionally plain: `DebugBool` renders as a checkbox, `DebugInt` renders as an integer input or drag value, and `DebugFloat` renders as a float input or drag value. Ranges, steps, formatting strings, color pickers, enum dropdowns, persistence, and hotkey metadata are future editor features, not part of the first scalar registry.

## Plan of Work

Milestone 1 vendors and builds Dear ImGui without changing runtime behavior. Add `C:\dev\ofg\cpp\third_party\imgui` with Dear ImGui `v1.92.8` from `https://github.com/ocornut/imgui`, tag commit `8936b58fe26e8c3da834b8f60b06511d537b4c63`, source checked on 2026-07-04. `SOURCE.md` must record the repository URL, tag, commit, import date, copied files, excluded files, and local build notes. Include root files needed to compile core Dear ImGui and the WebGPU backend: `imgui.cpp`, `imgui.h`, `imgui_draw.cpp`, `imgui_internal.h`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imconfig.h`, `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h`, `LICENSE.txt`, `backends/imgui_impl_wgpu.cpp`, and `backends/imgui_impl_wgpu.h`. Do not copy `imgui_demo.cpp`, GLFW, SDL, examples, docking branch code, or optional FreeType code. Add a CMake target such as `ofg_imgui` with warnings suppressed, public include directories for the vendored root and `backends`, native linkage/include access to `webgpu_c`, Emscripten compile options including `--use-port=emdawnwebgpu`, and `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` where the WebGPU backend requires it. This milestone should pass `npm run build:wasm` and `npm run test:cpp` without creating or rendering any ImGui context.

Milestone 2 adds the GPU-free debug-variable system. Add `C:\dev\ofg\cpp\include\ofg\debug\debug_menu.hpp` and `C:\dev\ofg\cpp\src\debug\debug_menu.cpp` with a process-wide `DebugMenu` singleton, `DebugBool` / `DebugInt` / `DebugFloat` wrappers, and `DEBUG_BOOL(path, variable, default_value)`, `DEBUG_INT(path, variable, default_value)`, and `DEBUG_FLOAT(path, variable, default_value)` macros. Keep this subsystem independent from WebGPU and Dear ImGui so it can be unit tested without a GPU. Implement path validation, duplicate diagnostics, typed scalar lookup, typed scalar mutation, parsed path segment storage, registry generation tracking, dirty cached tree rebuild, and snapshot/listing APIs for tests. Use the macros in exactly one `.cpp`; expose `extern DebugBool`, `extern DebugInt`, or `extern DebugFloat` declarations from headers only when cross-translation-unit access is needed. Test-only reset helpers may clear test-owned registrations and diagnostics, but production globals should not depend on ordinary shutdown order.

Milestone 3 adds the non-interactive renderer overlay that renders the real registry. Add an OFG-owned subsystem under `C:\dev\ofg\cpp\include\ofg\debug\debug_ui.hpp` and `C:\dev\ofg\cpp\src\debug\debug_ui.cpp`, plus an ImGui-specific helper such as `debug_menu_imgui.hpp/.cpp` if that keeps the registry clean. `DebugUi` should own Dear ImGui context creation/destruction, call `ImGui_ImplWGPU_Init` with the active `WGPUDevice` and final target format, set display size and framebuffer scale, call `ImGui_ImplWGPU_NewFrame`, `ImGui::NewFrame`, `DebugMenu::refresh_tree_if_dirty()`, the debug-menu ImGui renderer, `ImGui::Render`, and `ImGui_ImplWGPU_RenderDrawData`. It should skip begin/render work when hidden or when the platform target has zero size. Add `std::unique_ptr<DebugUi>` to `Renderer`, create it in `Renderer::prepare_impl` after the tone-map pass, render it after tone mapping and after any shadow preview overlay so ImGui remains topmost, include debug UI counters in status, and reset it during `Renderer::release_impl` before the borrowed GPU context is cleared. `Game` should pass a narrow `RendererFrameInfo` / `DebugUiFrameInfo` into `Renderer::render` containing time, device pixel ratio, latest debug input, and a stable status snapshot; `Game` should not build widgets.

Milestone 4 adds interactive browser input and capture policy. Introduce an OFG-owned C++ `DebugUiInput` snapshot, separate from gameplay `ControlInput`. It must define coordinate space as canvas CSS pixels, framebuffer size as physical pixels, wheel units as browser `WheelEvent.deltaY/deltaX` normalized to line-like scalar values, key identity by stable DOM `KeyboardEvent.code`, text input as UTF-8 codepoints, per-frame reset rules for deltas/text/key edges, current focus state, pointer-lock state, mouse buttons, absolute mouse position, and whether the debug UI visibility toggle is pressed. Make F1 the initial visibility toggle unless implementation records a different key with rationale. Extend `BrowserGame`, Embind, `src/app/wasmRuntime.ts`, `src/app/controlInput.ts` or a new debug-input collector, and `src/app/main.ts` so raw debug input is forwarded before `Game::update`. When the debug UI is visible or wants mouse capture, TypeScript must avoid requesting pointer lock and should exit pointer lock if needed; C++ must mask gameplay controls before `Scene::update` when ImGui wants mouse or keyboard capture.

Milestone 5 converts the existing shadow-map overlay into the first real debug variable. Replace the special-case `ControlInput::m_toggle_shadow_debug_overlay`, `Game` toggle routing, and `Renderer::set_shadow_debug_overlay_enabled(...)` flow with a `DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)` declaration owned near the renderer/shadow debug code. `Renderer::render_impl` should read the debug variable directly when deciding whether to run `ShadowDebugPass`. If a keyboard hotkey remains, it must mutate `DebugMenu::set_bool("render/shadows/show_debug_overlay", ...)` rather than a parallel renderer setter. This milestone must explicitly update or remove current call sites in `C:\dev\ofg\cpp\include\ofg\core\control_input.hpp`, `C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp`, `C:\dev\ofg\cpp\src\web\browser_game.cpp`, `C:\dev\ofg\cpp\src\web\embind_module.cpp`, `C:\dev\ofg\cpp\src\game\game.cpp`, `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`, `C:\dev\ofg\cpp\src\render\renderer.cpp`, `C:\dev\ofg\src\app\controlInput.ts`, `C:\dev\ofg\src\app\wasmRuntime.ts`, `C:\dev\ofg\tests\ts\controlInput.test.ts`, `C:\dev\ofg\tests\ts\wasmRuntime.test.ts`, and existing C++ renderer/game/control tests.

Milestone 6 completes docs, smoke, screenshots, coverage, and final validation. Update `C:\dev\ofg\docs\API_CONTRACTS.md` with DebugMenu ownership, DebugUi ownership, browser raw-input forwarding, renderer overlay ordering, BrowserSmoke expectations, and NativeRenderSmoke expectations. Browser and native smoke should assert debug UI visibility, capture flags where applicable, overlay pass counters, menu tree generation/rebuild counters, bounded overlay-aware scene pixels, and steady-state resource behavior. Run final coverage and refresh committed coverage summaries under `C:\dev\ofg\docs\coverage` according to `C:\dev\ofg\COVERAGE.md`.

## Concrete Steps

All commands run from `C:\dev\ofg` unless noted.

Before implementation starts, re-read this file and check the dirty worktree:

    git status --short
    Get-Content -Raw docs\plans\imgui-debug-renderer-plan.md

Milestone 1 commands:

    npm run format:cpp:check
    npm run build:wasm
    npm run test:cpp

Expected result: CMake configures both native tests and wasm builds with Dear ImGui in the build graph. No app visual change is expected yet.

Milestone 2 commands:

    npm run format:cpp
    npm run test:cpp
    npm run coverage:cpp

Expected result: `DebugMenu`, `DebugBool`, `DebugInt`, and `DebugFloat` unit tests cover default values, scalar casting, typed get/set APIs, path hierarchy ordering, cached tree rebuild after late registration, no tree rebuild when the generation is unchanged, invalid path diagnostics, duplicate and cross-type path diagnostics, test isolation, and ImGui-independent registry behavior.

Milestone 3 commands:

    npm run format:cpp
    npm run test:cpp
    npm run build:wasm
    npm run smoke:render
    npm run smoke:browser:cpp

Expected result: browser and native render outputs show the same scene plus a compact C++ debug menu overlay rendered from the `DebugMenu` cached tree. Native render smoke writes a PNG and report under `C:\dev\ofg\artifacts`; browser smoke reports initialized C++ renderer status and debug UI diagnostics.

Milestone 4 commands:

    npm run format:cpp
    npm run test:cpp
    npm run test:ts
    npm run smoke:browser
    npm run smoke:browser:cpp

Expected result: when the debug UI is open, mouse/keyboard input can interact with ImGui controls and gameplay controls do not also consume captured input. When the debug UI is closed or not capturing, existing player/camera controls continue to work.

Milestone 5 commands:

    npm run format:cpp
    npm run test:cpp
    npm run test:ts
    npm run build:wasm
    npm run smoke:browser:cpp
    npm run smoke:render

Expected result: the shadow-map cascade preview can be toggled through the ImGui debug menu path `render/shadows/show_debug_overlay`. The old one-off shadow-overlay control path is removed; any retained hotkey mutates the debug variable.

Milestone 6 commands:

    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage

Expected result: C++ and TypeScript unit gates pass, browser/native smoke pass with overlay-specific assertions, changed OFG-owned implementation files do not appear in the default filtered coverage attention output unless this plan records a specific exception and rationale, and committed coverage summaries under `C:\dev\ofg\docs\coverage` are refreshed.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

The review should pay special attention to C++/TypeScript ownership drift, WebGPU lifetime ordering, native/browser parity, test coverage for new OFG-owned files, and whether third-party Dear ImGui files are isolated from OFG style and coverage requirements.

## Validation and Acceptance

Acceptance criteria:

The browser app displays a compact "OFG Debug" Dear ImGui overlay drawn into the WebGPU canvas by C++.

The overlay includes a debug menu generated from `DebugMenu` registrations, with slash-separated paths rendered as submenus or nested tree nodes.

`DebugMenu` parses paths at registration time and uses `refresh_tree_if_dirty()` to rebuild a cached sorted tree only when the registry generation changes. Tests prove late registrations from different owners still group under common parents such as `render`.

`DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false)` creates a global `DebugBool` value that can be implicitly read as bool by C++ renderer code, defaults to false, and is visible in `DebugMenu`.

`DEBUG_INT(...)` creates a global `DebugInt` value that can be implicitly read as int by C++ code, defaults to the supplied integer, and is visible in `DebugMenu`.

`DEBUG_FLOAT(...)` creates a global `DebugFloat` value that can be implicitly read as float by C++ code, defaults to the supplied float, and is visible in `DebugMenu`.

`DebugMenu::get_bool(...)`, `set_bool(...)`, `get_int(...)`, `set_int(...)`, `get_float(...)`, and `set_float(...)` can read and edit registered scalar values by path. Missing, invalid, duplicate, or wrong-type paths produce clear failure behavior covered by tests.

The shadow-map debug overlay is controlled by the debug variable path `render/shadows/show_debug_overlay`, proving the registry on a real renderer debug feature.

The native render smoke PNG displays the same overlay on top of the offscreen scene, or the plan records a deliberate disabled-overlay smoke mode with a separate browser screenshot proving visibility.

The final target overlay order is explicit: tone map first, shadow preview overlay second when enabled, ImGui debug UI last so menu text remains readable. The overlay does not replace, clear, resize, or otherwise corrupt the scene output.

Dear ImGui context and WebGPU renderer resources are created once per renderer lifetime, reused across ordinary frames, and released during renderer teardown.

`DebugUiStatus` reports overlay pass count, ImGui buffer capacity/resizes, font texture creation count, draw list count, draw command count, and uploaded bytes. Smoke validates a warmup/steady-state frame pair so pipeline/font resources are not recreated every frame.

TypeScript does not own widgets, debug panel state, ImGui draw data, renderer resources, or debug rendering decisions.

Interactive mode forwards raw browser input to C++, and C++ exposes capture state so gameplay controls do not conflict with ImGui controls.

Debug status JSON remains stable and tested. Any new debug UI visibility/capture/status fields are added to `C:\dev\ofg\cpp\include\ofg\runtime\runtime_debug_status.hpp`, `C:\dev\ofg\cpp\src\runtime\runtime_debug_status.cpp`, `C:\dev\ofg\src\app\wasmRuntime.ts`, C++ tests, and TypeScript tests together. Debug scalar enumeration or browser-side scalar editing is deferred and should not be added to `RuntimeDebugStatus` in this plan.

Validation commands:

    npm run test:cpp
    npm run test:ts
    npm run smoke:browser
    npm run smoke:browser:cpp
    npm run smoke:render
    npm run coverage

Screenshot cadence:

During Milestone 3 and later visual work, start or keep the dev server running with:

    npm run dev

Report the printed URL in chat. Capture screenshots after first visible overlay, after input capture works, after final panel layout, and before final acceptance. Store durable screenshots under `C:\dev\ofg\artifacts\debug-ui\` when they are useful for comparing progress. Present each screenshot path in chat for human review.

Coverage gate:

The plan is complete only when modified OFG-owned implementation files pass the default coverage attention gate. Third-party Dear ImGui files should be treated as vendored dependency source and not as files requiring OFG line coverage.

## Idempotence and Recovery

Vendoring should be additive and pinned. If the wrong Dear ImGui snapshot is imported, remove only `C:\dev\ofg\cpp\third_party\imgui` and the matching CMake target changes from the current milestone, then re-import the chosen snapshot.

Renderer integration should be behind a single `DebugUi` subsystem. If WebGPU validation fails, temporarily disable only the `DebugUi::render(...)` call while preserving build and lifecycle code, then debug pass descriptors and backend init info in isolation.

If browser input conflicts with gameplay pointer lock, keep the visible non-interactive overlay from Milestone 3 and back out only the input forwarding changes for Milestone 4. The overlay should remain shippable while input policy is corrected.

If native smoke pixel thresholds fail only because the overlay adds expected pixels, update `C:\dev\ofg\tools\smoke-contract.json` and smoke inspection logic with explicit overlay-aware thresholds rather than weakening scene validation broadly.

Never use `git reset --hard` or checkout commands to recover from this work. Use targeted patches that remove only the files and lines added for the current milestone.

## Artifacts and Notes

Upstream source notes gathered during planning:

Dear ImGui is pinned for this plan to `v1.92.8`, tag commit `8936b58fe26e8c3da834b8f60b06511d537b4c63`, from `https://github.com/ocornut/imgui`. GitHub lists `v1.92.8` as the latest release on 2026-05-12 when this plan was revised on 2026-07-04.

Milestone 1 validation:

    npm run build:wasm
    Result: passed. CMake built `ofg_imgui` and linked `ofg_cpp_wasm`, generating `assets\wasm\ofg_cpp\ofg_cpp.js` and `assets\wasm\ofg_cpp\ofg_cpp.wasm`.

    npm run test:cpp
    Result: passed. CMake built `ofg_imgui`, linked `ofg_cpp_tests.exe`, and CTest reported `100% tests passed, 0 tests failed out of 1`.

    npm run format:cpp:check
    Result: passed. The formatter checked 191 OFG C++ source/header/test files; vendored `cpp\third_party` files are intentionally outside this formatting set.

Milestone 1 review:

    Scope: `C:\dev\ofg\cpp\third_party\imgui`, `C:\dev\ofg\cpp\CMakeLists.txt`, and the active ExecPlan.
    Reviewers: contract, code quality, legacy, correctness, validation local passes.
    Required findings fixed: none.
    Follow-ups recorded: none.
    Rejected findings: none.
    Validation rerun: no rerun needed after review because no fixes were required.
    Remaining risk: Dear ImGui is build-only at this milestone; renderer lifecycle and input/capture risks begin in later milestones.

Milestone 2 validation:

    npm run format:cpp
    Result: passed. Formatted 195 C++ files.

    npm run test:cpp
    Result: passed. CTest reported `100% tests passed, 0 tests failed out of 1` with `debug_menu_test.cpp` included.

    npm run coverage:cpp
    Result: passed after updating the coverage wrapper to include `cpp\src\debug`. The report printed `cpp\src\debug\debug_menu.cpp line coverage 97.92%` and `cpp\src\debug\debug_scalars.cpp line coverage 92.31%`.

    npm run build:wasm
    Result: passed. Emscripten rebuilt `debug_menu.cpp` and `debug_scalars.cpp` into the shared WASM library and regenerated `assets\wasm\ofg_cpp\ofg_cpp.js` / `.wasm`.

    npm run format:cpp:check
    Result: passed. Checked 195 C++ files.

    git -c safe.directory=C:/dev/ofg diff --check -- ...
    Result: passed with only the existing CMake line-ending warning for `cpp\CMakeLists.txt`.

Milestone 2 review:

    Scope: `C:\dev\ofg\cpp\include\ofg\debug\debug_menu.hpp`, `C:\dev\ofg\cpp\src\debug\debug_menu.cpp`, `C:\dev\ofg\cpp\src\debug\debug_scalars.cpp`, `C:\dev\ofg\cpp\tests\debug_menu_test.cpp`, `C:\dev\ofg\cpp\CMakeLists.txt`, `C:\dev\ofg\tools\cpp-coverage.mjs`, and this ExecPlan.
    Reviewers: contract, code quality, legacy, correctness, validation local passes.
    Required findings fixed: `cpp\src\debug` was initially absent from the C++ per-file coverage gate; the coverage wrapper was updated and the coverage gate rerun successfully.
    Follow-ups recorded: none.
    Rejected findings: none.
    Validation rerun: `npm run coverage:cpp`, `npm run build:wasm`, `npm run format:cpp:check`, and `git diff --check`.
    Remaining risk: DebugMenu is not rendered yet; Milestone 3 must prove Dear ImGui can render the cached tree without mixing registry ownership into ImGui code.

Dear ImGui root README describes the core library as self-contained C++ files and says it emits vertex buffers that can be rendered inside a 3D application.

Dear ImGui backend documentation distinguishes platform backends, which feed input/timing/window data, from renderer backends, which create textures and render ImGui draw data.

Dear ImGui WebGPU backend exposes `ImGui_ImplWGPU_Init`, `ImGui_ImplWGPU_NewFrame`, `ImGui_ImplWGPU_RenderDrawData`, `ImGui_ImplWGPU_Shutdown`, and device-object helpers. Its init info includes `WGPUDevice`, frame count, render-target format, optional depth/stencil format, and multisample state.

Dear ImGui examples documentation lists `example_glfw_wgpu` as supporting Emscripten web, Dawn native, and WGPU native, but OFG should not use GLFW. OFG should reuse only the WebGPU renderer backend and supply its own narrow platform/input bridge.

Current OFG file notes:

`C:\dev\ofg\cpp\CMakeLists.txt` already uses Clang-only C++20 and links `webgpu_c` for native tests/smoke, plus `--use-port=emdawnwebgpu` for Emscripten.

`C:\dev\ofg\cpp\src\render\renderer.cpp` is the correct integration point for a final overlay pass because it owns renderer pass order and already receives the platform `RenderTarget`.

`C:\dev\ofg\cpp\src\game\game.cpp` should stay thin. It may expose status snapshots and route input/capture decisions, but widget code belongs in the debug UI subsystem.

`C:\dev\ofg\cpp\include\ofg\render\shadow_debug_pass.hpp` and `C:\dev\ofg\cpp\src\render\shadow_debug_pass.cpp` already implement a final-target shadow-map cascade preview pass. It is currently toggled through a renderer setter and a special control-input edge; that path should become the first `DebugBool` consumer.

`C:\dev\ofg\src\app\controlInput.ts` currently requests pointer lock on canvas click and only forwards gameplay-oriented control fields. Interactive debug UI needs a broader raw input path and a policy for avoiding pointer lock while ImGui wants mouse input.

## Interfaces and Dependencies

Expected new vendored/dependency files:

`C:\dev\ofg\cpp\third_party\imgui\SOURCE.md`

`C:\dev\ofg\cpp\third_party\imgui\LICENSE.txt`

`C:\dev\ofg\cpp\third_party\imgui\imgui.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui.h`

`C:\dev\ofg\cpp\third_party\imgui\imgui_draw.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui_internal.h`

`C:\dev\ofg\cpp\third_party\imgui\imgui_tables.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui_widgets.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imconfig.h`

`C:\dev\ofg\cpp\third_party\imgui\imstb_rectpack.h`

`C:\dev\ofg\cpp\third_party\imgui\imstb_textedit.h`

`C:\dev\ofg\cpp\third_party\imgui\imstb_truetype.h`

`C:\dev\ofg\cpp\third_party\imgui\backends\imgui_impl_wgpu.cpp`

`C:\dev\ofg\cpp\third_party\imgui\backends\imgui_impl_wgpu.h`

Expected OFG-owned C++ interfaces, names adjustable if implementation reveals a clearer local pattern:

`C:\dev\ofg\cpp\include\ofg\debug\debug_menu.hpp`

`C:\dev\ofg\cpp\src\debug\debug_menu.cpp`

`C:\dev\ofg\cpp\include\ofg\debug\debug_ui.hpp`

`C:\dev\ofg\cpp\src\debug\debug_ui.cpp`

`class DebugBool` should provide at least:

    DebugBool(const char* path, bool default_value) noexcept;
    operator bool() const noexcept;
    DebugBool& operator=(bool value) noexcept;
    bool value() const noexcept;
    bool default_value() const noexcept;
    std::string_view path() const noexcept;
    void set(bool value) noexcept;

`class DebugInt` should provide at least:

    DebugInt(const char* path, int default_value) noexcept;
    operator int() const noexcept;
    DebugInt& operator=(int value) noexcept;
    int value() const noexcept;
    int default_value() const noexcept;
    std::string_view path() const noexcept;
    void set(int value) noexcept;

`class DebugFloat` should provide at least:

    DebugFloat(const char* path, float default_value) noexcept;
    operator float() const noexcept;
    DebugFloat& operator=(float value) noexcept;
    float value() const noexcept;
    float default_value() const noexcept;
    std::string_view path() const noexcept;
    void set(float value) noexcept;

`class DebugMenu` should provide at least:

    static DebugMenu& instance() noexcept;
    std::uint64_t registry_generation() const noexcept;
    bool refresh_tree_if_dirty();
    std::optional<bool> get_bool(std::string_view path) const;
    bool set_bool(std::string_view path, bool value);
    std::span<const DebugBoolEntry> bool_entries() const noexcept;
    std::optional<int> get_int(std::string_view path) const;
    bool set_int(std::string_view path, int value);
    std::span<const DebugIntEntry> int_entries() const noexcept;
    std::optional<float> get_float(std::string_view path) const;
    bool set_float(std::string_view path, float value);
    std::span<const DebugFloatEntry> float_entries() const noexcept;
    const DebugMenuTree& tree() const;
    DebugMenuDiagnostics diagnostics() const;

`DebugMenuDiagnostics` should provide stable diagnostic ordering in registration order and enough fields for tests: path, attempted type, diagnostic kind (`invalid_path`, `duplicate_path`, or `wrong_type_lookup` if wrong-type lookups are recorded), and a short message.

`DEBUG_BOOL(path, variable, default_value)`, `DEBUG_INT(path, variable, default_value)`, and `DEBUG_FLOAT(path, variable, default_value)` should each expand to a global or namespace-scope debug scalar object named by `variable`. Expected uses are:

    DEBUG_BOOL("render/shadows/show_debug_overlay", g_show_shadow_debug_overlay, false);
    DEBUG_INT("render/shadows/cascade_index", g_shadow_debug_cascade_index, 0);
    DEBUG_FLOAT("render/exposure/debug_scale", g_debug_exposure_scale, 1.0f);

`class DebugUi` with lifecycle methods similar to renderer passes:

    static std::unique_ptr<DebugUi> create(const GpuContext& gpu, WGPUTextureFormat target_format);
    void resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    void set_input(DebugUiInput input);
    void begin_frame(const DebugUiFrameInfo& frame_info);
    void render(WGPUCommandEncoder encoder, const RenderTarget& target, const DebugUiFrameInfo& frame_info);
    DebugUiStatus status() const noexcept;

`DebugUi::create` borrows WebGPU handles from `GpuContext`; it does not take ownership of the device or queue. `DebugUi::render` borrows the per-frame `RenderTarget`; platform frame drivers still own acquisition, presentation, finish, submit, and per-frame handle release.

`struct DebugUiFrameInfo` should include at least frame time in milliseconds, delta time in seconds if needed, canvas physical width/height, device pixel ratio, target format, debug UI input, a read-only runtime status snapshot, and the previous capture flags used by the browser host.

`struct DebugUiInput` should represent raw browser/platform input for one frame without gameplay interpretation.

`struct DebugUiStatus` should include at least visibility, `m_wants_capture_mouse`, `m_wants_capture_keyboard`, and any diagnostic counters needed by smoke/tests.

Expected TypeScript/WASM facade additions:

`C:\dev\ofg\src\app\wasmRuntime.ts` should gain typed forwarding for raw debug UI input and typed parsing for new debug status fields.

`C:\dev\ofg\src\app\controlInput.ts` may be split or renamed only if it improves ownership clarity. It can collect raw DOM events and produce both gameplay `ControlInput` and raw `DebugUiInput`, but it must not own Dear ImGui behavior.

Browser debug scalar listing/editing APIs are intentionally deferred. Do not add `debugScalars()`, `getDebugBool(...)`, `setDebugBool(...)`, or similar TypeScript/Embind APIs in this plan unless the plan is explicitly revised first.

Expected tests:

`C:\dev\ofg\cpp\tests\debug_menu_test.cpp` for `DebugBool`, `DebugInt`, and `DebugFloat` default values, implicit scalar reads, assignment, typed get/set, wrong-type lookup behavior, path validation, duplicate and cross-type duplicate diagnostics, path ordering, and ImGui-independent registry behavior.

`C:\dev\ofg\cpp\tests\debug_ui_test.cpp` for lifecycle, validation, status, rendering the registered debug menu, and no-GPU defensive paths where possible.

Existing renderer/native tests updated only as needed for counters/status/visual expectations.

Existing TypeScript tests in `C:\dev\ofg\tests\ts\wasmRuntime.test.ts` and `C:\dev\ofg\tests\ts\controlInput.test.ts`, or a new focused debug input test, should cover the browser facade shape and pointer-lock/capture policy.
