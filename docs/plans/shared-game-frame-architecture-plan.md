# Extract a shared C++ Game frame architecture

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Move OFG toward one shared C++ game/render frame path that is used by both the browser and the native Dawn smoke. After this change, browser-specific code should mostly create a browser WebGPU surface, acquire a canvas texture, create an encoder, call the shared game object, finish and submit. Native Dawn-specific code should mostly create a Dawn device, create an offscreen texture, create an encoder, call the same shared game object, append readback copy commands, finish, submit, and inspect pixels.

The user-visible output should remain the current bootstrap triangle until the renderer-resource plan replaces it with the 3D scene. This transition is successful when `npm run smoke:browser`, `npm run smoke:browser:cpp`, and `npm run smoke:render` still pass, but the code path that owns frame state, render resources, update/tick behavior, and draw encoding is shared behind a C++ `Game` object.

The key architectural rule is:

    Platform code owns platform handles and frame boundaries.
    Game code owns game state, renderer resources, and render command encoding.

For this plan, "platform code" means `C:\dev\ofg\cpp\src\web` for Emscripten/browser WebGPU and `C:\dev\ofg\cpp\src\native` for the Dawn render smoke. "Game code" means new shared C++ modules that are built into both the browser WASM executable and the native render-smoke executable.

## Progress

- [x] (2026-06-21 19:51Z) Re-read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\docs\SYSTEMS.md`, and `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md`.
- [x] (2026-06-21 19:51Z) Confirmed the current shared renderer island is `BootstrapRenderer`, while browser and native code still independently own frame driving, target acquisition, submission, and status/reporting.
- [x] (2026-06-21 19:51Z) Recorded the design decision from discussion: the shared render-entry function should be named `render`, not `render_to_view` or `encode_render`.
- [x] (2026-06-21 20:05Z) Removed the confusing `OFG_ENABLE_WEBGPU_RENDERER` compile definition after confirming the gated renderer files are already built only in WebGPU-capable WASM and native Dawn smoke targets.
- [x] (2026-06-21 20:35Z) Reviewed this plan through the `review-plan` skill and recorded the user decision that tests may link WebGPU directly; do not introduce CPU/GPU library splits to work around test linkage.
- [x] (2026-06-21 20:55Z) Re-reviewed the plan after the target-graph edits and accepted the user correction that library/linkage cleanup is the first milestone because every later change depends on it.
- [x] (2026-06-21 21:10Z) Milestone 1 complete: simplified the C++ target graph so tests, WASM, and native smoke link the same WebGPU-capable shared `ofg_cpp` library.
- [x] (2026-06-21 21:58Z) Milestone 2 complete: added shared `GpuContext`, `RenderTarget`, `GameRuntime`, and `Game`, with browser runtime behavior delegated through `GameRuntime` while visual output remains unchanged.
- [x] (2026-06-21 22:15Z) Milestone 3 complete: `BrowserGame` now owns browser frame-driver work only and delegates frame state, debug status, renderer ownership, and render command recording to shared `Game`.
- [x] (2026-06-21 22:33Z) Milestone 4 complete: native Dawn smoke now creates the same shared `Game`, calls `Game::resize`, `Game::tick`, and `Game::render`, and keeps offscreen/readback/report duties in native code.
- [x] (2026-06-21 22:53Z) Milestone 5 complete: active docs/contracts and the renderer-resource plan describe the `Game` boundary, final tests/coverage/smokes passed, and screenshot/report artifacts were inspected.

## Surprises & Discoveries

- Observation: The current repository already has the right low-level proof point, but at the wrong level.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp` exposes a renderer used by both browser and native paths, but `C:\dev\ofg\cpp\src\web\browser_game.cpp` and `C:\dev\ofg\cpp\src\native\render_smoke.cpp` each own their own frame-driving and target lifecycle.

- Observation: `render_smoke.cpp` is already in the file-size concern band before this transition.
  Evidence: The active renderer-resource plan notes that `C:\dev\ofg\cpp\src\native\render_smoke.cpp` is in the 500-1000 line concern range and should be split before more native behavior is added.

- Observation: The active renderer-resource plan had stale top-level `render_to_view` and per-frame `GpuContext` wording.
  Evidence: `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md` now sketches `OpaqueRenderer::render`, treats `Game::render` as the top-level browser/native boundary, and keeps `GpuContext` only on explicit asset mutation methods.

- Observation: `OFG_ENABLE_WEBGPU_RENDERER` was not carrying the real build boundary.
  Evidence: The only gated files were `bootstrap_renderer` and `webgpu_common`, and `C:\dev\ofg\cpp\CMakeLists.txt` already compiles those source files only into `ofg_cpp_wasm` and `ofg_render_smoke_cpp`, the two targets that provide `webgpu.h`.

- Observation: OFG should not introduce a CPU-only versus GPU-bound library split just to satisfy test linkage.
  Evidence: The desired shape is one WebGPU-capable shared C++ library, one doctest executable, the browser WASM executable, and the native render-smoke executable. It is acceptable for tests to include/link WebGPU when testing code that uses WebGPU types.

- Observation: Linking the shared OFG library directly to `dawn::webgpu_dawn` makes native doctests build the full Dawn runtime and leaks upstream Dawn/Tint tests into CTest.
  Evidence: The first `npm run test:cpp` attempt built 1011 Dawn-related steps and failed because CTest discovered `tint_unittests` that was not built. Switching `ofg_cpp` to Dawn's header-only `webgpu_c` target, linking `dawn::webgpu_dawn` only into `ofg_render_smoke_cpp`, disabling Dawn/Tint test/tool targets, and filtering CTest to `^ofg_cpp_tests$` made the same shared-library test pass.

- Observation: Once `Game` entered the shared library, doctests needed the Dawn runtime link even for validation-only `Game` tests.
  Evidence: `ofg_cpp_tests` now links `dawn::webgpu_dawn`; clean `npm run test:cpp` and `npm run coverage:cpp` each build roughly 1015 native steps and take about five minutes on this machine. This keeps the library model simple and follows the user decision that tests may link WebGPU.

- Observation: Treating every runtime error as a GPU/device error would make recoverable validation failures sticky.
  Evidence: The Milestone 2 review found that `GameRuntime::mark_error` cleared GPU-ready state, so a bad render target or invalid browser resize parse could leave status non-initialized after later valid work. `mark_error` now preserves ready resources, `mark_gpu_error` handles actual device/setup failures, and regression tests cover both paths.

- Observation: Deleting `BrowserRuntime` required moving its useful coverage cases into `GameRuntime` tests.
  Evidence: The first post-delete `npm run coverage:cpp` failed with `cpp/src/game/game_runtime.cpp` at 77.44%. Adding debug JSON, renderer counter, zero-size recovery, idempotent configuration, pre-GPU configuration, invalid resize, and disposed mutation tests brought the checked file back to 100.00%.

