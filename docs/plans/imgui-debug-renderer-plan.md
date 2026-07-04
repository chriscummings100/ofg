# Integrate Dear ImGui as the C++ Debug Renderer

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If `C:\dev\ofg\PLANS.md` is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

OFG needs an in-engine debug UI rendered by C++ on top of the WebGPU scene. After this change, running the browser app or native render smoke should show a small Dear ImGui based "OFG Debug" overlay drawn by the C++ renderer after the scene has been tone-mapped into the platform render target. The overlay should expose live renderer/runtime diagnostics such as frame count, adapter/backend, camera mode, culling counts, bloom status, temp-buffer usage, and later mutable debug settings.

The important architectural outcome is that Dear ImGui is integrated as a renderer-layer debug facility, not as a TypeScript DOM overlay. TypeScript may collect and forward raw browser events, but C++ owns Dear ImGui context lifetime, UI state, input interpretation, draw-data generation, WebGPU rendering, and debug capture decisions.

## Progress

- [x] (2026-07-04 12:59Z) Planning context gathered: read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\cpp\CMakeLists.txt`, renderer/game/browser frame code, runtime status code, and upstream Dear ImGui WebGPU backend notes.
- [x] (2026-07-04 12:59Z) Confirmed this is still planning phase only. No implementation files, vendored source, CMake build graph, tests, or runtime behavior have been changed by this plan.
- [ ] Milestone 1: Vendor Dear ImGui and add a build-only C++ integration target.
- [ ] Milestone 2: Add a renderer-owned, non-interactive ImGui overlay pass after tone mapping.
- [ ] Milestone 3: Add browser/raw-input forwarding and C++ capture decisions for interactive debug UI.
- [ ] Milestone 4: Promote useful renderer/runtime panels and update API contracts, smoke validation, screenshots, and coverage.

## Surprises & Discoveries

- Observation: `Renderer::render_impl` already has a clean final-target boundary. It renders scene color and depth, runs bloom, then calls `ToneMapPass::render(...)` into the platform `RenderTarget`.
  Evidence: `C:\dev\ofg\cpp\src\render\renderer.cpp` calls `m_tone_map_pass->render(...)` near the end of `Renderer::render_impl`.

- Observation: `Game` is a lifecycle facade and should not become the owner of ImGui behavior.
  Evidence: `C:\dev\ofg\docs\GUIDES.md` says facade and lifecycle files such as `game.cpp` should stay thin; feature-specific behavior belongs in owning subsystems.

- Observation: Upstream Dear ImGui provides a WebGPU renderer backend that supports Emscripten with the Dawn WebGPU port used by OFG.
  Evidence: upstream `backends/imgui_impl_wgpu.h` states Emscripten defaults to the Dawn backend and requires `--use-port=emdawnwebgpu` with Emscripten 4.0.10 or newer; OFG already compiles browser C++ with `--use-port=emdawnwebgpu`.

- Observation: OFG currently has raw game-control input, not the richer mouse/key/text event stream that an interactive Dear ImGui platform backend needs.
  Evidence: `C:\dev\ofg\cpp\include\ofg\core\control_input.hpp` only carries movement axes, look deltas, fast/slow, and camera-cycle edge.

## Decision Log

- Decision: Use upstream Dear ImGui core plus upstream `imgui_impl_wgpu` as the renderer backend, and write an OFG-specific platform/input bridge rather than using GLFW or SDL.
  Rationale: OFG has no engine dependency and already owns browser setup through TypeScript plus WebGPU handles through C++. Pulling in GLFW/SDL would add a window/input framework that does not match the browser host. The upstream WebGPU renderer backend directly matches OFG's WebGPU layer.
  Date/Author: 2026-07-04 / Codex

- Decision: Integrate the overlay as a `Renderer` subsystem, with only thin orchestration/status plumbing in `Game`.
  Rationale: `docs/API_CONTRACTS.md` says C++ owns renderer internals and browser/native frame drivers own acquisition/submission. The overlay should be another renderer pass, not DOM UI or gameplay state.
  Date/Author: 2026-07-04 / Codex

- Decision: Implement the first visible overlay as non-interactive, then add input capture in a separate milestone.
  Rationale: This de-risks build, WebGPU backend, render-pass ordering, native smoke, and browser smoke before changing browser input policy and pointer-lock behavior.
  Date/Author: 2026-07-04 / Codex

- Decision: Keep Dear ImGui visible by default with a compact debug panel during initial integration.
  Rationale: The feature must be observable in screenshots and smoke artifacts. A small panel is easier to validate than a hidden debug renderer that only proves itself through code.
  Date/Author: 2026-07-04 / Codex

## Outcomes & Retrospective

Planning is complete enough to begin implementation later. No runtime outcome has been delivered yet. After implementation, this section must record whether the overlay appeared in browser and native smoke, whether input capture felt usable with pointer lock, which screenshots were reviewed, and any remaining gaps such as docking, multi-viewport, custom fonts, or editable renderer settings.

## Contract and Quality Baseline

This plan preserves and extends the following contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`:

