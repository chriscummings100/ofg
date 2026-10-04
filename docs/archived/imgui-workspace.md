# Add the dockable ImGui laboratory workspace

This completed ExecPlan follows [PLANS.md](../../PLANS.md). Implementation was authorized on 2026-10-04 in the managed imgui-workspace worktree and merged into main at the user's request. Status: complete.

## Purpose / Big Picture

Provide a native D3D12 and browser WebGPU workspace with a menu bar, dockable Scene viewport, read-only Scene Hierarchy, and editable Render Settings. Hierarchy starts left, scene centre, settings right. Docking stays inside the application. Persist layout/visibility locally; fixture values reset on launch. Right mouse capture operates only over the scene; Escape releases capture without closing the application.

## Progress

- [x] (2026-10-04) Inspected frame flow, scene ownership, lighting, platform input and build workflows; user selected viewport docking, read-only inspection, output/lighting controls, layout persistence and held-right-button camera on both platforms.
- [x] (2026-10-04) Created managed worktree imgui-workspace from clean main HEAD; initialized pinned RHI.
- [x] (2026-10-04) Shared ImGui renderer and platform input verified on native D3D12 and browser WebGPU.
- [x] (2026-10-04) Docking, scene viewport, capture and persistence pass browser interaction checks; native window interaction and persistence inspected.
- [x] (2026-10-04) Hierarchy/settings panels, CPU/native/browser regressions, screenshots, formatting and documentation complete.
- [x] (2026-10-04) Merged commit 89e102b into main, preserving the existing PBR naming edits as uncommitted work. The only restore conflict was the appended development notes; both sections were retained.

## Surprises & Discoveries

2026-10-04: Closing the browser scene panel exposed odd 16-bit index counts. The pinned RHI WebGPU initial-buffer upload requires a byte count divisible by four; the unpadded upload threw a JS OperationError and cascading Asyncify stack errors. Padding index storage only preserves draw counts/offsets. The final native GPU odd-index fixture and browser close/reopen interaction both pass.

2026-10-04: Initial native black output was reproduced by the GPU test. ShaderCursor must dereference a constant-buffer field (ui.scale/ui.offset); setting data on its container writes the wrong storage. Corrected and covered by GPU checks for clipping, 16-bit base-vertex overflow, alpha and gamma. Browser testing waits for the completed link: probing while output is being replaced can pair incompatible JS/WASM files.

2026-10-04: ImGui gesture automation requires completed frames between hover, press and dragging, and between modifier/text-entry events. Event bursts initially missed the intended action. Waiting on the application's frame counter fixes the test; there are no arbitrary timing sleeps in the smoke. The final test asserts an inactive Scene viewport after tabbing Render Settings into its dock.

2026-10-04 merge verification: the PBR smoke's resize screenshot occasionally captured the cleared canvas before the next Asyncify frame completed. Its capture helper only required three frames since startup. It now additionally waits for three fresh completed frames per capture; all pixel assertions are unchanged, and the rerun passes.

Browser canvas textures expire across Asyncify yields. Finish all scene/UI rendering before acquisition, then present with a resource-only shader. PBR already renders display-linear output into float targets. Entities have IDs and lifetime-aware Ptr observers, but no names yet. Main was clean at implementation start; prior PBR work is committed.

## Decision Log

2026-10-04: Pin ImGui v1.92.9b-docking (tag object 9acdfbf46810c0c74ab281ce04122c4149ae8bd1, peeled commit b48d1afbe8ee8b238e2961dc363a949dd7304e23); retain the RHI pin. Use upstream GLFW input natively, browser event adaptation, and one shared RHI renderer. No separate OS windows, generic editor framework, transform editing, picking, light creation or material editing.

2026-10-04: Compose the scene and UI in display-linear RGBA16F, decode UI vertex colours, and encode once at presentation. Fresh immutable frame buffers avoid unsafe in-flight overwrite; no speculative ring-buffer optimization.

## Outcomes & Retrospective

The menu and three dockable panels run on both targets. Named hierarchy selection uses existing observer lifetimes, render edits commit validated lighting drafts, and layout/visibility survive restart without saving scene values. Both targets share one concrete RHI renderer. Existing no-UI diagnostics remain available. Native/CPU CTest and all three browser smoke suites pass.

Remaining limits: native held-RMB fly/focus transitions were reviewed in code but not exhaustively replayed with the native automation tool; browser capture/release/Escape and synthetic focus loss are exercised. Multi-monitor DPI transitions, long-running allocation/residency and performance are not claimed. Emscripten's existing Asyncify/WASM-exceptions warning remains. Separate OS viewports, entity editing and terrain tools are outside scope.

## Contract and Quality Baseline

Hosts own platform input and one UI context. Shared workspace owns panels, selection and offscreen targets; shared renderer owns UI pipelines and font textures. Core remains independent of graphics/UI dependencies. Entity names are owned strings, optional and nonunique. Ptr selection nulls on scene destruction. Lighting remains authoritative, invalid edits preserve last valid values. UI persistence contains layout and visibility only.

Follow project formatting and purpose/function comments. Test behavioral boundaries rather than widget implementation. No numerical coverage threshold is introduced: native GPU and browser interaction checks cover platform/render glue; CPU tests cover new data contracts. Record skipped checks honestly.

## Context and Orientation

Game updates resources/state/scene and renders through Graphics. Native uses GLFW, browser uses HTML canvas and Emscripten callbacks. Scene owns entities and Lighting. The workspace sits between host input and Game::frame; the original no-UI paths and checkerboard remain available as diagnostics.