- Observation: `BrowserGame.cpp` is still in the file-size concern band after the browser frame-driver refactor.
  Evidence: `cpp/src/web/browser_game.cpp` is 683 lines. This is below the critical range, but the callback/setup/frame-driver split should be revisited before adding more browser behavior.

- Observation: The native smoke refactor kept the one-submit frame shape while moving render command ownership to `Game`.
  Evidence: `C:\dev\ofg\cpp\src\native\render_smoke.cpp` now calls `Game::render`, then appends `wgpuCommandEncoderCopyTextureToBuffer`, finishes the command encoder, and submits once. The submit-site grep finds only one browser submit and one native submit.

- Observation: `render_smoke.cpp` is close to the top of the file-size concern band after the native handoff.
  Evidence: `C:\dev\ofg\cpp\src\native\render_smoke.cpp` is 925 lines. It still passes validation, but the next native-smoke expansion should split Dawn setup, readback, report writing, or argument parsing into smaller modules before adding much more behavior.

## Decision Log

- Decision: Introduce a shared `ofg::Game` class before building the larger renderer-resource pipeline.
  Rationale: The browser and native paths should not each learn how to drive every future renderer feature. A shared `Game` gives the renderer plan one integration target and keeps browser/Dawn code small.
  Date/Author: 2026-06-21 / Codex

- Decision: Keep `BrowserGame::frame(time_ms)` as the external Embind/TypeScript facade, but call the shared `Game` internally.
  Rationale: The TypeScript contract should stay narrow and stable. The internal shared method names can improve without breaking `src/app/wasmRuntime.ts`.
  Date/Author: 2026-06-21 / Codex

- Decision: Name the shared render-entry method `render`.
  Rationale: The user explicitly chose `render` as the plain name. It is also the right abstraction: the caller does not need to know whether the implementation records one pass or many passes.
  Date/Author: 2026-06-21 / User and Codex

- Decision: `Game` is constructed for one WebGPU device/queue lifetime and does not receive `GpuContext` on every ordinary call.
  Rationale: OFG currently assumes one active browser device or one active native smoke device. Device loss should destroy and recreate device-owned resources rather than hot-swapping a live `Game` across devices.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Keep the C++ build target model simple, and allow tests to link WebGPU.
  Rationale: Splitting CPU-only lifecycle code from GPU-bound `Game` code just to satisfy native tests would be over-engineering. If a test covers code that exposes or uses `WGPU*` types, the test target should receive the required WebGPU headers/libraries. If the current CMake graph makes that awkward, simplify the graph instead of adding more library layers.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Name the shared C++ library target `ofg_cpp`, link native tests and WASM to that target, and link the Dawn runtime implementation only where native GPU execution is needed.
  Rationale: `ofg_cpp` owns the common WebGPU-capable source files and receives Dawn's header target for native builds. Native smoke links `dawn::webgpu_dawn`; doctests can add that runtime link later if they begin executing real WebGPU calls. This keeps one shared OFG library without forcing every native test run to build the full Dawn runtime.
  Date/Author: 2026-06-21 / Codex

- Decision: Native wrapper scripts resolve an installed Dawn checkout from `OFG_DAWN_SOURCE_DIR` or the local Windows default `C:\tools\dawn`, and reject repository-local toolchain paths.
  Rationale: The npm command shape remains usable for tests and coverage on the current machine, while direct CMake configuration still requires `OFG_DAWN_SOURCE_DIR`. Rejecting `C:\dev\ofg\artifacts\toolchains\...` preserves the installed-toolchain contract from the build cleanup.
  Date/Author: 2026-06-21 / Codex

- Decision: Platform code creates the command encoder, finishes command buffers, and submits. `Game` records commands but never calls `wgpuQueueSubmit`.
  Rationale: Native smoke must append texture-to-buffer readback copy commands after rendering and before the single frame submit. Keeping submit outside `Game` also avoids browser presentation and native readback logic leaking into shared game code.
  Date/Author: 2026-06-21 / User and Codex

- Decision: The ordinary frame path should use one queue submit.
  Rationale: One submit keeps frame ordering and smoke behavior simple. Later explicit non-frame work such as asset streaming or readback probes may use named extra submits, but steady-state rendering should not multiply submissions by accident.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Remove `OFG_ENABLE_WEBGPU_RENDERER` instead of preserving it as an autocomplete-time renderer switch.
  Rationale: The macro hid renderer classes from editors while duplicating a target-level CMake boundary. The renderer sources are already excluded from non-WebGPU targets, so the macro made the code harder to navigate without protecting an active build configuration.
  Date/Author: 2026-06-21 / Codex

- Decision: Treat `BrowserRuntime` as transitional and likely redundant after `Game` exists.
  Rationale: `BrowserGame` should own the browser-only WebGPU machinery, while `Game` should own the shared lifecycle/status/frame state. Keeping a separate shared class named `BrowserRuntime` after that split would be confusing unless implementation proves it still has a clear purpose.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Split recoverable runtime/render errors from GPU/device errors in `GameRuntime`.
  Rationale: Validation failures, such as an invalid resize value or render target mismatch, should report `lastError` and clear `initialized` without forgetting durable renderer/device readiness. Actual WebGPU setup or device-loss errors should still clear readiness and require platform reinitialization.
  Date/Author: 2026-06-21 / Codex

- Decision: Remove `BrowserRuntime` once `BrowserGame` delegates to `Game`.
  Rationale: After `GameRuntime` and `Game` own shared lifecycle/status/frame behavior, keeping a browser-named wrapper would make the architecture harder to read. Browser-only JavaScript-double parsing and surface/frame-driver work now stay in `BrowserGame`; shared status behavior stays in `GameRuntime`.
  Date/Author: 2026-06-21 / Codex

- Decision: Let `GpuContext` carry adapter/backend debug labels and let `Game` expose generic error-reporting hooks.
  Rationale: Once `Game` owns debug status, browser/native frame drivers need a platform-neutral way to report adapter/backend labels and platform failures into that status without moving surface acquisition, command-buffer finish, or queue submission into shared game code.
  Date/Author: 2026-06-21 / Codex

- Decision: Native render smoke advances `Game` with deterministic time `0.0` for the bootstrap triangle frame.
  Rationale: The current visual contract is static, so the native smoke only needs to prove the shared `tick`/`render` path is usable. The renderer-resource plan can introduce a named deterministic animation time when native smoke starts validating animated scene output.
  Date/Author: 2026-06-21 / Codex

## Outcomes & Retrospective

Preflight cleanup removed the confusing `OFG_ENABLE_WEBGPU_RENDERER` macro. Milestone 1 simplified the library/linkage shape: the shared `ofg_cpp` target now owns common runtime, scene, `bootstrap_renderer`, and `webgpu_common` sources, while doctests, browser WASM, and native render smoke link that one target. Milestone 2 added the shared `Game` layer and moved portable lifecycle/status behavior into `GameRuntime`. Milestone 3 switched the browser frame driver to `Game` and retired `BrowserRuntime`. Milestone 4 switched native render smoke to `Game` while keeping Dawn setup, offscreen texture/readback, command-buffer finish/submit, PNG output, and report writing in native code. Milestone 5 aligned active docs and the future renderer-resource plan with the final boundary.