OFG-BOOT-001 TypeScript Host Ownership: preserve. TypeScript may continue to own DOM boot, canvas lookup, raw browser event collection, and WASM method calls. TypeScript must not own Dear ImGui widgets, debug panel state, renderer draw data, GPU resources, or debug-renderer settings.

OFG-BOOT-002 C++ Runtime Ownership: extend. C++ will own Dear ImGui context lifetime, the OFG debug UI model, ImGui draw-data generation, the WebGPU ImGui renderer backend, and whether gameplay controls should be ignored because ImGui wants mouse or keyboard capture.

OFG-BOOT-003 WASM Facade: extend narrowly. The browser facade may gain methods for raw debug input snapshots/events and status fields for debug UI visibility/capture. It must not expose GPU handles, renderer internals, ImGui pointers, or mutable scene/resource objects to TypeScript.

OFG-BOOT-004 Renderer Compatibility: extend. Browser and native smoke must continue to validate equivalent scene rendering, and must also either validate the visible C++ debug overlay or explicitly render with it disabled in tests that are only about scene pixels. This plan prefers validating the visible overlay.

OFG-BOOT-005 WebGPU Baseline: preserve. Dear ImGui must not require optional WebGPU features or manually request limits above adapter defaults. It may create ordinary buffers, texture views, bind groups, samplers, shader modules, and render pipelines needed by the upstream WebGPU backend.

OFG-BOOT-006 Resource Lifetime: extend. Dear ImGui GPU resources must be durable across steady-state frames and released during renderer release. Ordinary frames may update ImGui vertex/index buffers and font texture data as expected by ImGui, but must not recreate the context or pipeline every frame.

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

## Plan of Work

Milestone 1 vendors and builds Dear ImGui without changing runtime behavior. Add `C:\dev\ofg\cpp\third_party\imgui` with a pinned upstream snapshot, `SOURCE.md`, and `LICENSE.txt`. Include only the core root files needed by Dear ImGui plus `backends/imgui_impl_wgpu.cpp` and `backends/imgui_impl_wgpu.h`; do not include GLFW, SDL, examples, or docking branch code unless a later decision changes scope. Add a CMake target such as `ofg_imgui` that compiles third-party files with warnings suppressed, links it privately into `ofg_cpp`, and defines `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` where needed. This milestone should pass `npm run build:wasm` and `npm run test:cpp` without creating or rendering any ImGui context.

Milestone 2 adds the non-interactive renderer overlay. Add an OFG-owned subsystem under `C:\dev\ofg\cpp\include\ofg\debug\debug_ui.hpp` and `C:\dev\ofg\cpp\src\debug\debug_ui.cpp`, or another nearby path if implementation reveals a better renderer-owned naming boundary. The subsystem should own Dear ImGui context creation/destruction, call `ImGui_ImplWGPU_Init` with the active `WGPUDevice` and final target format, set display size and framebuffer scale, build one compact "OFG Debug" panel, render draw data into a final load pass, and release all ImGui resources in renderer release order. Add `std::unique_ptr<DebugUi>` to `Renderer`, create it in `Renderer::prepare_impl` after the tone-map pass, render it after `ToneMapPass::render(...)`, include its durable pipelines/buffers in renderer counters if useful, and reset it during `Renderer::release_impl` before the borrowed GPU context is cleared. `Game` should only pass status snapshots or diagnostics needed by the panel; it should not build widgets.

Milestone 3 adds interactive browser input. Introduce an OFG-owned C++ input snapshot for debug UI, separate from gameplay `ControlInput`, with fields for display width/height, device pixel ratio, mouse position, mouse buttons, wheel deltas, key transitions, modifier state, text input characters, focus, and a visibility toggle such as F1. Extend `BrowserGame` and Embind with a narrow method that accepts raw browser input data. Extend TypeScript collection to forward raw events and to read C++ status fields such as `debugUiVisible`, `debugUiWantsCaptureMouse`, and `debugUiWantsCaptureKeyboard`. C++ should decide when ImGui captures gameplay input. TypeScript may use the previous frame's capture flags to avoid requesting pointer lock while the interactive debug UI is open, but TypeScript must not decide widget behavior or renderer state.