## Plan of Work

1. Add pinned ImGui and a small ofg-ui target, a direct RHI draw-data renderer, font texture lifecycle, output encoding and platform event integration. Prove font/textured triangles, clipping, offsets, DPI and colour.
2. Add the dockspace/menu/Scene image, physical-pixel sizing, application input routing and persisted layout. Hidden scene panels continue update-only frames. Verify docking, close/reopen, resize, focus and capture.
3. Add entity names, observer-based hierarchy selection/read-only detail and existing output/light controls. Validate settings, reset fixture values, regression suites, evidence and docs.

## Concrete Steps

Work directory: C:\Users\Chris\.codex\worktrees\imgui-workspace\ofg. Use the x64 developer environment and Emscripten setup in DEVELOPING.md. The following commands were run successfully:

```powershell
cmake --preset native-debug
cmake --build --preset native-debug --parallel 8
ctest --preset native-debug
cmake --preset cpu-tests
cmake --build --preset cpu-tests
ctest --preset cpu-tests
cmake --preset web
cmake --build --preset web --parallel 8
npm.cmd ci
node tools/imgui-smoke.mjs
node tools/pbr-smoke.mjs
node tools/browser-smoke.mjs
```

Changed C++ was formatted with clang-format 22.1.3 and checked with --dry-run --Werror; git diff --check also passes. A worktree preview server runs at loopback port 8091 during review; the ordinary reusable interface remains npm run serve:web.

## Milestone Review

2026-10-04: Reviewed context/callback ownership, queue-retained uploads/textures, offscreen resize, output transfer, panel identity, lighting validation, capture boundaries, persistence and failure diagnostics. Kept direct RHI calls and concrete Workspace/ImGuiRenderer/BrowserInput types; added no editor registry or virtual graphics API. Fixed the constant-buffer binding and WebGPU alignment issues with regressions. Removed the temporary probe script, preserved dependency licenses/pins, and updated usage/build/architecture documentation. CPU-only configuration remains free of ImGui and graphics dependencies.

## Validation and Acceptance

Native GPU tests exercise fonts, texture switching, clipping, alpha, nonzero index/vertex offsets, display transfer and retained submissions. CPU tests exercise naming and invalid lighting edits. Browser smoke exercises rendered panels, menus, docking/tabbing, close/reopen, layout/reset/reload, hierarchy selection, live settings, camera capture and UI isolation, resize/DPI/focus and diagnostics. Inspect native and browser screenshots under artifacts/imgui. Run existing CTest and browser regression suites with explicit no-UI flags where full-canvas pixels are required. No performance claim without measurement.

## Idempotence and Recovery

Work only in this managed worktree. Do not reset the main checkout or historical repository. Reuse configured build trees; keep dependency pins fixed. Missing layout uses defaults, storage failures log and leave the session usable. Shutdown destroys platform callbacks before UI context and drains native GPU work before graphics teardown.

## Artifacts and Notes

Sources: Dear ImGui docking/backend documentation; SGL ui.cpp and imgui.slang at bdbc9f3f809c5f6eda7e61e6eecae787db3f9620 informed the design. No SGL source was copied. ImGui upstream sources/license remain unmodified in the submodule.

Evidence from 2026-10-04:

- Native D3D12 on NVIDIA GeForce RTX 3050 Ti Laptop GPU: all four CTest targets passed. Core: 67 cases/1636 assertions; checkerboard: 1 case/22 assertions; scene/PBR/textures/UI: 21 cases/1044 assertions. Logs: artifacts/imgui/native/ctest.log and build/native/Testing/Temporary/LastTest.log. Independent cpu-tests has the same 67 cases/1636 assertions.
- Native visual inspection: scene/panels, selection, exposure/reset, maximize, minimize/restore, clean exit/relaunch and persisted layout, Escape remains open. Captures: artifacts/imgui/native/workspace.png and restarted.png.
- WebGPU in Chrome 154.0.8037.95: UI smoke passes with zero unexpected warnings/errors. Captures include workspace, exposure, invalid-edit, normals, scene-hidden, floating-settings, docking-preview, tabbed-settings, reset-layout, restored-layout, resized and dpi-2. Canvas starts at 1440x901, resizes to 1100x721 and renders 2200x1442 at DPR 2. Report: artifacts/imgui/browser/report.json. RHI reports browser-selected adapter, not a named GPU.
- Existing PBR and texture/checkerboard browser smoke suites pass with explicit UI bypass. Reports remain under artifacts/pbr/browser and artifacts/textures/browser. No UI performance claim is made.

After merging into C:\dev\ofg with the pre-existing PBR naming edits restored, native and web configure/build passed again, as did all four native CTest targets (same case/assertion counts), the PBR WGSL shader check, and browser UI/PBR smoke. The new browser workspace screenshot was inspected. The final web link retained only the documented Asyncify/WASM-exceptions warning. Merge evidence is in artifacts/imgui/merge/native-ctest.log, artifacts/imgui/browser/report.json and artifacts/pbr/browser/report.json. The original uncommitted PBR diff is also preserved as artifacts/imgui/merge/preexisting-pbr.patch; those edits remain outside the ImGui commits.

## Interfaces and Dependencies

New Entity name accessors; shared Workspace and ImGuiRenderer concrete classes. Existing Game::frame and Scene::lighting remain central. UI-free options --no-ui and ?ui=0 preserve diagnostics. Default native/browser workspace uses the PBR fixture; checkerboard bypasses UI.