Both browser and native code now call the same device-bound `Game`. Browser-specific code remains responsible for Embind/browser WebGPU setup, surface configuration, current texture acquisition, command encoder creation, command-buffer finish, queue submit, and handle release. Native-specific code remains responsible for Dawn setup, offscreen texture/readback buffers, readback copy, command-buffer finish, queue submit, PNG writing, and report generation. Shared `Game` owns portable frame state, debug status, durable renderer resources, target validation, and render command recording. The smoke artifacts stayed visually identical: a dark blue-gray background with the red/green/blue bootstrap triangle. Coverage exceptions remain explicit for device-bound `Game`/renderer/browser/native WebGPU paths, with native-checkable lifecycle and validation code covered by C++ doctests.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`. TypeScript continues to own DOM boot, canvas sizing, fatal-error display, WASM loading, and smoke helpers. It must not own gameplay simulation, renderer resources, GPU handles, or draw submission.

This plan refines `OFG-BOOT-002 C++ Runtime Ownership`. C++ still owns frame state, debug status, scene data, renderer setup, WebGPU resource creation, browser WebGPU runtime behavior, native Dawn offscreen rendering, and platform queue submission. The refinement is that shared C++ `Game` owns game/render state and render command recording, while browser/native C++ platform code owns target acquisition, command-buffer finish, and queue submission.

This plan preserves `OFG-BOOT-003 WASM Facade`. The browser facade remains create, resize, frame, debug status, and dispose. The public TypeScript-facing method remains `frame(time_ms)`, even though the shared C++ object will have a `render` method for draw command recording.

This plan preserves `OFG-BOOT-004 Renderer Compatibility` for the current bootstrap triangle and prepares its later rewrite. Browser and native smoke must still validate equivalent visual output from the same renderer data, shader source, clear color, and smoke thresholds.

This plan preserves `OFG-BOOT-005 WebGPU Baseline`. It must not introduce optional features, elevated limits, extra steady-state resource recreation, or browser/native visual divergence.

This plan strengthens `OFG-BOOT-006 Resource Lifetime`. Durable renderer resources move behind `Game`, and ordinary frames must not recreate them. Browser resize may reconfigure the surface; `Game::resize` may recreate size-dependent shared resources, such as a future depth texture, but ordinary `Game::render` calls must not recreate durable resources.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Generated WASM, build, smoke, coverage, and deployment artifact directories stay generated. Modified implementation files must pass coverage unless this plan records a specific exception.

Quality constraints from `C:\dev\ofg\AGENTS.md` apply. New and changed C++ files must have top-of-file purpose comments. Every written function should have a purpose comment or doc comment. Functions over 50 lines need internal comments explaining their phases. Files over 500 lines should be treated as a readability concern.

## Context and Orientation

The repository root is `C:\dev\ofg`.

The current browser path is centered on `C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp` and `C:\dev\ofg\cpp\src\web\browser_game.cpp`. `BrowserGame` is the Embind-facing C++ object created by TypeScript through `C:\dev\ofg\src\app\wasmRuntime.ts`. It owns browser WebGPU setup, browser surface configuration, target acquisition, command encoder creation, command buffer finish/submit, and WebGPU resource release. Shared frame state, debug status, renderer resources, and render command recording now live behind `Game`; the transitional `BrowserRuntime` wrapper has been removed.

The current native path is centered on `C:\dev\ofg\cpp\include\ofg\native\render_smoke.hpp`, `C:\dev\ofg\cpp\src\native\render_smoke.cpp`, and `C:\dev\ofg\cpp\src\native\render_smoke_main.cpp`. It creates a Dawn instance/adapter/device/queue, creates the same shared `Game` used by the browser path, creates a native offscreen texture and readback buffer, calls `Game::render`, copies pixels to a padded readback buffer, writes a PNG, and writes a report.

The current shared renderer is `C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp` and `C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp`. It creates a shader module, pipeline layout, render pipeline, and vertex buffer. It exposes `render_to_view`, which records a clear+triangle pass into a caller-owned command encoder. This is a useful shared unit, but it is too low-level to be the future browser/native boundary.

The desired shared object is `Game`. A `Game` is not a global singleton. It is one device-bound game/render frame object constructed after a platform device and queue exist. It owns portable runtime state, frame/update state, renderer resources, and later resource stores and draw lists. It does not own browser surfaces, Dawn backend selection, offscreen readback textures, command buffer submission, or PNG/report writing.

Definitions:

`GpuContext` is a small construction-time value containing the active `WGPUDevice` and `WGPUQueue`. `Game` may store these handles as borrowed, non-owning handles for the device lifetime, but the platform owns them and releases them only after `Game` is destroyed. `Game` must not release the device or queue.

`RenderTarget` is a per-frame borrowed value containing the `WGPUTextureView` to render into, the target format, and target dimensions. In the browser the view comes from `wgpuSurfaceGetCurrentTexture`; in native smoke it comes from an offscreen texture.

`Frame driver` means the platform-specific code that owns one frame boundary: create command encoder, call `Game::render`, append platform-specific commands if needed, finish command buffer, submit exactly once for the ordinary frame, and release per-frame handles.

`Render command encoding` means recording WebGPU commands into a caller-owned `WGPUCommandEncoder`, including draw calls inside render passes. `Queue submission` means calling `wgpuQueueSubmit`. `Game` owns render command encoding; browser/native frame drivers own command-buffer finish and queue submission. This keeps the current API-contract phrase "draw submission" within C++ while making the `wgpuQueueSubmit` owner explicit.

`Native-checkable` means code that can be exercised by the native doctest executable on the developer machine. In this plan, native-checkable code may include WebGPU types and may link Dawn if the code under test needs them. Browser-only Emscripten callback glue remains smoke-covered unless a later test harness makes it native-checkable.

## Plan of Work

Milestone 1 cleans up the C++ target graph before any `Game` implementation begins. Replace the current split where `ofg_cpp_core` is CPU-only and WebGPU sources are duplicated into the WASM and native smoke executables. Create one WebGPU-capable shared C++ library target, or rename/reshape the existing library into that role, containing the common C++ sources used by tests, browser WASM, and native smoke. Link that one library into the doctest executable, the browser WASM executable, and the native render-smoke executable. Update `tools/test-cpp.mjs` and `tools/cpp-coverage.mjs` so native tests and coverage can configure the same Dawn/WebGPU availability that native smoke uses when a tested source includes `WGPU*` types. Update the renderer-resource plan early so future renderer work targets `Game::render` for the top-level frame boundary while still allowing `GpuContext` for explicit asset mutation methods.

Milestone 2 creates the shared game-frame layer without changing visuals. Add shared headers under `C:\dev\ofg\cpp\include\ofg\game\` and source under `C:\dev\ofg\cpp\src\game\`. Add `gpu_context.hpp`, `render_target.hpp`, and `game.hpp`, or equivalent names. Implement `Game` by moving the useful shared lifecycle/status/frame behavior out of `BrowserRuntime` and by owning the existing `BootstrapRenderer`. The initial `Game` should create the bootstrap renderer in `Game::create`, keep durable renderer counters, validate resize dimensions through existing runtime logic, advance frame state on `tick`, and expose `render(WGPUCommandEncoder encoder, RenderTarget target, std::string& error)`. Keep `BootstrapRenderer` intact; its existing `render_to_view` can remain private to the shared `Game` adapter for this milestone. The public forward-looking boundary should be `Game::render`.

Milestone 3 refactors the browser path. Change `BrowserGame` so it still owns Emscripten-specific setup: canvas selector, instance, surface, adapter/device request callbacks, device lost and uncaptured error callbacks, surface capabilities, surface configure/unconfigure, current surface texture acquisition, encoder creation, command finish, queue submit, per-frame handle release, and WebGPU handle release. Move frame state, renderer counters, bootstrap renderer ownership, and render command recording into `Game`. `BrowserGame::frame(time_ms)` should process browser WebGPU events, call `Game` to update frame state, acquire the surface target, create an encoder, call `game_->render`, finish and submit one command buffer, and release handles. `BrowserGame` must store the latest physical size and device-pixel ratio even before `Game` exists, apply that pending size immediately after `Game` creation, and call `Game::resize` before surface acquisition after every accepted size change. Recoverable surface acquisition states such as timeout or outdated should skip, retry, or reconfigure the platform surface without destroying durable `Game` resources; only actual device loss should force device and `Game` recreation. Teardown must reset/destroy `Game` before releasing queue, device, surface, or instance handles.

Milestone 4 refactors the native Dawn smoke. Keep Dawn-specific creation and reporting in native code, but replace direct `BootstrapRenderer::create` and `renderer->render_to_view` calls with `Game::create`, `Game::resize` if needed, `Game::tick` at the deterministic smoke time, and `Game::render`. Native smoke should append its texture-to-buffer copy after `Game::render` and before `wgpuCommandEncoderFinish`, so render and readback are ordered in the same submitted command buffer. Split `render_smoke.cpp` only where needed for this frame-driver/readback seam; leave broad native-smoke file decomposition to the renderer-resource plan.

Milestone 5 removes drift, updates docs/plans, and runs final validation. Update `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\docs\SYSTEMS.md`, and `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md` so they describe the `Game` and platform frame-driver boundary. `SYSTEMS.md` should gain the new `Game` / frame-driver ownership model and remove or rewrite stale claims that `BootstrapRuntime` delegates directly to `BootstrapRenderer` or that `BootstrapRenderer` is the main draw-submission owner. Do helper deduplication or broad renaming only if this work touches that code directly or if it is needed to prevent immediate contract drift. The current triangle visual should remain unchanged. Browser smoke and native render smoke should both prove the same image using the new shared `Game` path. Coverage should pass for changed native-checkable files. Browser-only C++ remains covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke unless a more precise browser-WASM test is added.

## Concrete Steps

From `C:\dev\ofg`, first simplify the build graph in `cpp/CMakeLists.txt` and the native wrapper scripts:

    cpp/CMakeLists.txt
    tools/test-cpp.mjs
    tools/cpp-coverage.mjs

The target graph should have one WebGPU-capable shared OFG C++ library target that owns common sources such as frame state, runtime status, bootstrap scene data, `bootstrap_renderer`, `webgpu_common`, and later `game`. The doctest executable, browser WASM executable, and native render-smoke executable should link that library instead of each carrying their own common source list. Native test and coverage wrappers should resolve an installed Dawn checkout, pass it through to CMake as `OFG_DAWN_SOURCE_DIR`, reject repository-local toolchain paths, and fail clearly with setup guidance if no installed checkout can be found.

After the library/linkage cleanup, add the shared game-frame files and wire them into the shared library:

    cpp/include/ofg/game/gpu_context.hpp
    cpp/include/ofg/game/render_target.hpp
    cpp/include/ofg/game/game.hpp
    cpp/src/game/game.cpp

The exact paths may adjust if implementation finds a better local naming pattern, but the new shared files must be compiled into browser WASM, native render smoke, and the doctest executable when the tests cover them. Do not introduce a CPU-only/GPU-bound library split just to make tests link. The required target shape is one WebGPU-capable shared OFG C++ library, one doctest executable, one browser WASM executable, and one native render-smoke executable.

Add or update C++ doctest coverage where native-checkable behavior exists:

    cpp/tests/game_runtime_test.cpp

The test should cover resize validation, finite-time tick/frame count behavior, debug status serialization, render-target validation that does not need real GPU execution, and disposed/error behavior after `Game` absorbs current `BrowserRuntime` responsibilities. It is acceptable for this test target to link WebGPU headers/libraries if the tested interface uses `WGPU*` types.

Update `tools/cpp-coverage.mjs` so any new native-checkable implementation files under `cpp/src/game` or any moved runtime implementation files are included in the coverage attention gate. If an implementation file is intentionally smoke-only, record the exception in this plan and the coverage docs before relying on it.

Milestone 1 validation:

    npm run test:cpp
    npm run coverage:cpp
    npm run build:wasm
    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2 validation:

    npm run test:cpp
    npm run coverage:cpp
    npm run build:wasm
    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render
    $sharedPaths = @("cpp/include/ofg/game", "cpp/src/game", "cpp/include/ofg/render", "cpp/src/render"); foreach ($path in $sharedPaths) { if (!(Test-Path $path)) { throw "Missing shared path: $path" } }; $matches = rg -n "wgpuQueueSubmit|wgpuCommandEncoderFinish|wgpuCommandEncoderCopyTextureToBuffer|wgpuBufferMapAsync|wgpuSurface" $sharedPaths; if ($matches) { $matches; throw "Shared game/render code contains platform frame-driver work." } else { "Shared game/render code has no platform frame-driver calls." }
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 3 validation:

    npm run smoke:browser:cpp
    npm run smoke:browser
    $sharedPaths = @("cpp/include/ofg/game", "cpp/src/game", "cpp/include/ofg/render", "cpp/src/render"); foreach ($path in $sharedPaths) { if (!(Test-Path $path)) { throw "Missing shared path: $path" } }; $matches = rg -n "wgpuQueueSubmit|wgpuCommandEncoderFinish|wgpuCommandEncoderCopyTextureToBuffer|wgpuBufferMapAsync|wgpuSurface" $sharedPaths; if ($matches) { $matches; throw "Shared game/render code contains platform frame-driver work." } else { "Shared game/render code has no platform frame-driver calls." }
    $matches = rg -n "BootstrapRenderer|render_to_view|renderer_" cpp/include/ofg/web cpp/src/web; if ($matches) { $matches; throw "Browser platform path still owns the bootstrap renderer directly." } else { "Browser platform path delegates renderer ownership." }
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 4 validation:

    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render
    $sharedPaths = @("cpp/include/ofg/game", "cpp/src/game", "cpp/include/ofg/render", "cpp/src/render"); foreach ($path in $sharedPaths) { if (!(Test-Path $path)) { throw "Missing shared path: $path" } }; $matches = rg -n "wgpuQueueSubmit|wgpuCommandEncoderFinish|wgpuCommandEncoderCopyTextureToBuffer|wgpuBufferMapAsync|wgpuSurface" $sharedPaths; if ($matches) { $matches; throw "Shared game/render code contains platform frame-driver work." } else { "Shared game/render code has no platform frame-driver calls." }
    $matches = rg -n "BootstrapRenderer|render_to_view" cpp/include/ofg/native cpp/src/native; if ($matches) { $matches; throw "Native smoke still owns the bootstrap renderer directly." } else { "Native smoke delegates renderer ownership." }
    $submitMatches = rg -n "wgpuQueueSubmit" cpp/include/ofg/web cpp/src/web cpp/include/ofg/native cpp/src/native; $submitMatches; if (($submitMatches | Measure-Object).Count -gt 2) { throw "Expected at most one ordinary frame submit site per browser/native frame driver." }
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 5 validation:

    npm test
    npm run coverage
    npm run smoke:browser:cpp
    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke
    $sharedPaths = @("cpp/include/ofg/game", "cpp/src/game", "cpp/include/ofg/render", "cpp/src/render"); foreach ($path in $sharedPaths) { if (!(Test-Path $path)) { throw "Missing shared path: $path" } }; $matches = rg -n "wgpuQueueSubmit|wgpuCommandEncoderFinish|wgpuCommandEncoderCopyTextureToBuffer|wgpuBufferMapAsync|wgpuSurface" $sharedPaths; if ($matches) { $matches; throw "Shared game/render code contains platform frame-driver work." } else { "Shared game/render code has no platform frame-driver calls." }
    $matches = rg -n "__EMSCRIPTEN__|emscripten|WGPUEmscripten|dawn::|wgpuInstanceWaitAny" cpp/include/ofg/game cpp/src/game cpp/include/ofg/render cpp/src/render; if ($matches) { $matches; throw "Shared game/render code contains platform integration terms." } else { "No shared platform integration terms found." }
    $matches = rg -n "OpaqueRenderer::render_to_view|bool render_to_view\(" docs/plans/cpp-renderer-resources-pipeline-plan.md; if ($matches) { $matches; throw "Renderer-resource plan still names stale top-level render boundary." } else { "Renderer-resource plan boundary is aligned." }
    git -c safe.directory=C:/dev/ofg diff --check

For the `rg` validation commands, expected results are no matches in shared game/render code unless the plan records an explicit exception. Generic `webgpu.h` type usage is allowed in shared WebGPU-capable code. Browser-only terms should stay under `cpp/src/web` or browser-specific headers. Dawn wait/native integration terms should stay under `cpp/src/native`. Shared enum label helpers may mention backend names, but native backend selection must not move into shared `Game` code.

Final validation:

    npm test
    npm run smoke:browser:cpp
    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke
    npm run coverage
    npm run build:cloudflare
    git -c safe.directory=C:/dev/ofg diff --check

For browser or visual work, keep a dev server available for human review in a separate terminal/session:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, use the alternate URL printed by the tool. Capture and present a browser screenshot after Milestone 3 switches the browser path to `Game`, the native PNG/report after Milestone 4 switches native smoke to `Game`, and final browser/native visual artifacts before completing the plan.

## Milestone Review

After each milestone:

1. Update any changed API contracts, systems docs, and this ExecPlan.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Milestone 1 is accepted when `cpp/CMakeLists.txt` has one WebGPU-capable shared OFG C++ library linked by the doctest executable, browser WASM executable, and native render-smoke executable. `tools/test-cpp.mjs` and `tools/cpp-coverage.mjs` must configure the native WebGPU/Dawn dependencies needed by that target, and `npm run test:cpp`, `npm run coverage:cpp`, `npm run build:wasm`, and native render smoke must pass.

Milestone 2 is accepted when the doctest executable, browser WASM target, and native smoke target can all link the new shared `Game` code through the shared library. Native C++ tests should cover shared lifecycle/status behavior and render-target validation where possible. The external browser visual output may still be driven through old browser code until Milestone 3, but the shared `Game` interface must compile for WASM, native tests, and native smoke builds.

Milestone 3 is accepted when `BrowserGame` no longer owns `BootstrapRenderer` directly. It owns browser WebGPU setup, surface configuration, target acquisition, encoder creation, finish, submit, and handle release. It delegates frame state and render command recording to `Game`, applies pending size state to `Game`, destroys `Game` before releasing device-owned platform handles, and keeps recoverable browser surface states from corrupting durable `Game` resources. `npm run smoke:browser:cpp` and `npm run smoke:browser` must pass and produce the same triangle visual contract.

Milestone 4 is accepted when native render smoke no longer owns `BootstrapRenderer` directly. It owns Dawn setup, offscreen target creation, readback copy, PNG/report writing, and smoke threshold inspection. It delegates frame state and render command recording to `Game`. `npm run smoke:render` must pass and write `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `report.json`.