Milestone 4 fills out useful panels and validation. Add debug panels for runtime status, renderer counters, culling, bloom, temp buffers, player/model state, camera mode, and maybe a checkbox for `ImGui::ShowDemoWindow()` while the integration is fresh. Update `RuntimeDebugStatus` JSON, `src/app/wasmRuntime.ts`, TypeScript tests, C++ tests, browser smoke, native smoke, API contracts, and system docs. Capture screenshots from the dev server and native smoke artifacts. Run coverage and record any exceptions for device-bound or third-party code in this plan and `C:\dev\ofg\COVERAGE.md` only if necessary.

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
    npm run build:wasm
    npm run smoke:render
    npm run smoke:browser:cpp

Expected result: browser and native render outputs show the same scene plus a compact C++ debug overlay. Native render smoke writes a PNG and report under `C:\dev\ofg\artifacts`; browser smoke reports initialized C++ renderer status.

Milestone 3 commands:

    npm run test:ts
    npm run test:cpp
    npm run smoke:browser
    npm run smoke:browser:cpp

Expected result: when the debug UI is open, mouse/keyboard input can interact with ImGui controls and gameplay controls do not also consume captured input. When the debug UI is closed or not capturing, existing player/camera controls continue to work.

Milestone 4 commands:

    npm test
    npm run smoke
    npm run coverage

Expected result: C++ and TypeScript unit gates pass, browser/native smoke pass, and changed OFG-owned implementation files do not appear in the default filtered coverage attention output unless this plan records a specific exception and rationale.

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

The native render smoke PNG displays the same overlay on top of the offscreen scene, or the plan records a deliberate disabled-overlay smoke mode with a separate browser screenshot proving visibility.

The overlay is rendered after tone mapping and does not replace, clear, resize, or otherwise corrupt the scene output.

Dear ImGui context and WebGPU renderer resources are created once per renderer lifetime, reused across ordinary frames, and released during renderer teardown.

TypeScript does not own widgets, debug panel state, ImGui draw data, renderer resources, or debug rendering decisions.

Interactive mode forwards raw browser input to C++, and C++ exposes capture state so gameplay controls do not conflict with ImGui controls.

Debug status JSON remains stable and tested. Any new fields are added to `C:\dev\ofg\cpp\include\ofg\runtime\runtime_debug_status.hpp`, `C:\dev\ofg\cpp\src\runtime\runtime_debug_status.cpp`, `C:\dev\ofg\src\app\wasmRuntime.ts`, C++ tests, and TypeScript tests together.

Validation commands:

    npm run test:cpp
    npm run test:ts
    npm run smoke:browser
    npm run smoke:browser:cpp
    npm run smoke:render
    npm run coverage

Screenshot cadence:

During Milestone 2 and later visual work, start or keep the dev server running with:

    npm run dev