Milestone 5 is accepted when active docs and the renderer-resource plan describe the new boundary. Shared game/render code must not contain browser-only Emscripten surface code, command-buffer finish/submit work, readback work, or Dawn native-wait/integration code. `Game::render` is the shared render-entry method name used by future renderer work, the renderer-resource plan no longer directs implementation toward `OpaqueRenderer::render_to_view` as the top-level frame boundary, the full validation suite passes, coverage confirms modified native-checkable implementation files are absent from the filtered coverage attention report or have explicit exceptions, `docs/coverage` is refreshed if coverage summaries change, and visual artifacts show the unchanged triangle in both browser and native outputs.

Visual acceptance:

The rendered image should remain a dark blue-gray background with the red/green/blue bootstrap triangle until the later renderer-resource plan changes the scene. Browser screenshots should live under `C:\dev\ofg\artifacts\browser-smoke` or `C:\dev\ofg\artifacts\browser-smoke-cpp`. Native screenshots should live under `C:\dev\ofg\artifacts\render-smoke`.

Submission acceptance:

`Game` and shared renderer/resource modules must not call `wgpuQueueSubmit`. The browser frame driver and native smoke frame driver may each submit once for the ordinary frame. Native smoke may include render and readback copy commands in the same submitted command buffer.

Render acceptance:

`Game::render` must validate that the command encoder and target view are non-null, that `RenderTarget::format` matches the color format used to create the renderer, and that target dimensions match the most recent accepted nonzero resize. Ordinary `Game::render` calls must not recreate durable resources, call `wgpuQueueWrite*`, or hide per-draw resource churn. Uploads should happen during creation, resize, explicit asset mutation, or a named prepare phase with counters that make per-frame work visible.

## Idempotence and Recovery

This transition should be additive until each platform path is switched. Add `Game` first, wire browser second, wire native third. If a platform refactor fails, revert only the current platform integration while preserving the shared `Game` code if it still passes tests.

Generated directories `C:\dev\ofg\dist`, `C:\dev\ofg\dist-test`, `C:\dev\ofg\.deploy`, `C:\dev\ofg\artifacts`, and `C:\dev\ofg\assets\wasm\ofg_cpp` can be regenerated by the existing npm scripts. Do not treat generated files as source of truth.

If browser WebGPU is unavailable locally, record the environment failure and preserve artifacts. Do not weaken browser smoke expectations for real rendering failures.

If native Dawn smoke fails because no Vulkan adapter is available, record the adapter/environment error. Do not replace the native PNG with a fake image or a null-backend pass.

If implementation reveals that `Game` must support device replacement in place, pause and revise this plan. The current intended recovery model for device loss is to destroy the device-bound `Game`, request a new device, and create a new `Game`.