Report the printed URL in chat. Capture screenshots after first visible overlay, after input capture works, after final panel layout, and before final acceptance. Store durable screenshots under `C:\dev\ofg\artifacts\debug-ui\` when they are useful for comparing progress. Present each screenshot path in chat for human review.

Coverage gate:

The plan is complete only when modified OFG-owned implementation files pass the default coverage attention gate. Third-party Dear ImGui files should be treated as vendored dependency source and not as files requiring OFG line coverage.

## Idempotence and Recovery

Vendoring should be additive and pinned. If the wrong Dear ImGui snapshot is imported, remove only `C:\dev\ofg\cpp\third_party\imgui` and the matching CMake target changes from the current milestone, then re-import the chosen snapshot.

Renderer integration should be behind a single `DebugUi` subsystem. If WebGPU validation fails, temporarily disable only the `DebugUi::render(...)` call while preserving build and lifecycle code, then debug pass descriptors and backend init info in isolation.

If browser input conflicts with gameplay pointer lock, keep the visible non-interactive overlay from Milestone 2 and back out only the input forwarding changes for Milestone 3. The overlay should remain shippable while input policy is corrected.

If native smoke pixel thresholds fail only because the overlay adds expected pixels, update `C:\dev\ofg\tools\smoke-contract.json` and smoke inspection logic with explicit overlay-aware thresholds rather than weakening scene validation broadly.

Never use `git reset --hard` or checkout commands to recover from this work. Use targeted patches that remove only the files and lines added for the current milestone.

## Artifacts and Notes

Upstream source notes gathered during planning:

Dear ImGui root README describes the core library as self-contained C++ files and says it emits vertex buffers that can be rendered inside a 3D application.

Dear ImGui backend documentation distinguishes platform backends, which feed input/timing/window data, from renderer backends, which create textures and render ImGui draw data.

Dear ImGui WebGPU backend exposes `ImGui_ImplWGPU_Init`, `ImGui_ImplWGPU_NewFrame`, `ImGui_ImplWGPU_RenderDrawData`, `ImGui_ImplWGPU_Shutdown`, and device-object helpers. Its init info includes `WGPUDevice`, frame count, render-target format, optional depth/stencil format, and multisample state.

Dear ImGui examples documentation lists `example_glfw_wgpu` as supporting Emscripten web, Dawn native, and WGPU native, but OFG should not use GLFW. OFG should reuse only the WebGPU renderer backend and supply its own narrow platform/input bridge.

Current OFG file notes:

`C:\dev\ofg\cpp\CMakeLists.txt` already uses Clang-only C++20 and links `webgpu_c` for native tests/smoke, plus `--use-port=emdawnwebgpu` for Emscripten.

`C:\dev\ofg\cpp\src\render\renderer.cpp` is the correct integration point for a final overlay pass because it owns renderer pass order and already receives the platform `RenderTarget`.

`C:\dev\ofg\cpp\src\game\game.cpp` should stay thin. It may expose status snapshots and route input/capture decisions, but widget code belongs in the debug UI subsystem.

`C:\dev\ofg\src\app\controlInput.ts` currently requests pointer lock on canvas click and only forwards gameplay-oriented control fields. Interactive debug UI needs a broader raw input path and a policy for avoiding pointer lock while ImGui wants mouse input.

## Interfaces and Dependencies

Expected new vendored/dependency files:

`C:\dev\ofg\cpp\third_party\imgui\SOURCE.md`

`C:\dev\ofg\cpp\third_party\imgui\LICENSE.txt`

`C:\dev\ofg\cpp\third_party\imgui\imgui.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui.h`

`C:\dev\ofg\cpp\third_party\imgui\imgui_draw.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui_tables.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui_widgets.cpp`

`C:\dev\ofg\cpp\third_party\imgui\imgui_demo.cpp`

`C:\dev\ofg\cpp\third_party\imgui\backends\imgui_impl_wgpu.cpp`

`C:\dev\ofg\cpp\third_party\imgui\backends\imgui_impl_wgpu.h`

Expected OFG-owned C++ interfaces, names adjustable if implementation reveals a clearer local pattern:

`C:\dev\ofg\cpp\include\ofg\debug\debug_ui.hpp`

`C:\dev\ofg\cpp\src\debug\debug_ui.cpp`

`class DebugUi` with lifecycle methods similar to renderer passes:

    static std::unique_ptr<DebugUi> create(GpuContext gpu, WGPUTextureFormat target_format);
    void resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
    void set_input(DebugUiInput input);
    void begin_frame(double time_ms, const RuntimeDebugStatus& status);
    void render(WGPUCommandEncoder encoder, RenderTarget target, const RuntimeDebugStatus& status);
    DebugUiStatus status() const noexcept;

`struct DebugUiInput` should represent raw browser/platform input for one frame without gameplay interpretation.

`struct DebugUiStatus` should include at least visibility, `m_wants_capture_mouse`, `m_wants_capture_keyboard`, and any diagnostic counters needed by smoke/tests.

Expected TypeScript/WASM facade additions:

`C:\dev\ofg\src\app\wasmRuntime.ts` should gain typed forwarding for raw debug UI input and typed parsing for new debug status fields.

`C:\dev\ofg\src\app\controlInput.ts` may be split or renamed only if it improves ownership clarity. It can collect raw DOM events and produce both gameplay `ControlInput` and raw `DebugUiInput`, but it must not own Dear ImGui behavior.

Expected tests:

`C:\dev\ofg\cpp\tests\debug_ui_test.cpp` for lifecycle, validation, status, and no-GPU defensive paths where possible.

Existing renderer/native tests updated only as needed for counters/status/visual expectations.

Existing TypeScript tests in `C:\dev\ofg\tests\ts\wasmRuntime.test.ts` and `C:\dev\ofg\tests\ts\controlInput.test.ts`, or a new focused debug input test, should cover the browser facade shape and pointer-lock/capture policy.