This plan does not solve the existing browser frame-loop overhead from per-frame layout polling, debug-status JSON parsing, and DOM status writes. Avoid adding new per-frame status reads or larger debug payloads in this refactor. Keep the existing renderer-resource plan TODOs for throttling/debug-gating status updates and dirty-tracking resize.

## Artifacts and Notes

Expected durable implementation artifacts:

    C:\dev\ofg\cpp\include\ofg\game\gpu_context.hpp
    C:\dev\ofg\cpp\include\ofg\game\render_target.hpp
    C:\dev\ofg\cpp\include\ofg\game\game.hpp
    C:\dev\ofg\cpp\include\ofg\game\game_runtime.hpp
    C:\dev\ofg\cpp\src\game\game.cpp
    C:\dev\ofg\cpp\src\game\game_runtime.cpp
    C:\dev\ofg\cpp\src\game\render_target.cpp
    C:\dev\ofg\cpp\tests\game_runtime_test.cpp

Likely edited integration files. Touch these when needed for the milestone; do not churn them just because they are listed here:

    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\include\ofg\runtime\browser_runtime.hpp
    C:\dev\ofg\cpp\src\runtime\browser_runtime.cpp
    C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
    C:\dev\ofg\cpp\src\web\browser_game.cpp
    C:\dev\ofg\cpp\include\ofg\native\render_smoke.hpp
    C:\dev\ofg\cpp\src\native\render_smoke.cpp
    C:\dev\ofg\cpp\tests\browser_runtime_test.cpp
    C:\dev\ofg\tools\cpp-coverage.mjs
    C:\dev\ofg\COVERAGE.md
    C:\dev\ofg\docs\coverage\latest.md
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md
    C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md

Expected visual artifacts:

    C:\dev\ofg\artifacts\browser-smoke\bootstrap.png
    C:\dev\ofg\artifacts\browser-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json
    C:\dev\ofg\artifacts\render-smoke\bootstrap.png
    C:\dev\ofg\artifacts\render-smoke\report.json

Record concise command transcripts and screenshot/report paths here as milestones complete.

Preflight macro cleanup validation:

    `rg -n "OFG_ENABLE_WEBGPU_RENDERER" cpp -S` returned no matches.
    `npm run test:cpp` passed.
    `npm run build:wasm` passed.
    `npm run smoke:browser:cpp` passed and wrote `C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png`.
    `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png`.

Milestone 1 library/linkage cleanup validation:

    `node --check tools\test-cpp.mjs; node --check tools\cpp-coverage.mjs; node --check tools\smoke-render-cpp.mjs; node --check tools\lib\toolchain.mjs` passed.
    `npm run test:cpp` passed. The build compiled `src/render/bootstrap_renderer.cpp` and `src/render/webgpu_common.cpp` into `ofg_cpp.lib`, linked `ofg_cpp_tests.exe` against `ofg_cpp`, and ran one doctest CTest target successfully.
    `npm run coverage:cpp` passed. The filtered coverage report remained 100.00% for `cpp/src/core/frame_state.cpp`, `cpp/src/render/bootstrap_scene.cpp`, `cpp/src/runtime/browser_runtime.cpp`, and `cpp/src/runtime/runtime_debug_status.cpp`.
    `npm run build:wasm` passed. The build compiled the renderer sources into `libofg_cpp.a` and linked `C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js`.
    `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`; the report has `passed: true`.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with Git LF/CRLF warnings only.

Milestone review:

    Scope: Milestone 1 CMake target graph, native test/coverage/smoke wrappers, Dawn resolver helper, and renderer-resource plan alignment.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were available but not used because the tool policy allows spawning only when the user explicitly requests delegated/sub-agent work.
    Required findings fixed: reject repository-local Dawn/toolchain paths in `resolveDawnSourceDir`; update stale renderer-resource wording from the old portable core/library and `render_to_view` model to the shared `Game::render` boundary.
    Follow-ups recorded: none for this milestone.
    Rejected findings: none.
    Validation rerun: node syntax checks, `npm run test:cpp`, `npm run coverage:cpp`, `npm run build:wasm`, native `npm run smoke:render`, renderer-resource boundary grep, and `git diff --check`.
    Remaining risk: native test and coverage now link the Dawn runtime after the shared `Game` symbols entered `ofg_cpp`, so clean native test/coverage builds take about five minutes on this machine. This is accepted for now because it keeps the target graph simple and avoids CPU/GPU library splitting.

Milestone 2 shared `Game` layer validation:

    `npm run test:cpp` passed after adding recovery tests for `GameRuntime` and `BrowserRuntime`.
    `npm run coverage:cpp` passed. Filtered line coverage was `cpp/src/core/frame_state.cpp` 100.00%, `cpp/src/game/game_runtime.cpp` 98.50%, `cpp/src/game/render_target.cpp` 100.00%, `cpp/src/render/bootstrap_scene.cpp` 100.00%, `cpp/src/runtime/browser_runtime.cpp` 100.00%, and `cpp/src/runtime/runtime_debug_status.cpp` 100.00%.
    `npm run build:wasm` passed and linked `C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js` with the new shared `Game` sources.
    `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`; the report has `passed: true`.
    `npm run smoke:browser:cpp` was run as an extra transitional-browser sanity check and passed, writing `C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png` and `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.
    The shared-code frame-driver grep passed: no `wgpuQueueSubmit`, `wgpuCommandEncoderFinish`, readback copy/map, or surface calls exist under `cpp/include/ofg/game`, `cpp/src/game`, `cpp/include/ofg/render`, or `cpp/src/render`.
    The shared platform-term grep passed: no Emscripten, Dawn native integration, or native wait terms exist under shared game/render paths.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with Git LF/CRLF warnings only.

Milestone 2 review:

    Scope: shared `GpuContext`, `RenderTarget`, `GameRuntime`, `Game`, transitional `BrowserRuntime` wrapper, C++ tests, coverage exception documentation, and CMake linkage for the shared game sources.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. `docs/ARCHITECTURE.md` was requested by the skill but does not exist in this repo, so `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `PLANS.md`, and this ExecPlan were used as the active contract baseline. Sub-agent tools were not used because the tool policy allows spawning only when the user explicitly requests delegated/sub-agent work.
    Required findings fixed: split recoverable `GameRuntime::mark_error` from GPU/device `mark_gpu_error`; make target-configuration marking idempotent; have `Game::render` restore initialized status on valid render; add tests proving browser resize parse errors and recoverable render/runtime errors do not discard readiness.
    Follow-ups recorded: `BrowserRuntime` remains transitional until Milestone 3 removes its remaining browser-named shared-state role.
    Rejected findings: none.
    Validation rerun: `npm run test:cpp`, `npm run coverage:cpp`, `npm run build:wasm`, native `npm run smoke:render`, extra `npm run smoke:browser:cpp`, shared frame-driver grep, shared platform-term grep, and `git diff --check`.
    Remaining risk: `cpp/src/game/game.cpp` owns device-bound renderer creation and command encoding, so it is documented in `COVERAGE.md` and this plan as build/smoke-covered until browser/native frame drivers call `Game` directly in Milestones 3 and 4. Native test/coverage clean builds are also slower because doctests now link the Dawn runtime.

Milestone 3 browser delegation validation:

    `npm run build:wasm` passed with `cpp/src/web/browser_game.cpp` linking against shared `Game`.
    `npm run smoke:browser:cpp` passed and wrote `C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png` plus `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`. The report kept `backend: "BrowserWebGpu"`, adapter name `intel`, one pipeline, one buffer, and resize/recovery `surfaceConfigureCount` values 1, 2, and 3.
    `npm run smoke:browser` passed and wrote `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\browser-smoke\report.json`; the report has the expected triangle ratio, background ratio, and `lastError: null`.
    `npm run test:cpp` passed after `BrowserRuntime` was removed from CMake.
    `npm run coverage:cpp` initially failed because deleting `browser_runtime_test.cpp` removed coverage of shared runtime branches. After moving those cases into `game_runtime_test.cpp`, `npm run coverage:cpp` passed with `cpp/src/game/game_runtime.cpp`, `cpp/src/game/render_target.cpp`, `cpp/src/core/frame_state.cpp`, `cpp/src/render/bootstrap_scene.cpp`, and `cpp/src/runtime/runtime_debug_status.cpp` all at 100.00%.
    `docs/coverage/cpp-summary.json` and `docs/coverage/latest.md` were refreshed for the changed C++ coverage gate.
    The shared-code frame-driver grep passed: no `wgpuQueueSubmit`, `wgpuCommandEncoderFinish`, readback copy/map, or surface calls exist under shared game/render paths.
    The browser ownership grep passed: no `BootstrapRenderer`, `render_to_view`, or `renderer_` references remain under `cpp/include/ofg/web` or `cpp/src/web`.
    Live source and active docs contain no `BrowserRuntime` or `browser_runtime` references outside archived/history coverage artifacts.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with Git LF/CRLF warnings only.
    `npm run dev` is running at `http://127.0.0.1:5173`; its log is `C:\dev\ofg\artifacts\dev-server.log`.

Milestone 3 review:

    Scope: browser `BrowserGame` delegation to shared `Game`, removal of `BrowserRuntime`, CMake/coverage updates, active systems and coverage docs, browser smoke artifacts, and shared-code ownership greps.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because the tool policy allows spawning only when the user explicitly requests delegated/sub-agent work.
    Required findings fixed: move lost `BrowserRuntime` coverage cases into `GameRuntime` tests; update `docs/SYSTEMS.md`, `docs/coverage/latest.md`, `docs/plans/cpp-renderer-resources-pipeline-plan.md`, and `COVERAGE.md` so active docs no longer point at the retired wrapper.
    Follow-ups recorded: `cpp/src/web/browser_game.cpp` is 683 lines and should be split before much more browser behavior is added; device-lost callback userdata lifetime and a true device/Game recreate path remain deferred in the renderer-resource plan.
    Rejected findings: none.
    Validation rerun: `npm run build:wasm`, `npm run smoke:browser:cpp`, `npm run smoke:browser`, `npm run test:cpp`, `npm run coverage:cpp`, shared frame-driver grep, browser ownership grep, live `BrowserRuntime` grep, and `git diff --check`.
    Remaining risk: actual device-loss recovery is still mostly diagnostic rather than a full recreate flow. Timeout and outdated surface acquisitions now stay recoverable without destroying `Game`, but lost/error states still need the later recreate hardening recorded in `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md`.

Milestone 4 native delegation validation:

    `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed after `C:\dev\ofg\cpp\src\native\render_smoke.cpp` switched from direct `BootstrapRenderer` use to shared `Game`.
    The smoke wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report has `passed: true`, `backend: "Vulkan"`, adapter `NVIDIA GeForce RTX 3050 Ti Laptop GPU`, `triangleRatio: 0.230112`, `backgroundRatio: 0.769888`, and `nonBackgroundColorBuckets: 28`.
    Visual inspection of `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` showed the expected dark blue-gray background and red/green/blue bootstrap triangle.
    The shared-code frame-driver grep passed: no `wgpuQueueSubmit`, `wgpuCommandEncoderFinish`, readback copy/map, or surface calls exist under `cpp/include/ofg/game`, `cpp/src/game`, `cpp/include/ofg/render`, or `cpp/src/render`.
    The native ownership grep passed: no `BootstrapRenderer` or `render_to_view` references remain under `cpp/include/ofg/native` or `cpp/src/native`.
    The submit-site grep found only `cpp/src/web/browser_game.cpp` and `cpp/src/native/render_smoke.cpp`, one ordinary submit site for each platform frame driver.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with Git LF/CRLF warnings only.

Milestone 4 review:

    Scope: native Dawn smoke delegation to shared `Game`, native smoke visual/report artifacts, `docs/SYSTEMS.md` ownership wording, shared/native frame-driver greps, and one-submit validation.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because the tool policy allows spawning only when the user explicitly requests delegated/sub-agent work.
    Required findings fixed: update stale `docs/SYSTEMS.md` and native file-header wording that still described native smoke as rendering `BootstrapRenderer` directly.
    Follow-ups recorded: `cpp/src/native/render_smoke.cpp` is 925 lines and should be split before the next native-smoke expansion; `cpp/src/web/browser_game.cpp` remains in the 500-1000 line concern band.
    Rejected findings: none.
    Validation rerun: native direct-renderer wording grep, native ownership grep, submit-site grep, and `git diff --check`.
    Remaining risk: native smoke now uses `Game`, but the native harness still contains Dawn setup, readback, report writing, and argument parsing in one 925-line file. This is accepted for this milestone because no new native behavior was added beyond the handoff, but it should not keep growing unchecked.

Milestone 5 final validation:

    `npm test` passed. C++ doctests ran one CTest target, `ofg_cpp_tests`, and TypeScript Mocha reported 19 passing tests.
    `npm run coverage` passed. C++ checked files were `cpp/src/core/frame_state.cpp` 100.00%, `cpp/src/game/game_runtime.cpp` 100.00%, `cpp/src/game/render_target.cpp` 100.00%, `cpp/src/render/bootstrap_scene.cpp` 100.00%, and `cpp/src/runtime/runtime_debug_status.cpp` 100.00%. TypeScript checked files met the per-file gate; total line coverage was 82.76% with `src/app/main.ts` documented as the browser-smoke exception.
    Coverage summary hashes matched between generated artifacts and committed docs for `artifacts/coverage/cpp/cpp-summary.json` / `docs/coverage/cpp-summary.json` and `artifacts/coverage/ts/coverage-summary.json` / `docs/coverage/ts-coverage-summary.json`.
    `npm run smoke:browser:cpp` passed and wrote `C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png` plus `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`. The report kept `backend: "BrowserWebGpu"`, one pipeline, one buffer, recoverable zero-size status, recovered initialized status, and `lastError: null`.
    `npm run smoke:browser` passed and wrote `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\browser-smoke\report.json`. The report had `triangleRatio: 0.2301123595505618`, `backgroundRatio: 0.7698876404494382`, 28 non-background color buckets, one pipeline, one buffer, and `lastError: null`.
    `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`. The report had `passed: true`, `backend: "Vulkan"`, `triangleRatio: 0.230112`, `backgroundRatio: 0.769888`, and 28 non-background color buckets.
    Visual inspection of `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png`, `C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png`, and `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` showed the unchanged dark blue-gray background and red/green/blue bootstrap triangle.
    Active source/docs sweeps passed: `OFG_ENABLE_WEBGPU_RENDERER` is absent outside this historical plan, no live/current-doc `BrowserRuntime` ownership remains, shared game/render code contains no frame-driver calls, and browser/native frame drivers no longer reference `BootstrapRenderer`, `render_to_view`, or `renderer_` directly.
    The submit-site grep found only `cpp/src/web/browser_game.cpp` and `cpp/src/native/render_smoke.cpp`.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with Git LF/CRLF warnings only.
    The local dev server was reachable at `http://127.0.0.1:5173`.

Milestone 5 review:

    Scope: final docs/contracts, coverage records, renderer-resource plan alignment, full validation evidence, visual smoke artifacts, stale-symbol greps, and remaining file-size risks.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because the tool policy allows spawning only when the user explicitly requests delegated/sub-agent work.
    Required findings fixed: refine `docs/API_CONTRACTS.md`, `COVERAGE.md`, `docs/coverage/latest.md`, `docs/SYSTEMS.md`, and this plan so they describe `Game` command recording separately from platform finish/submit, and so `bootstrap_renderer.cpp` is no longer described as browser-only.
    Follow-ups recorded: split `cpp/src/web/browser_game.cpp` before much more browser behavior; split `cpp/src/native/render_smoke.cpp` before the next native-smoke expansion; keep device-loss/recovery and status-preservation hardening in `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md`.
    Rejected findings: none.
    Validation rerun: active stale-term greps, removed-macro grep, shared frame-driver grep, browser/native direct-renderer grep, submit-site grep, `npm test`, `npm run coverage`, `npm run smoke:browser:cpp`, `npm run smoke:browser`, native `npm run smoke:render`, coverage summary hash comparison, visual artifact inspection, dev-server reachability check, and `git diff --check`.
    Remaining risk: the final architecture is intentionally still a bootstrap triangle renderer behind `Game`; the next renderer-resource plan must replace that internal renderer without expanding browser/native frame-driver ownership again. Clean native test and coverage builds remain slow because doctests link Dawn.

## Interfaces and Dependencies

The final names may adjust during implementation, but the public shape should remain close to this:

    namespace ofg {

    struct GpuContext {
      WGPUDevice device = nullptr;
      WGPUQueue queue = nullptr;
      std::string adapter_name = "Unavailable";
      std::string backend = "SharedGame";
    };

    struct RenderTarget {
      WGPUTextureView view = nullptr;
      WGPUTextureFormat format = WGPUTextureFormat_Undefined;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
    };

    class Game {
    public:
      Game(const Game&) = delete;
      Game& operator=(const Game&) = delete;
      Game(Game&&) = delete;
      Game& operator=(Game&&) = delete;
      ~Game();

      static std::unique_ptr<Game> create(
        GpuContext gpu,
        WGPUTextureFormat color_format,
        std::string& error
      );

      bool resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio, std::string& error);
      bool tick(double time_ms, std::string& error);
      bool render(WGPUCommandEncoder encoder, RenderTarget target, std::string& error);
      bool record_error(std::string message);
      bool record_gpu_error(std::string message);
      std::string debug_status_json() const;
      const RuntimeDebugStatus& status() const noexcept;
      void dispose();
    };

    } // namespace ofg

`GpuContext` is passed at creation because the device/queue are stable for the `Game` lifetime. If later asset mutation methods need queue writes after construction, they should either use the stored context inside `Game` or receive an explicit mutation context at the point of mutation. Ordinary `resize`, `tick`, and `render` should not receive `GpuContext`.

`RenderTarget` is passed to `render` because it changes per frame in the browser and differs by platform. Browser supplies a view created from the current surface texture. Native smoke supplies a view created from an offscreen texture.

`Game::tick` is the shared per-frame state advance. For this transition it should preserve the current accepted-frame behavior: validate finite time, increment the frame counter on accepted ticks, and report disposed/runtime errors clearly. This plan does not introduce frame pacing, fixed-step simulation, vsync policy, or server simulation timing.

`Game::render` records render commands only. It must validate the target view, target format, and target size before recording. It must not finish the command encoder, submit the queue, present a surface, copy pixels for readback, or write artifacts.

The browser frame driver should follow this shape:

    process WebGPU events
    game.tick(time_ms)
    configure surface if needed
    acquire current surface texture
    create texture view
    create command encoder
    game.render(encoder, render_target)
    finish command encoder
    submit one command buffer
    release per-frame handles
    destroy Game before releasing device-owned platform handles during teardown

The native smoke frame driver should follow this shape:

    create Dawn device and queue
    create Game
    create offscreen texture, target view, and readback buffer
    game.tick(deterministic_time_ms)
    create command encoder
    game.render(encoder, render_target)
    copy offscreen texture to readback buffer
    finish command encoder
    submit one command buffer
    map readback buffer
    write PNG and report
    destroy Game before releasing native device-owned handles

The TypeScript dependency surface should not change. `C:\dev\ofg\src\app\wasmRuntime.ts` should continue to expose `BrowserGameRuntime` with `resize`, `frame`, `debugStatus`, and `dispose`.

## Revision Notes

2026-06-21: Updated after the second `review-plan` pass and user feedback. Library/linkage cleanup is now Milestone 1, the plan requires one WebGPU-capable shared C++ library linked by tests, browser WASM, and native smoke, native test/coverage tooling must configure Dawn/WebGPU when needed, the renderer-resource alignment check was narrowed to stale top-level render boundaries, and browser resize/surface recovery plus `Game::render` validation rules were made explicit.
