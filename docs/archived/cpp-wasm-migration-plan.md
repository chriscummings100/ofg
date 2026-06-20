# Migrate the OFG runtime from Rust/WASM to C++/WASM

Archived on 2026-06-20 after all milestones completed.

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Move OFG's game/runtime implementation from Rust/WASM to C++/WASM because Rust is becoming an obstruction for the project. In this plan, "obstruction" means the language and toolchain are slowing engine iteration, WebGPU debugging, build/deploy simplicity, and contributor velocity enough that continuing to deepen the Rust codebase would increase future migration cost. After this migration, the browser app should still open the same full-window canvas, load a WASM runtime, drive `resize`, `frame`, `debugStatus`, and `dispose`, and render a verified WebGPU bootstrap frame. The observable behavior should remain stable while the implementation language behind the TypeScript host changes from Rust to C++.

The first goal is parity, not new gameplay. A user should be able to run `npm run dev`, open the printed URL, and see the running WebGPU scene produced by C++/WASM. The TypeScript browser host should remain narrow: it owns DOM, canvas sizing, errors, module loading, and dev ergonomics; C++ owns game frame state, runtime status, WebGPU setup, durable render resources, draw submission, and native/offscreen render smoke when available.

## Progress

- [x] (2026-06-20 10:40Z) Read C:\dev\ofg\PLANS.md, C:\dev\ofg\docs\GUIDES.md, C:\dev\ofg\docs\API_CONTRACTS.md, C:\dev\ofg\README.md, C:\dev\ofg\package.json, current Rust runtime files, TypeScript WASM wrapper, browser smoke, native render smoke, and active renderer resources plan.
- [x] (2026-06-20 10:40Z) Confirmed the current TypeScript-to-WASM boundary is small enough to preserve during migration: create runtime, resize, frame, debug status JSON, dispose, and free/delete.
- [x] (2026-06-20 10:40Z) Checked current C++/WASM WebGPU options against official Emscripten, WebGPU header, Chrome, and Dawn documentation.
- [x] (2026-06-20 10:40Z) Drafted this migration ExecPlan in C:\dev\ofg\docs\plans\cpp-wasm-migration-plan.md.
- [x] (2026-06-20 10:43Z) Archived the Rust renderer/resource plan at C:\dev\ofg\docs\archived\renderer-resources-pipeline-plan.md so it can wait until after C++ migration parity.
- [x] (2026-06-20 10:45Z) Added a glossary explaining Dawn, Emscripten, Emdawnwebgpu, Embind, CMake, CTest, LLVM coverage, and related migration tools.
- [x] (2026-06-20 11:04Z) Reviewed this plan with five sub-agents through the review-plan skill and accepted required fixes around Clang-only tooling, additive C++ commands, exact generated module shape, coverage, deployment, async lifecycle, and performance gates.
- [x] (2026-06-20 11:04Z) Updated C:\dev\ofg\AGENTS.md so project instructions now describe C++/WASM as the target language and Clang as the only C++ compiler family.
- [x] (2026-06-20 11:04Z) Applied the review fixes to this plan: additive C++ commands, split WebGPU milestones, exact generated output names, Clang-only coverage, API mapping, async lifecycle rules, screenshot/deployment gates, native-smoke exception rules, and C++ WebGPU resource lifetime notes.
- [x] (2026-06-20 11:23Z) Switched the planned C++ test framework from assert-only executables to doctest registered through CTest.
- [x] (2026-06-20 11:29Z) Milestone 0 complete: captured Rust/WASM baseline command logs, browser/native smoke artifacts, coverage summaries, generated WASM sizes, and startup timing under C:\dev\ofg\artifacts\migration-baseline; local milestone review found no required fixes.
- [x] (2026-06-20 12:10Z) Added the additive C++ source root, CMake project, doctest v2.5.2 tests, Embind browser facade, and Emdawnwebgpu compile/link probe without changing the default Rust `npm run build:wasm`.
- [x] (2026-06-20 12:16Z) Added source-controlled tool pins and setup scripts for Emscripten 6.0.0 and Ninja 1.13.2, installed both under C:\dev\ofg\artifacts\toolchains, and proved Clang-only C++ tests through CMake/CTest.
- [x] (2026-06-20 12:22Z) Proved `npm run build:wasm:cpp` generates C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js and C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm through CMake/Emscripten with `--use-port=emdawnwebgpu`; the generated ES module imports and exposes a default factory.
- [x] (2026-06-20 12:25Z) Added the additive TypeScript C++ runtime adapter and Mocha tests for Embind `delete()` ownership and async module creation.
- [x] (2026-06-20 12:40Z) Milestone 1 complete: additive C++/WASM CMake/Emscripten build, Emdawnwebgpu link probe, doctest/CTest native tests, pinned Emscripten/Ninja setup, and TypeScript C++ adapter are in place; milestone review required fixes were applied.
- [x] (2026-06-20 13:10Z) Added a portable C++ `BrowserRuntime` core for frame count, debug status JSON, resize validation, recoverable zero-size dimensions, and dispose lifecycle; the Embind `BrowserGame` facade now delegates to this core.
- [x] (2026-06-20 13:20Z) Added pinned native LLVM 22.1.8 setup for native Clang/LLVM coverage, after emsdk's bundled native Clang proved insufficient for Windows source-based coverage.
- [x] (2026-06-20 13:30Z) Added `npm run coverage:cpp`, Clang source-based C++ coverage wiring, committed C++ coverage summary notes, and focused doctest cases that bring checked C++ core/runtime files to 100% line coverage.
- [x] (2026-06-20 13:45Z) Milestone 2 complete: C++ owns the portable non-render runtime contract, native doctest/CTest passes, TypeScript adapter tests pass through `npm test`, and `npm run coverage:cpp` passes with checked C++ core/runtime files at 100% line coverage.
- [x] (2026-06-20 14:05Z) Added C++/WASM browser WebGPU setup through Emdawnwebgpu: canvas selector surface creation, adapter/device/queue request callbacks, surface format selection, nonzero resize configuration, zero-size recovery, and deterministic late-dispose guards.
- [x] (2026-06-20 14:15Z) Milestone 3A complete: `npm run smoke:browser:cpp` creates and disposes the additive C++ runtime in Chromium, validates initialized status JSON, adapter/backend/format reporting, surface reconfigure on resize, and zero-size recovery without taking over the default Rust app.
- [x] (2026-06-20 14:35Z) Added C++/WASM clear-only browser frame submission: acquire current surface texture, create a texture view, encode a render pass with the shared dark blue-gray clear color, submit commands through the C++ queue, and release per-frame handles without calling browser `wgpuSurfacePresent`.
- [x] (2026-06-20 14:45Z) Milestone 3B complete: `npm run smoke:browser:cpp` now writes C:\dev\ofg\artifacts\browser-smoke-cpp\clear.png, classifies clear-only pixels, and reports `clearRatio: 1`.
- [x] (2026-06-20 14:55Z) Milestone 3B review complete: split browser-only WebGPU helper utilities out of C:\dev\ofg\cpp\src\web\browser_game.cpp, reran C++ browser smoke, C++ coverage, and the default test gate, and recorded the review summary below.
- [x] (2026-06-20 15:10Z) Added portable C++ bootstrap scene data and doctest coverage for triangle vertices, byte layout, and clear color.
- [x] (2026-06-20 15:15Z) Added browser-only C++ `BootstrapRenderer` for shader module, render pipeline, vertex buffer, triangle draw encoding, and durable resource counters.
- [x] (2026-06-20 15:20Z) Updated `npm run smoke:browser:cpp` to verify C++ bootstrap triangle pixels and stable `pipelineCreateCount` / `bufferCreateCount` values; latest screenshot is C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png.
- [x] (2026-06-20 15:25Z) Milestone 3C complete: C++/WASM renders the bootstrap triangle, reports stable durable pipeline/buffer counters, passes browser pixel smoke, passes C++ coverage/default tests, and milestone review required fixes are recorded below.
- [x] (2026-06-20 15:30Z) Milestone 4 discovery: local Emdawnwebgpu provides browser WebGPU headers/port files, but no native Dawn libraries were found under C:\dev\ofg\artifacts\toolchains; a pinned native Dawn source/build path is required for C++ native render smoke.
- [x] (2026-06-20 16:20Z) Added pinned Dawn revision C:\dev\ofg\dawn-version.txt, C:\dev\ofg\tools\setup-dawn.mjs, a Clang-built `ofg_render_smoke_cpp` CMake target, native C++ PNG/report helpers, and C:\dev\ofg\tools\smoke-render-cpp.mjs.
- [x] (2026-06-20 16:35Z) Replaced `npm run smoke:render` with the native C++ Dawn smoke path; it writes C:\dev\ofg\artifacts\render-smoke\bootstrap.png and C:\dev\ofg\artifacts\render-smoke\report.json.
- [x] (2026-06-20 16:40Z) Corrected C++ migration source, tests, TypeScript adapter, smoke fixture, and tool scripts for the repository comments/readability rule after the user called out missing function comments.
- [x] (2026-06-20 16:55Z) Milestone 4 review complete: fixed the native command-encoder lifetime issue, completed the broader comment/readability pass, reran native/browser/default validation, and recorded the review summary below.
- [x] Milestone 4: replace native render smoke with a Clang-built C++ Dawn path, or record a user-approved temporary exception and follow-up plan.
- [x] (2026-06-20 17:20Z) Switched the default app/runtime/build/test/coverage/package path to C++/WASM: `src/app/wasmRuntime.ts` now loads `assets/wasm/ofg_cpp`, `npm run build:wasm` runs CMake/Emscripten, and package/Cloudflare output contains `ofg_cpp.js` plus `ofg_cpp.wasm`.
- [x] (2026-06-20 17:30Z) Removed active Rust workspace/source/tooling: Cargo manifests/lock/toolchain config, `crates/`, wasm-bindgen build helpers, Rust coverage helper, and committed Rust coverage summary.
- [x] (2026-06-20 17:35Z) Milestone 5 review complete: active docs/contracts/coverage/system maps now describe C++ ownership; stale Rust command search outside the migration plan found no active Cargo/rustup/rustc/wasm-bindgen commands; validation passed and review follow-ups are recorded below.
- [x] Milestone 5: retire Rust build, tests, coverage, contracts, and generated artifact assumptions.
- [x] (2026-06-20 18:15Z) Translated the archived renderer/resource plan into the active C++-first plan at C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md.
- [x] (2026-06-20 18:15Z) Milestone 6 review complete: the new plan is self-contained, names C++/WASM, Emdawnwebgpu, Dawn, doctest/CTest, Clang coverage, active contracts, and comment/readability gates, and stale Rust command/crate searches found no active follow-up plan drift.
- [x] Milestone 6: translate the archived renderer/resource plan into C++ once the migration is proven.

## Surprises & Discoveries

- Observation: The root instructions mention GUIDES.md, but the committed guide file currently lives at C:\dev\ofg\docs\GUIDES.md.
  Evidence: `Get-Content GUIDES.md` failed, while C:\dev\ofg\docs\GUIDES.md exists and contains the active guide.

- Observation: The current browser facade is narrow and should not force a broad TypeScript rewrite.
  Evidence: C:\dev\ofg\src\app\wasmRuntime.ts expects a generated module with `BrowserGame.create(canvas)`, then calls `resize`, `frame`, `debug_status_json`, `dispose`, and `free`.

- Observation: The renderer-resource plan that was active when this migration was drafted assumed new Rust crates and should not proceed unchanged if this migration is accepted.
  Evidence: C:\dev\ofg\docs\archived\renderer-resources-pipeline-plan.md proposes `crates/ofg_resources` and Rust `wgpu` resource interfaces.

- Observation: The Rust renderer-resource plan was removed from active plans and kept as historical reference for later C++ translation.
  Evidence: C:\dev\ofg\docs\archived\renderer-resources-pipeline-plan.md exists, and during early migration C:\dev\ofg\docs\plans only contained C:\dev\ofg\docs\plans\cpp-wasm-migration-plan.md.

- Observation: The renderer/resource follow-up is now active again as a C++ plan.
  Evidence: C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md translates the asset-handle renderer/resource design to C++ source paths, doctest/CTest validation, Clang coverage, Emdawnwebgpu browser rendering, native Dawn smoke, active API contracts, and explicit comment/readability gates.

- Observation: Official Emscripten documentation points C++ WebGPU-on-web work at Emdawnwebgpu, an Emscripten port that implements a Dawn-like `webgpu.h` API on top of browser WebGPU.
  Evidence: https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html

- Observation: `webgpu.h` is the intended common C API for WebGPU implementations on native and Wasm, with Dawn and Emdawnwebgpu listed as implementations.
  Evidence: https://github.com/webgpu-native/webgpu-headers

- Observation: The review-plan sub-agent review found that the first draft was directionally sound but too vague about toolchain pins, generated asset paths, API naming, C++ coverage, native-smoke deferral, async lifecycle, and performance gates.
  Evidence: The review was run on 2026-06-20 with correctness, completeness, clarity, efficiency, and performance reviewers.

- Observation: Milestone 0 baseline validation passed on the current Rust/WASM bootstrap.
  Evidence: C:\dev\ofg\artifacts\migration-baseline\baseline-summary.md records `npm test`, `npm run smoke`, and `npm run coverage` as `exitCode=0`; browser smoke produced C:\dev\ofg\artifacts\migration-baseline\browser\bootstrap.png, and native smoke produced C:\dev\ofg\artifacts\migration-baseline\render\bootstrap.png.

- Observation: The milestone-review skill references C:\dev\ofg\docs\ARCHITECTURE.md, but this repository currently uses C:\dev\ofg\docs\SYSTEMS.md for system ownership documentation.
  Evidence: `Test-Path docs\ARCHITECTURE.md` returned false during Milestone 0 review; C:\dev\ofg\docs\SYSTEMS.md exists and maps active contracts to modules and commands.

- Observation: The local machine had CMake, Node, Git, Python, rustup, and Cargo, but no desktop `clang++`, `llvm-profdata`, `llvm-cov`, `em++`, or `emcmake` on PATH before the migration spike.
  Evidence: tool probes before Milestone 1 found CMake at C:\Program Files\CMake\bin\cmake.exe and no Clang/Emscripten tools in PATH or common install locations.

- Observation: Pinned emsdk 6.0.0 provides the Clang/LLVM binaries needed for the first native C++ doctest executable and the browser C++/WASM build, but it does not ship Ninja and only ships `lld.exe`, not the `lld-link.exe` name CMake's Windows-Clang generator asks Clang to execute.
  Evidence: C:\dev\ofg\tools\test-cpp.mjs now uses Clang from C:\dev\ofg\artifacts\toolchains\emsdk\upstream\bin, Ninja from C:\dev\ofg\artifacts\toolchains\ninja, Windows SDK `rc.exe` and `mt.exe` when present, and creates an ignored local `lld-link.exe` alias from emsdk's `lld.exe` if needed.

- Observation: Emdawnwebgpu compile/link is proven for the additive C++/WASM target.
  Evidence: `npm run build:wasm:cpp` completed through CMake and Emscripten with `--use-port=emdawnwebgpu`, produced C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js and C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm, and the first build retrieved the Emdawnwebgpu port from Dawn release `v20260423.175430`.

- Observation: Native C++ coverage on Windows needs a coherent desktop LLVM bundle, not only emsdk's Clang.
  Evidence: emsdk's native Clang could run doctest, but source coverage failed because its Windows profile runtime was missing. Copying a Visual Studio profile runtime produced stale raw profile data and a process-exit access violation. The pinned LLVM 22.1.8 bundle provides matching `clang-cl`, `llvm-profdata`, `llvm-cov`, and compiler-rt profile libraries; `npm run coverage:cpp` passes with it.

- Observation: Emdawnwebgpu browser adapter and device requests can be handled with `WGPUCallbackMode_AllowSpontaneous`, avoiding Asyncify for Milestone 3A.
  Evidence: C:\dev\ofg\cpp\src\web\browser_game.cpp uses spontaneous callbacks for `wgpuInstanceRequestAdapter` and `wgpuAdapterRequestDevice`; `npm run smoke:browser:cpp` observes initialized C++ status in Chromium.

- Observation: Emdawnwebgpu does not support `wgpuSurfacePresent` on the browser path.
  Evidence: C:\dev\ofg\artifacts\toolchains\emsdk\upstream\emscripten\cache\ports\emdawnwebgpu\emdawnwebgpu_pkg\webgpu\src\library_webgpu.js aborts in `wgpuSurfacePresent` with guidance to use requestAnimationFrame. Milestones 3B and 3C must submit browser command buffers without calling `wgpuSurfacePresent`.

- Observation: The local Emdawnwebgpu package is enough for browser C++/WASM WebGPU work but does not provide a native Dawn library for Milestone 4.
  Evidence: Recursive searches under C:\dev\ofg\artifacts\toolchains found C:\dev\ofg\artifacts\toolchains\emsdk\upstream\emscripten\cache\ports\emdawnwebgpu\emdawnwebgpu_pkg\webgpu\include\webgpu\webgpu.h and webgpu_cpp headers, but no `dawn*.lib` or `webgpu_dawn*` native library artifacts.

- Observation: Native Dawn D3D12 could not be used on this machine without a newer Windows SDK.
  Evidence: A Dawn build with D3D12 enabled failed in C:\dev\ofg\artifacts\toolchains\dawn\src\src\dawn\native\d3d12\D3D12Info.cpp because Windows SDK 10.0.19041.0 does not define `D3D12_FEATURE_DATA_D3D12_OPTIONS13` or `D3D12_FEATURE_D3D12_OPTIONS13`.

- Observation: Native Dawn Vulkan can build with Clang on this machine, but it needs Dawn's system-component loading option to load the Windows Vulkan loader reliably.
  Evidence: A Vulkan-only Dawn `webgpu_dawn` build completed successfully. The first `npm run smoke:render` attempt then failed at runtime with `DynamicLib.Open: vulkan-1.dll Windows Error: 87`; setting `DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON` in CMake let `npm run smoke:render` pass on the Vulkan backend.

- Observation: The repository's comments/readability rule was under-applied during the first C++ migration edits and needed correction before continuing.
  Evidence: The user explicitly called out C:\dev\ofg\docs\GUIDES.md and C:\dev\ofg\AGENTS.md requiring comments/docstrings for every function and internal comments for functions over 50 lines. The new native smoke, PNG writer, shared WebGPU helper, renderer, and smoke setup scripts were updated with purpose comments and section comments.

## Decision Log

- Decision: Treat this as a staged migration with a parity gate before deleting Rust.
  Rationale: The current Rust runtime is already tested by browser and native smoke. Keeping it as a reference until the C++ path passes the same public contracts lowers risk and avoids a long broken middle.
  Date/Author: 2026-06-20 / Codex

- Decision: Do not build additional engine features in Rust while this migration is active.
  Rationale: New Rust renderer/resource work would increase the amount of code to port. The active renderer resources plan should be paused, archived, or rewritten for C++ after parity.
  Date/Author: 2026-06-20 / Codex

- Decision: Archive the Rust renderer/resource plan rather than keep it active during C++ migration.
  Rationale: The user explicitly said the renderer plan can wait until later. Archiving leaves the design available as reference without presenting it as current implementation work.
  Date/Author: 2026-06-20 / User and Codex

- Decision: Use C++20, CMake, Emscripten, Embind, and Emdawnwebgpu for the browser build unless the toolchain spike proves a blocking issue.
  Rationale: CMake gives a standard native/web C++ project shape, Embind can expose the existing narrow browser object model, and Emdawnwebgpu is the official Emscripten route for WebGPU.
  Date/Author: 2026-06-20 / Codex

- Decision: Use Clang only for C++ builds, including Windows native builds and browser WASM builds.
  Rationale: The user explicitly preferred one C++ compiler family. Clang-only builds reduce coverage, warning, sanitizer, and CI drift. Native C++ uses desktop Clang/LLVM; browser C++ uses Emscripten's Clang.
  Date/Author: 2026-06-20 / User and Codex

- Decision: Keep the C++ path additive until browser rendering parity is proven.
  Rationale: The existing Rust bootstrap should remain the default `npm run build:wasm` and `npm run build` path while C++ proves lifecycle, WebGPU initialization, and browser smoke. This prevents the migration spike from breaking every downstream command.
  Date/Author: 2026-06-20 / Codex

- Decision: Use doctest for C++ tests, with doctest test executables registered through CTest.
  Rationale: The user prefers doctest. It is a small single-header C++ testing framework with fast compile times, a clean syntax, and enough structure for the migration tests without bringing in a heavier framework.
  Date/Author: 2026-06-20 / User and Codex

- Decision: Pin Emscripten to 6.0.0 for the migration spike and install it under C:\dev\ofg\artifacts\toolchains\emsdk through `npm run setup:emscripten`.
  Rationale: The repo needs a reproducible Clang-family browser toolchain without depending on each developer's global PATH. Emscripten 6.0.0 is source-controlled in C:\dev\ofg\emscripten-version.txt and was the newest stable emsdk tag observed during the Milestone 1 probe.
  Date/Author: 2026-06-20 / Codex

- Decision: Pin Ninja to 1.13.2 under C:\dev\ofg\ninja-version.txt and install it under C:\dev\ofg\artifacts\toolchains\ninja through `npm run setup:ninja`.
  Rationale: Ninja is a small CMake generator, not a second compiler toolchain. Pinning it lets CMake/CTest run consistently on Windows without requiring a system-wide install.
  Date/Author: 2026-06-20 / Codex

- Decision: Use the pinned emsdk LLVM/Clang binaries for Milestone 1 native doctest execution on Windows.
  Rationale: The user's Clang-only preference is better served by one pinned LLVM family at this stage than by adding a separate desktop LLVM install. This remains compatible with a future full desktop LLVM setup for coverage and native Dawn work.
  Date/Author: 2026-06-20 / Codex

- Decision: Add pinned native LLVM 22.1.8 for desktop C++ tests and coverage while keeping Emscripten 6.0.0 for browser WASM.
  Rationale: Emscripten's Clang remains the correct browser compiler, but native source coverage needs matching desktop `clang-cl`, `llvm-profdata`, `llvm-cov`, and profile runtime libraries. This still keeps C++ on the Clang/LLVM family and avoids MSVC as a compiler.
  Date/Author: 2026-06-20 / Codex

- Decision: Pin native Dawn to revision 31e25af254ab572c77054edec4946d2244e184dd and use it only for native/offline render smoke.
  Rationale: Browser C++/WASM uses Emdawnwebgpu to call the browser's WebGPU API; native smoke needs a separate desktop WebGPU implementation. Pinning the Dawn source revision keeps the native validation backend reproducible without vendoring Dawn into source control.
  Date/Author: 2026-06-20 / Codex

- Decision: Build native Dawn smoke with Vulkan enabled, D3D backends disabled, and `DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON` on Windows.
  Rationale: The local Windows SDK is too old for the current Dawn D3D12 backend, while Vulkan builds and renders successfully. `DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON` makes Dawn load `vulkan-1.dll` through the system component path and fixes the observed Windows Error 87 loader failure.
  Date/Author: 2026-06-20 / Codex

- Decision: Use a fixed 32 MiB initial WebAssembly heap for the additive C++ browser module.
  Rationale: The migration spike has tiny runtime state and should fail visibly rather than silently changing memory-growth behavior. Later asset-heavy milestones can increase the budget deliberately.
  Date/Author: 2026-06-20 / Codex

- Decision: Use a tiny tested local JSON writer for `RuntimeDebugStatus`.
  Rationale: The status schema is fixed and small. A local serializer with string escaping tests avoids pulling a general JSON library into the initial browser runtime and keeps WASM size down.
  Date/Author: 2026-06-20 / Codex

- Decision: Keep Milestone 3C debug status counters to the existing `pipelineCreateCount`, `bufferCreateCount`, and `surfaceConfigureCount` contract.
  Rationale: The public TypeScript status parser and current Rust default runtime already expose these counters. Adding shader or upload counters during the additive C++ milestone would expand the public contract before the default runtime switch. Upload/shader accounting remains a later performance diagnostics extension.
  Date/Author: 2026-06-20 / Codex

- Decision: Use `webgpu.h` and a thin RAII C++ wrapper layer for WebGPU ownership rather than writing rendering through TypeScript.
  Rationale: OFG should remain engine-from-scratch and TypeScript should stay a host, not the renderer. `webgpu.h` keeps the code close to the browser WebGPU model while still being usable from C++.
  Date/Author: 2026-06-20 / Codex

- Decision: Keep npm as the repository-level command orchestrator.
  Rationale: Existing developer and deployment commands are npm scripts. CMake and Emscripten should sit behind `npm run build:wasm`, `npm run test:cpp`, `npm run smoke:render`, and coverage scripts so the repo remains approachable.
  Date/Author: 2026-06-20 / Codex

- Decision: Preserve the TypeScript `BrowserGameRuntime` interface through the migration.
  Rationale: The TypeScript app and tests already encode a clean ownership boundary. The generated module shape can change, but `src/app/wasmRuntime.ts` should continue exposing the same application-facing runtime interface.
  Date/Author: 2026-06-20 / Codex

- Decision: Defer C++ threads, Wasm workers, and shared-memory simulation until after bootstrap parity.
  Rationale: Threading affects build flags, cross-origin isolation, memory model, and deployment headers. The first migration milestone should prove runtime/render ownership before adding concurrency complexity.
  Date/Author: 2026-06-20 / Codex

- Decision: Make C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md the active renderer/resource follow-up after this migration.
  Rationale: The old renderer/resource plan had useful asset-handle goals but obsolete implementation assumptions. The replacement plan keeps the design direction while using the C++ runtime, C++ source layout, doctest/CTest, Clang coverage, browser Emdawnwebgpu, and native Dawn smoke paths that now exist.
  Date/Author: 2026-06-20 / Codex

## Outcomes & Retrospective

Milestone 0 established the Rust/WASM bootstrap baseline before code movement. The current Rust path passes unit/integration tests, browser/native smoke, and coverage. Baseline artifacts under C:\dev\ofg\artifacts\migration-baseline capture generated WASM size, JS glue size, startup-to-frame timing, debug counters, pixel ratios, screenshots, smoke reports, coverage summaries, and command logs.

Milestone 1 is complete. The additive C++ path now has pinned setup scripts, a CMake project, doctest/CTest tests, an Embind browser facade, a TypeScript C++ adapter, and a CMake/Emscripten WebAssembly build that links Emdawnwebgpu. Rust remains the default app runtime.

Milestone 2 is complete. C++ owns the portable non-render runtime state, resize validation, frame counting, status JSON, dispose lifecycle, native doctest coverage, and an additive TypeScript adapter. At the end of Milestone 2, it did not yet own the active browser render path, WebGPU device/surface setup, draw submission, native Dawn smoke, or default coverage replacement; Milestone 3A resolved browser WebGPU setup. When the migration is complete, record whether C++ removed the current Rust obstruction, which toolchain pieces were painful, whether the browser/native smoke coverage stayed strong, and what follow-up plan should replace the Rust renderer-resource plan.

Milestone 3A is complete. The additive C++/WASM runtime now owns browser WebGPU instance, canvas surface, adapter, device, queue, surface format selection, surface configuration on resize, initialized status reporting, zero-size recovery, and deterministic dispose behavior. It still does not draw, submit command buffers, replace Rust as the default app runtime, provide native Dawn smoke, or replace the default coverage command.

Milestone 3B is complete. The additive C++/WASM runtime now submits a clear-only WebGPU render pass to the browser canvas using the shared clear color `[27, 37, 50, 255]`. The focused C++ browser smoke captures a screenshot and verifies that all sampled pixels match the expected clear color. It still does not create durable triangle renderer resources, draw geometry, replace Rust as the default app runtime, provide native Dawn smoke, or replace deployment packaging.

Milestone 3C is complete. The additive C++/WASM runtime now owns portable bootstrap scene data, creates browser WebGPU shader/pipeline/vertex-buffer resources once, submits the red/green/blue triangle draw, reports stable `pipelineCreateCount: 1` and `bufferCreateCount: 1`, and writes a passing triangle screenshot/report under C:\dev\ofg\artifacts\browser-smoke-cpp. Rust remains the default app runtime until native smoke/default-switch and packaging milestones are complete.

Milestone 4 is complete. `npm run smoke:render` now builds and runs a Clang-native C++ Dawn executable that renders the same bootstrap triangle offscreen, writes C:\dev\ofg\artifacts\render-smoke\bootstrap.png and C:\dev\ofg\artifacts\render-smoke\report.json, and validates the shared smoke contract on the Vulkan backend. Rust remains the default browser runtime until Milestone 5 switches default commands and retires Rust.

Milestone 5 is complete. C++/WASM is now the default browser runtime and deployment package. `npm run build:wasm` emits `assets/wasm/ofg_cpp/ofg_cpp.js` and `assets/wasm/ofg_cpp/ofg_cpp.wasm`; `npm test` runs C++ doctest/CTest plus TypeScript tests; `npm run coverage` runs C++ plus TypeScript coverage; `npm run smoke` validates default browser C++ rendering plus native Dawn rendering; and `npm run build:cloudflare` packages only C++ runtime assets. Active Rust source, Cargo manifests, wasm-bindgen helpers, and Rust coverage summaries have been removed.

Milestone 6 is complete. The archived renderer/resource plan has a C++-first replacement at C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md. That plan preserves the asset-handle, mutable resource store, draw-list renderer, and animated plane-and-cubes goals, but it now uses C++ source paths, WebGPU through `webgpu.h`, Emdawnwebgpu in the browser, pinned Dawn for native smoke, doctest/CTest, Clang/LLVM coverage, and explicit comment/readability acceptance.

This migration removed the active Rust obstruction: the repo now has one C++/WASM runtime path for browser rendering, native smoke, coverage, packaging, and deployment. The painful pieces were toolchain setup on Windows, native LLVM coverage, and native Dawn backend setup. Browser and native smoke stayed strong enough to catch real lifetime and packaging issues, and the follow-up renderer/resource plan is now aligned with the new C++ foundation.

## Contract and Quality Baseline

This plan intentionally changes active contracts in C:\dev\ofg\docs\API_CONTRACTS.md. The contract changes must happen in the milestone that makes the behavior true, not as stale promise text.

C:\dev\ofg\AGENTS.md has been updated to remove the old "vast majority Rust/WASM" instruction. During this migration, C++/WASM with Clang-only C++ builds is the project direction, while existing Rust commands remain available until C++ parity is proven.

OFG-BOOT-001 TypeScript Host Ownership is preserved. TypeScript may keep DOM boot, canvas lookup/creation, canvas resize policy, fatal-error display, local dev ergonomics, WASM module loading, and Playwright smoke helpers. TypeScript must not own gameplay simulation, scene graph state, GPU pipeline creation, render draw submission, or game-world data structures.

OFG-BOOT-002 Rust Runtime Ownership must be replaced with C++ Runtime Ownership. The new contract should say C++ owns frame state, debug status, scene data, renderer setup, WebGPU resource creation, draw submission, and native/offscreen rendering if retained.

OFG-BOOT-003 WASM Facade is preserved in spirit. The browser facade remains narrow: TypeScript creates the runtime, resizes it, requests frames, reads debug status, and disposes it. The generated module implementation may change from wasm-bindgen to Emscripten/Embind.

OFG-BOOT-004 Renderer Compatibility must be updated. During migration, capture the current Rust browser/native bootstrap visual and smoke report as baseline artifacts, then compare the C++ path against those expectations. Do not require two live browser runtimes indefinitely. After Rust removal, the contract should describe browser and native C++ smoke compatibility or record a temporary exception if native Dawn smoke is deferred.

OFG-BOOT-005 WebGPU Baseline is preserved. The renderer should request no optional GPU features, should not manually request limits above the conservative browser baseline, should keep durable render resources out of ordinary frames, and should report adapter/backend/format diagnostics.

OFG-BOOT-006 Resource Lifetime is preserved. Pipeline, shader module, vertex buffer, and bind-group-like resources must be created during initialization or explicit resize/mutation, not every frame.

OFG-BOOT-007 Generated Artifacts must be updated. Rust outputs under C:\dev\ofg\target and C:\dev\ofg\assets\wasm\ofg_web remain generated during the additive phase. The C++ additive browser output is C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js and C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm, served at `/assets/wasm/ofg_cpp/ofg_cpp.js` and `/assets/wasm/ofg_cpp/ofg_cpp.wasm`. C++ build trees live under C:\dev\ofg\artifacts\build\cpp-native and C:\dev\ofg\artifacts\build\cpp-wasm. After the default switch, packaging and import code must point at the C++ generated files.

OFG-BOOT-008 Deployment is preserved. Cloudflare Pages remains the default deployment target with `.deploy` as the build output directory and cross-origin isolation headers intact.

OFG-BOOT-009 Coverage must be rewritten from Rust plus TypeScript to C++ plus TypeScript. C++ coverage uses Clang source-based coverage, `llvm-profdata`, and `llvm-cov`; TypeScript keeps c8. Modified C++ implementation files should meet the same default coverage attention threshold, currently about 90% line coverage, unless the plan records a specific exception. Browser-only C++/WASM and WebGPU glue may be covered by TypeScript adapter tests and browser smoke when native line coverage cannot exercise it directly.

Quality constraints from C:\dev\ofg\docs\GUIDES.md apply to C++ too: public interfaces should be documented, files approaching 500 lines should be considered for splitting, files above 1000 lines should be treated as a concern, and module contracts should stay explicit.

Target contract updates when milestones make them true:

OFG-BOOT-002 should become "C++ Runtime Ownership": C++ owns frame state, debug status, scene data, renderer setup, WebGPU resource creation, draw submission, and native/offscreen rendering when the native smoke path is present. TypeScript remains the browser host.

OFG-BOOT-004 should become "C++ Renderer Compatibility": browser smoke and native smoke, when enabled, validate the same C++ renderer data, clear color, shader source, and visual contract. If native smoke is temporarily deferred, this contract must name the exception and link the follow-up plan.

OFG-BOOT-007 should name C:\dev\ofg\assets\wasm\ofg_cpp, C:\dev\ofg\artifacts\build\cpp-native, C:\dev\ofg\artifacts\build\cpp-wasm, C:\dev\ofg\.deploy, C:\dev\ofg\dist, and C:\dev\ofg\dist-test as generated outputs after the default switch. Rust generated paths remain historical or additive-only during migration.

OFG-BOOT-009 should become "C++ and TypeScript Coverage": Clang source-based coverage is the C++ coverage gate; c8 remains the TypeScript coverage gate; browser-only WebGPU code may be validated by browser smoke and adapter tests when native coverage cannot execute it directly.

## Context and Orientation

The repository root is C:\dev\ofg. The current project is a Rust workspace plus a TypeScript browser host.

C:\dev\ofg\package.json currently drives all repo commands. It builds Rust WASM through C:\dev\ofg\tools\build-wasm.mjs, compiles TypeScript, runs Rust tests through Cargo, runs TypeScript tests through Mocha, and runs browser/native smoke tests.

C:\dev\ofg\src\app\wasmRuntime.ts is the TypeScript boundary to the generated WASM package. It imports `/assets/wasm/ofg_web/ofg_web.js`, initializes the generated module, calls `BrowserGame.create(canvas)`, and wraps the raw generated object behind `BrowserGameRuntime`.

C:\dev\ofg\crates\ofg_core currently owns `FrameState`.

C:\dev\ofg\crates\ofg_web currently owns the Rust wasm-bindgen facade and browser WebGPU runtime.

C:\dev\ofg\crates\ofg_render currently owns the bootstrap triangle renderer, shared clear color, vertex data, and WGSL shader.

C:\dev\ofg\crates\ofg_test_harness currently owns the native offscreen render smoke that writes C:\dev\ofg\artifacts\render-smoke\bootstrap.png and C:\dev\ofg\artifacts\render-smoke\report.json.

Definitions used in this plan:

C++ runtime: the C++ code compiled to WebAssembly for the browser, plus native C++ code compiled for tests and smoke where possible.

Clang-only: every C++ build uses Clang-family compilers. Native Windows builds use desktop Clang/LLVM, not MSVC. Browser builds use Emscripten's Clang. Coverage builds use the same Clang source-based coverage flags across platforms.

Emscripten: the LLVM-based C/C++ compiler toolchain that produces WebAssembly and JavaScript glue for browsers.

Embind: Emscripten's binding system for exposing C++ classes and functions to JavaScript.

Emdawnwebgpu: the Emscripten WebGPU port that maps the `webgpu.h` C API to the browser's JavaScript WebGPU implementation.

`webgpu.h`: the C API equivalent of browser WebGPU, intended to be implemented by backends such as Dawn and Emdawnwebgpu.

Dawn: Chromium's native WebGPU implementation. In this plan it is a platform graphics dependency, not a game engine.

Parity gate: a validation point where the C++ path provides the same user-visible and test-visible behavior as the current Rust path.

Durable render resource: a GPU object that should be created during initialization, resize, or explicit resource mutation, not every frame. Examples include buffers, textures, samplers, bind group layouts, bind groups, shader modules, render pipelines, and depth textures.

Ordinary frame: a frame that advances time and submits rendering without changing asset definitions, resizing the canvas, or rebuilding durable GPU state.

Mutation: an explicit change to resource data or configuration, such as replacing shader source, resizing a surface, updating mesh vertices, or changing texture pixels.

RAII: C++ "resource acquisition is initialization"; an object owns cleanup for a resource. For WebGPU, RAII wrappers must account for queued GPU work, so destruction may need deferred release rather than immediate destruction after command submission.

## Toolchain and Dependency Glossary

This migration introduces several C++/WASM tools. They are build/runtime infrastructure, not a third-party game engine. OFG remains written from the ground up; these tools provide the compiler, browser binding, graphics API access, tests, and coverage plumbing.

Dawn is Google's open-source implementation of the WebGPU standard. It is the WebGPU implementation used underneath Chromium-family browsers, and it also provides native libraries, headers, and C++ wrappers that can run WebGPU code on desktop backends such as D3D12, Metal, Vulkan, and OpenGL. In OFG, Dawn is not a game engine and should not own gameplay, scene logic, resources, or renderer architecture. Its likely role is the native C++ render-smoke backend: the same OFG renderer code can target `webgpu.h`, while Dawn supplies the native implementation for tests outside the browser.

`webgpu.h` is the C API form of the WebGPU browser API. Think of it as the common ABI-level graphics contract: OFG's C++ renderer calls `webgpu.h`, then the browser or a native implementation performs the GPU work. Using `webgpu.h` keeps OFG close to the browser WebGPU model and avoids tying the renderer to a high-level engine framework.

Emscripten is the C/C++ to WebAssembly compiler toolchain. It uses LLVM/Clang to compile C++ into `.wasm` plus JavaScript glue that browsers can load. In OFG, it replaces the current Rust `cargo build --target wasm32-unknown-unknown` plus `wasm-bindgen` browser package generation.

emsdk is the official Emscripten SDK installer and version manager. It installs and activates a known Emscripten toolchain version, including `emcc`, `em++`, `emcmake`, and related support tools. OFG should pin the emsdk/Emscripten version so local development and Cloudflare builds are reproducible.

`emcc` and `em++` are Emscripten's C and C++ compiler drivers. They behave like Clang-style compiler commands, but their output is WebAssembly and JavaScript glue for web runtimes.

`emcmake` is Emscripten's wrapper around CMake. It configures a CMake build directory so the C++ project cross-compiles to WebAssembly instead of building a normal Windows/Linux/macOS executable.

Emdawnwebgpu is Dawn's Emscripten implementation of `webgpu.h` on top of the browser's JavaScript WebGPU API. In browser builds, this is the bridge that lets C++ call WebGPU without moving renderer ownership into TypeScript. It is the preferred WebGPU route for C++/WASM in this plan.

Embind is Emscripten's binding system for exposing selected C++ classes and functions to JavaScript. In OFG, Embind should expose only the narrow browser facade, such as creating `BrowserGame`, calling `resize`, calling `frame`, reading debug status JSON, and disposing. It should not expose renderer internals or mutable game-world state to TypeScript.

Embind must stay off hot paths. JavaScript should make one coarse `frame(timeMs)` call per animation frame, plus coarse resize/input calls. It should not call through Embind per entity, per draw, per component, or per string-heavy query.

CMake is the C++ build system generator. It describes libraries, executables, include paths, compiler options, and test targets once, then generates native or Emscripten builds. In OFG, npm remains the top-level command surface, while CMake owns the C++ build graph underneath scripts like `npm run build:wasm` and `npm run test:cpp`.

CTest is CMake's test runner. It runs tests registered by CMake and reports failures in a standard way. In OFG, it should run doctest-based native C++ unit test executables for portable systems such as frame state, status serialization, resource stores, math, and simulation logic.

LLVM/Clang source-based coverage is the replacement for Rust coverage. Clang instruments C++ test builds, then LLVM tools such as `llvm-profdata` and `llvm-cov` produce coverage reports. OFG's npm coverage script should wrap these tools and keep generated reports under C:\dev\ofg\artifacts\coverage, with committed summaries under C:\dev\ofg\docs\coverage.

The initial C++ test framework is doctest, registered with CTest. Pin doctest as a source-controlled third-party dependency, preferably by vendoring the single header under C:\dev\ofg\cpp\third_party\doctest\doctest.h with its license and version note. The current planned version is doctest v2.5.2 unless Milestone 1 records a reason to pin a different release.

The initial runtime JSON implementation is a small local writer dedicated to `RuntimeDebugStatus`, with tests for camelCase field names, nullable strings, numeric fields, and string escaping.

Node.js and npm remain the repository-level command runner. They already orchestrate TypeScript builds, browser smoke, packaging, and Cloudflare output. The C++ migration should keep that shape so day-to-day commands stay stable.

Mocha and c8 remain the TypeScript test and coverage tools. C++ replaces Rust, not the TypeScript browser host or its tests.

Cloudflare Pages remains the deployment target. The migration changes which WASM files are packaged, but it should preserve `.deploy` output and the cross-origin isolation headers required by WebGPU.

## Plan of Work

Milestone 0 captures the current Rust baseline and pins migration decisions before code movement. Run the current Rust build, browser smoke, native smoke, and coverage when the environment allows. Record the Rust `.wasm` size, generated JS glue size, browser startup-to-first-frame timing, browser smoke duration, frame count at capture, pipeline/buffer creation counts, and native/browser screenshots under C:\dev\ofg\artifacts\migration-baseline. Record the exact Clang-only policy in C:\dev\ofg\AGENTS.md and this plan. This milestone is accepted when future C++ output has concrete baseline artifacts to compare against.

Milestone 1 pins and proves the C++/WASM toolchain additively. Add C:\dev\ofg\cpp as the C++ source root with only the files required for a minimal module and portable tests. Add C:\dev\ofg\tools\build-cpp-wasm.mjs and an npm script such as `build:wasm:cpp`; do not change `npm run build:wasm` away from Rust yet. The C++ browser output is C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js and C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm. Milestone 1 must prove Emdawnwebgpu can compile/link with `--use-port=emdawnwebgpu` and bind or receive the existing TypeScript-owned `HTMLCanvasElement` far enough to create a surface or produce a clear blocker. It must also write an exact Emscripten/emsdk pin to C:\dev\ofg\emscripten-version.txt or a similarly explicit source-controlled version file before Milestone 2 starts.

Milestone 2 ports the non-render runtime contract. Add C++ `FrameState`, `RuntimeDebugStatus`, the local status JSON writer, lifecycle/dispose behavior, and doctest native tests registered with CTest. Add C:\dev\ofg\tools\cpp-coverage.mjs and `npm run coverage:cpp` in this milestone so test seams are designed with coverage from the start. Update TypeScript adapter tests around the stable C++ module shim while preserving the existing application-facing `BrowserGameRuntime` interface. TypeScript tests should keep fake raw runtimes and add coverage for the C++ module adapter if generated names differ from wasm-bindgen.

Milestone 3A proves C++/WASM browser WebGPU setup without drawing a triangle. Implement async runtime creation, canvas/surface binding, adapter/device request, conservative limits/features, surface format selection, zero-size resize behavior, debug status fields, and WebGPU error-scope or uncaptured-error reporting. This milestone is accepted when browser automation can create and dispose the C++ runtime, validate status JSON, and report adapter/backend/format without taking over the default Rust app.

Milestone 3B renders a clear-only C++/WASM WebGPU frame. It should configure the surface, acquire a frame, clear to the existing dark blue-gray clear color, submit the command buffer through the queue without calling browser `wgpuSurfacePresent`, and report surface configure count plus durable resource counters. The smoke should fail on blank frames, WebGPU validation errors, device loss, or unexpected per-frame durable resource creation.

Milestone 3C renders the bootstrap triangle with C++/WASM. Compile-time embed the WGSL shader source and deterministic triangle/clear-color data so browser WASM does not depend on runtime file reads. Create shader module, vertex buffer, render pipeline, and any bind-group-like resources outside ordinary frames. Browser smoke must capture a screenshot under C:\dev\ofg\artifacts\browser-smoke-cpp and prove the same visual categories as the Rust baseline. This milestone proves browser C++ rendering parity, but the default app path still waits for the native smoke/default-switch milestones.

Milestone 4 replaces or explicitly defers native render smoke. Preferred path: add a Clang-built native C++ executable using Dawn through the same `webgpu.h`-style renderer abstraction, write C:\dev\ofg\artifacts\render-smoke\bootstrap.png and report JSON, and keep `npm run smoke:render` equivalent to today's validation value. If Dawn native setup is too heavy, record a user-approved temporary exception in this plan and C:\dev\ofg\docs\API_CONTRACTS.md, create a follow-up plan under C:\dev\ofg\docs\plans, and redefine smoke commands so they do not pretend native smoke passed. Browser C++ smoke remains mandatory either way.

Milestone 5 retires Rust from active code and commands. Remove or archive C:\dev\ofg\Cargo.toml, C:\dev\ofg\Cargo.lock, C:\dev\ofg\rust-toolchain.toml, C:\dev\ofg\crates, wasm-bindgen helper scripts, Rust coverage scripts, and Rust commands in C:\dev\ofg\package.json only after the C++ browser parity gates pass and native smoke is either replaced or explicitly deferred. Update C:\dev\ofg\README.md, C:\dev\ofg\COVERAGE.md, C:\dev\ofg\docs\COVERAGE.md, C:\dev\ofg\docs\SYSTEMS.md, and C:\dev\ofg\docs\API_CONTRACTS.md so active documentation describes C++/WASM. Archives may still mention Rust as historical context.

Milestone 6 translates the archived renderer/resource plan into C++. Once C++ owns the bootstrap runtime, use C:\dev\ofg\docs\archived\renderer-resources-pipeline-plan.md as historical reference and create a new active C++ plan under C:\dev\ofg\docs\plans. The asset/resource goals can survive, but the implementation paths, coverage commands, native smoke harness, and ownership contracts must be C++-first.

After each milestone, run the repo-local milestone-review skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

At implementation start, capture the current baseline from C:\dev\ofg:

    npm test
    npm run smoke
    npm run coverage

The Rust baseline may fail if local Rust or WebGPU tooling is missing. If so, record the exact failure in Surprises & Discoveries and continue only after deciding whether it is an environment limitation or a real regression.

Record baseline performance and size artifacts before replacing the default runtime:

    assets/wasm/ofg_web/ofg_web.js byte size
    assets/wasm/ofg_web/ofg_web_bg.wasm byte size
    browser smoke duration
    startup-to-first-frame timing from page load to frameCount >= 2
    pipelineCreateCount, bufferCreateCount, and surfaceConfigureCount from debug status
    browser screenshot and report under artifacts/migration-baseline/browser
    native screenshot and report under artifacts/migration-baseline/render when native smoke is available

Verify local Clang-only C++/WASM tools from PowerShell:

    Get-Command clang++
    Get-Command llvm-profdata
    Get-Command llvm-cov
    Get-Command emcmake
    Get-Command em++
    clang++ --version
    llvm-profdata --version
    llvm-cov --version
    em++ --version
    emcc --version
    cmake --version

If Emscripten is not installed, install or activate a pinned emsdk version outside the repository, then record that version in a source-controlled file such as C:\dev\ofg\emscripten-version.txt and teach C:\dev\ofg\tools\build-cpp-wasm.mjs to check it. Milestone 1 cannot complete until this file contains the exact emsdk/Emscripten version and the build script fails with a clear message when the active version differs.

For the current local Windows workflow, the pinned setup commands are:

    npm run setup:emscripten
    npm run setup:llvm
    npm run setup:ninja

They install generated toolchain files under C:\dev\ofg\artifacts\toolchains, which is ignored and can be rebuilt.

The C++/WASM release build should use Emscripten ES module output and Emdawnwebgpu. The exact flags may adjust during Milestone 1, but the plan expects the equivalent of:

    -std=c++20
    --use-port=emdawnwebgpu
    --bind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createOfgCppModule
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=0
    -sINITIAL_MEMORY=33554432
    -sASSERTIONS=0 for release
    --closure=1 for release when compatible
    -g0 for deployable release output
    -o assets/wasm/ofg_cpp/ofg_cpp.js

The initial heap policy is fixed-size memory with `ALLOW_MEMORY_GROWTH=0` and `INITIAL_MEMORY=33554432`, a 32 MiB bootstrap budget. Exceeding that budget should fail as an ordinary WebAssembly allocation failure during the spike. Later asset-heavy plans may revise this deliberately.

Create the initial C++ project structure incrementally. Do not scaffold renderer files until the renderer milestones need them. Milestone 1 starts with:

    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\include\ofg\core\frame_state.hpp
    C:\dev\ofg\cpp\include\ofg\runtime\runtime_debug_status.hpp
    C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
    C:\dev\ofg\cpp\src\core\frame_state.cpp
    C:\dev\ofg\cpp\src\runtime\runtime_debug_status.cpp
    C:\dev\ofg\cpp\src\web\browser_game.cpp
    C:\dev\ofg\cpp\src\web\embind_module.cpp
    C:\dev\ofg\cpp\src\web\emdawn_probe.cpp
    C:\dev\ofg\cpp\tests\test_main.cpp
    C:\dev\ofg\cpp\tests\frame_state_test.cpp
    C:\dev\ofg\cpp\tests\runtime_debug_status_test.cpp

Renderer files such as C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp and C:\dev\ofg\cpp\src\render\shaders\bootstrap.wgsl are added in Milestone 3B or 3C, not during the toolchain spike.

Expected source organization can change during implementation, but C++ files should remain small enough to satisfy C:\dev\ofg\docs\GUIDES.md and should keep browser-only code separated from portable core code.

Build native C++ unit tests through CMake:

    npm run test:cpp

Build native C++ coverage through Clang:

    cmake -S cpp -B artifacts/build/cpp-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DOFG_ENABLE_COVERAGE=ON
    cmake --build artifacts/build/cpp-coverage
    ctest --test-dir artifacts/build/cpp-coverage --output-on-failure
    llvm-profdata merge -sparse artifacts/coverage/cpp/*.profraw -o artifacts/coverage/cpp/cpp.profdata
    llvm-cov export artifacts/build/cpp-coverage/<test-binary> -instr-profile=artifacts/coverage/cpp/cpp.profdata -format=text > artifacts/coverage/cpp-summary.json

C:\dev\ofg\tools\cpp-coverage.mjs should own the real version of the coverage flow. It should set `LLVM_PROFILE_FILE`, discover all CTest test binaries or consume a manifest written by CMake, merge `.profraw` files, export JSON, write a human summary, and fail when changed C++ implementation files fall below the documented threshold without an exception.

Build browser C++/WASM through Emscripten:

    npm run build:wasm:cpp

Wrap those commands in npm scripts. During the additive phase, keep current Rust defaults and add C++ commands beside them:

    npm run build:wasm:cpp
    npm run test:cpp
    npm run coverage:cpp

Only after Milestone 3C passes may the defaults switch from Rust to C++:

    npm run build:wasm
    npm run build
    npm run test:wasm
    npm run test:ts
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run smoke
    npm run coverage
    npm run package:site
    npm run build:cloudflare

During browser/render milestones, keep the dev server available:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, report the alternate URL printed by the tool.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the milestone-review skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Milestone 0 is accepted when baseline artifacts exist under C:\dev\ofg\artifacts\migration-baseline, C:\dev\ofg\AGENTS.md and this plan both record the C++/WASM and Clang-only direction, and any failed baseline command is recorded as either an environment limitation or an existing regression.

Milestone 1 is accepted when `npm run build:wasm:cpp` can produce C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js and C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm without changing the default Rust `npm run build:wasm`; the generated ES module can be imported by a TypeScript adapter test; the Emdawnwebgpu compile/link path is proven or blocked with evidence; and the exact emsdk/Emscripten version is pinned in source control.

Milestone 2 is accepted when C++ owns frame count, debug status JSON, lifecycle/dispose behavior, and resize validation. Native C++ tests and TypeScript Mocha tests must both pass. `BrowserGameRuntime` in TypeScript must keep the same application-facing methods and status field names.

Milestone 3A is accepted when browser automation can create a C++ runtime from the TypeScript-owned canvas, await adapter/device/surface setup, read camelCase status JSON, resize to nonzero and zero dimensions, dispose deterministically, and fail on WebGPU validation or device-loss signals.

Milestone 3B is accepted when the C++/WASM runtime renders a clear-only frame to the browser canvas using the existing clear color, reports surface configuration and durable resource counters, and produces a screenshot/report artifact. WebGPU validation errors, blank frames, device loss, and steady-state durable resource recreation must fail the smoke.

Milestone 3C is accepted when browser smoke passes with the C++/WASM bootstrap triangle, C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png shows the nonblank triangle frame, and debug status reports initialized runtime, canvas size, frame count, surface format, adapter/backend, stable `pipelineCreateCount` / `bufferCreateCount` values, surface configure count, and no fatal error. This milestone proves browser C++ rendering parity; default `npm run build:wasm` and `npm run build` still wait for the native smoke/default-switch milestones.

Milestone 4 is accepted when `npm run smoke:render` either uses a Clang-built C++ native/offscreen renderer to produce C:\dev\ofg\artifacts\render-smoke\bootstrap.png and report JSON, or this plan records a deliberate user-approved temporary native-smoke exception with a follow-up plan and updated API contract. If deferred, browser smoke remains mandatory and command names/docs must make the missing native validation obvious.

Milestone 5 is accepted when active build/test/deploy commands no longer require Cargo, rustup, rustc, wasm-bindgen, or Rust source crates. Search active source and active docs, excluding C:\dev\ofg\docs\archived and migration-baseline notes, for stale Rust command requirements. `npm test`, the active smoke gate, `npm run coverage`, and `npm run build:cloudflare` must pass.

Milestone 6 is accepted when the next renderer/resource implementation plan is C++-first, self-contained, and no active plan asks for new Rust crates.

Final acceptance for this migration:

The first viewport of the app shows the actual running render surface. The C++/WASM runtime owns game state and WebGPU rendering. TypeScript remains a host. Cloudflare packaging includes the generated C++ WASM/JS files and cross-origin isolation headers. Coverage reports include C++ and TypeScript, and modified implementation files either meet the 90% attention threshold or have explicit documented exceptions.

For browser UI, rendering, deployment, or visual output, take screenshots at these minimum points and present them in chat: Rust baseline before C++ rendering changes, C++ clear-only frame in Milestone 3B, C++ triangle frame in Milestone 3C, and packaged/deployment output after packaging paths change. Durable screenshot artifacts should live under C:\dev\ofg\artifacts\browser-smoke or a clearly named subdirectory under C:\dev\ofg\artifacts.

Deployment acceptance after the default switch must inspect C:\dev\ofg\.deploy, verify `_headers`, verify the generated C++ JS/WASM filenames and MIME-served paths, ensure no Rust WASM files are packaged as active runtime files, and run a browser smoke or equivalent static-server check against the packaged output.

## Idempotence and Recovery

Keep Rust code available until the C++ parity gates pass. If a C++ milestone fails, the existing Rust path should still be buildable unless the milestone explicitly reaches the Rust retirement step.

Generated directories C:\dev\ofg\dist, C:\dev\ofg\dist-test, C:\dev\ofg\target, C:\dev\ofg\.deploy, C:\dev\ofg\artifacts, and C:\dev\ofg\assets\wasm can be regenerated by repo scripts. Do not treat generated files as source of truth.

If Emdawnwebgpu cannot expose the needed browser surface/device path cleanly, stop before rewriting rendering through TypeScript. Record the blocker, then choose between a lower-level Emscripten WebGPU C API path, a small JavaScript bridge that only passes browser WebGPU handles into C++, or a temporary spike branch. Any bridge must preserve the rule that TypeScript does not own renderer resources or draw submission.

If native Dawn setup is too expensive for this migration, keep the exception narrow: browser C++ smoke remains required, C++ core tests remain required, and a follow-up native smoke plan must exist before the migration is called fully done.

If Cloudflare build images cannot install or cache emsdk acceptably, update C:\dev\ofg\tools\cloudflare-build.mjs to install a pinned emsdk version on Linux in the same spirit as the current Rust installation path, or document a Pages build-image requirement with exact setup steps.

If generated Emscripten output file names do not match current packaging assumptions, update C:\dev\ofg\src\app\wasmRuntime.ts, C:\dev\ofg\tools\package-site.mjs, and C:\dev\ofg\tools\cloudflare-build.mjs together in the same milestone so local build and deployment do not drift.

C++ WebGPU RAII wrappers must not immediately destroy resources that may still be referenced by submitted command buffers. The initial implementation may use a conservative deferred-release queue that retires old GPU handles after a small number of submitted frames or after an explicit device idle/wait point in native tests. Ordinary frames must not recreate durable resources. The migration status contract currently tracks pipeline, buffer, and surface configuration counts; broader counters for textures, samplers, bind groups, shader modules, upload calls, and upload bytes are a future diagnostics extension so smoke and future perf tests can detect accidental churn.

Upload strategy for bootstrap and near-term renderer work should avoid per-frame `mapAsync`, readbacks, and many tiny transfers. Prefer initialization-time uploads for static bootstrap resources, batched queue writes for dynamic data, and later ring/staging buffers for repeated per-frame data. Any per-frame upload must be visible in counters.

## Artifacts and Notes

Milestone 0 baseline evidence:

    Summary:
    C:\dev\ofg\artifacts\migration-baseline\baseline-summary.md
    C:\dev\ofg\artifacts\migration-baseline\baseline-summary.json

    Commands:
    npm test -> exitCode=0
    npm run smoke -> exitCode=0
    npm run coverage -> exitCode=0

    Generated Rust/WASM size baseline:
    assets/wasm/ofg_web/ofg_web.js -> 45516 bytes
    assets/wasm/ofg_web/ofg_web_bg.wasm -> 270131 bytes

    Browser smoke baseline:
    C:\dev\ofg\artifacts\migration-baseline\browser\bootstrap.png
    C:\dev\ofg\artifacts\migration-baseline\browser\report.json
    C:\dev\ofg\artifacts\migration-baseline\browser\startup-timing.json
    startup to initialized frameCount >= 2 -> 1057.56 ms
    final frameCount -> 14
    backend -> BrowserWebGpu
    surface format -> Bgra8Unorm
    pipelineCreateCount -> 1
    bufferCreateCount -> 1
    surfaceConfigureCount -> 3
    triangleRatio -> 0.2301123595505618
    backgroundRatio -> 0.7698876404494382
    nonBackgroundColorBuckets -> 28

    Native render smoke baseline:
    C:\dev\ofg\artifacts\migration-baseline\render\bootstrap.png
    C:\dev\ofg\artifacts\migration-baseline\render\report.json
    adapter -> NVIDIA GeForce RTX 3050 Ti Laptop GPU
    backend -> Vulkan
    passed -> true

    Coverage baseline:
    C:\dev\ofg\artifacts\migration-baseline\coverage\rust\summary.json
    C:\dev\ofg\artifacts\migration-baseline\coverage\rust\summary.pretty.json
    C:\dev\ofg\artifacts\migration-baseline\coverage\ts\coverage-summary.json

Milestone 0 review:

    Scope: C++/WASM migration plan preparation, AGENTS language/tooling update, archived Rust renderer plan, and generated Rust/WASM baseline artifacts.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegated work.
    Required findings fixed: none.
    Follow-ups recorded: C:\dev\ofg\docs\ARCHITECTURE.md is absent; C:\dev\ofg\docs\SYSTEMS.md is the current system ownership map.
    Rejected findings: none.
    Validation rerun: npm test, npm run smoke, npm run coverage, and git diff --check all passed.
    Remaining risk at the time: Milestone 1 still needed to verify local Clang/Emscripten availability and pin exact toolchain versions before C++ code was introduced. This was resolved in Milestone 1.

Milestone 1 implementation evidence:

    Tool pins:
    C:\dev\ofg\emscripten-version.txt -> 6.0.0
    C:\dev\ofg\llvm-version.txt -> 22.1.8
    C:\dev\ofg\ninja-version.txt -> 1.13.2
    doctest -> v2.5.2 vendored under C:\dev\ofg\cpp\third_party\doctest

    Local tool versions:
    emcc / em++ -> 6.0.0
    browser bundled Clang -> 23.0.0git
    native LLVM/Clang -> 22.1.8
    Ninja -> 1.13.2
    CMake -> 3.26.3

    Commands:
    npm run setup:emscripten -> exitCode=0
    npm run setup:llvm -> exitCode=0
    npm run setup:ninja -> exitCode=0
    npm run build:wasm:cpp -> exitCode=0
    npm run test:cpp -> exitCode=0
    npm test -> exitCode=0
    git diff --check -> exitCode=0

    Generated C++/WASM size:
    assets/wasm/ofg_cpp/ofg_cpp.js -> 46027 bytes
    assets/wasm/ofg_cpp/ofg_cpp.wasm -> 140523 bytes

    Notes:
    `node -e "import('./assets/wasm/ofg_cpp/ofg_cpp.js').then((m)=>console.log(typeof m.default))"` printed `function`.
    The Rust default `npm run build:wasm` and `npm test` path remains unchanged.
    The Emdawnwebgpu link path is proven by C:\dev\ofg\cpp\src\web\emdawn_probe.cpp including `webgpu/webgpu.h` and the CMake target linking with `--use-port=emdawnwebgpu`.

Milestone 1 review:

    Scope: additive C++/WASM toolchain spike, CMake/doctest project, pinned local setup scripts, Emdawnwebgpu link probe, generated C++ WASM artifacts, additive TypeScript C++ wrapper, API/generated-artifact docs, and this ExecPlan.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegated work.
    Required findings fixed: prevented C:\dev\ofg\tools\setup-emscripten.mjs from mutating a user-provided global EMSDK checkout; made command discovery use `which` instead of a shell builtin on non-Windows; changed the placeholder C++ browser runtime so it does not report WebGPU initialized before Milestone 3; reran validation.
    Follow-ups recorded: C++ coverage remains for Milestone 2; browser WebGPU initialization and screenshots remain for Milestone 3A and later; docs still name Rust runtime ownership until the default switch makes C++ ownership true.
    Rejected findings: none.
    Validation rerun: npm run build:wasm:cpp, npm run test:cpp, npm test, generated ES module import probe, and git diff --check all passed.
    Remaining risk: the additive C++ module is only a toolchain/runtime skeleton; it does not yet request a browser WebGPU adapter/device/surface, draw, run browser smoke, or replace Rust coverage.

Milestone 2 implementation and review evidence:

    Added source:
    C:\dev\ofg\cpp\include\ofg\runtime\browser_runtime.hpp
    C:\dev\ofg\cpp\src\runtime\browser_runtime.cpp
    C:\dev\ofg\cpp\tests\browser_runtime_test.cpp
    C:\dev\ofg\tools\cpp-coverage.mjs
    C:\dev\ofg\tools\setup-llvm.mjs
    C:\dev\ofg\llvm-version.txt
    C:\dev\ofg\docs\coverage\cpp-summary.json

    Commands:
    npm run setup:llvm -> exitCode=0
    npm run build:wasm:cpp -> exitCode=0
    npm run test:cpp -> exitCode=0
    npm run coverage:cpp -> exitCode=0
    npm test -> exitCode=0
    git diff --check -> exitCode=0

    Generated C++/WASM size after Milestone 2:
    assets/wasm/ofg_cpp/ofg_cpp.js -> 46027 bytes
    assets/wasm/ofg_cpp/ofg_cpp.wasm -> 143085 bytes

    C++ coverage:
    C:\dev\ofg\artifacts\coverage\cpp\cpp-summary.json
    C:\dev\ofg\docs\coverage\cpp-summary.json
    cpp/src/core/frame_state.cpp -> 100.00%
    cpp/src/runtime/browser_runtime.cpp -> 100.00%
    cpp/src/runtime/runtime_debug_status.cpp -> 100.00%

    Milestone review:
    Scope: portable C++ non-render runtime state, status JSON, resize validation, dispose lifecycle, doctest/CTest tests, TypeScript C++ adapter tests, native LLVM setup, C++ coverage gate, and active docs.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegated work.
    Required findings fixed: made `npm run coverage:cpp` require the pinned desktop LLVM bundle instead of falling back to emsdk's native Clang, because emsdk lacks a working Windows native source-coverage runtime.
    Follow-ups recorded: browser-only C++ WebGPU/Embind files remain coverage exceptions until Milestone 3 browser smoke; default `npm run coverage` still runs Rust plus TypeScript until the runtime switch.
    Rejected findings: none.
    Validation rerun: npm run build:wasm:cpp, npm run test:cpp, npm run coverage:cpp, npm test, and git diff --check all passed.
    Remaining risk at the time: C++ runtime status remained non-render and reported `initialized: false` until Milestone 3A added browser WebGPU adapter/device/surface setup.

Milestone 3A implementation evidence:

    Added or changed source:
    C:\dev\ofg\cpp\src\web\browser_game.cpp
    C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
    C:\dev\ofg\cpp\src\runtime\browser_runtime.cpp
    C:\dev\ofg\cpp\include\ofg\runtime\browser_runtime.hpp
    C:\dev\ofg\cpp\tests\browser_runtime_test.cpp
    C:\dev\ofg\tools\browser-smoke-cpp.mjs
    C:\dev\ofg\tools\cpp-webgpu-smoke.html

    Commands:
    npm run smoke:browser:cpp -> exitCode=0
    npm run coverage:cpp -> exitCode=0
    npm test -> exitCode=0

    Generated C++/WASM size after Milestone 3A:
    assets/wasm/ofg_cpp/ofg_cpp.js -> 78491 bytes
    assets/wasm/ofg_cpp/ofg_cpp.wasm -> 171382 bytes

    C++ browser WebGPU smoke:
    C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json
    browserSignals.webgpu -> true
    browserSignals.isolated -> true
    initialStatus.initialized -> true
    initialStatus.canvasWidth/canvasHeight -> 640x360
    initialStatus.backend -> BrowserWebGpu
    initialStatus.surfaceFormat -> Bgra8Unorm
    initialStatus.adapterName -> intel
    initialStatus.surfaceConfigureCount -> 1
    resizedStatus.canvasWidth/canvasHeight -> 320x180
    resizedStatus.surfaceConfigureCount -> 2
    zeroSizeStatus.initialized -> false
    zeroSizeStatus.lastError -> null
    recoveredStatus.initialized -> true
    recoveredStatus.surfaceConfigureCount -> 3

    C++ coverage after Milestone 3A:
    C:\dev\ofg\artifacts\coverage\cpp\cpp-summary.json
    C:\dev\ofg\docs\coverage\cpp-summary.json
    cpp/src/core/frame_state.cpp -> 100.00%
    cpp/src/runtime/browser_runtime.cpp -> 100.00%
    cpp/src/runtime/runtime_debug_status.cpp -> 100.00%

Milestone 3A review:

    Scope: additive C++/WASM browser WebGPU setup, Emdawn surface/adapter/device/queue ownership, runtime initialized status transitions, focused C++ browser smoke, C++ coverage/docs updates, and active command documentation.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: updated C:\dev\ofg\AGENTS.md with the new C++ setup/build/test/smoke/coverage commands; reworded stale Milestone 2 retrospective/risk text so it no longer reads as the current state; added C++ WebGPU device-lost and uncaptured-error callbacks so validation or device-loss signals report through runtime status; reran validation.
    Follow-ups recorded: Milestones 3B and 3C must not call browser `wgpuSurfacePresent`, because Emdawnwebgpu aborts there; C++ still needs triangle rendering, native Dawn smoke or approved exception, default runtime switch, packaging updates, and Rust retirement.
    Rejected findings: none.
    Validation rerun: npm run build:wasm:cpp, npm run smoke:browser:cpp, npm run coverage:cpp, npm test, and git diff --check all passed.
    Remaining risk at the time: Milestone 3A proved browser WebGPU initialization and surface configuration only. C++ draw submission and a pixel screenshot were added in Milestone 3B, but durable triangle render resources, native Dawn path, deployment packaging switch, and default runtime replacement remain.

Milestone 3B implementation evidence:

    Added or changed source:
    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\src\web\browser_game.cpp
    C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
    C:\dev\ofg\cpp\src\web\webgpu_utils.cpp
    C:\dev\ofg\cpp\include\ofg\web\webgpu_utils.hpp
    C:\dev\ofg\cpp\src\web\embind_module.cpp
    C:\dev\ofg\cpp\src\web\emdawn_probe.cpp
    C:\dev\ofg\tools\browser-smoke-cpp.mjs

    Commands:
    npm run build:wasm:cpp -> exitCode=0
    npm run smoke:browser:cpp -> exitCode=0
    npm run coverage:cpp -> exitCode=0
    npm test -> exitCode=0

    Generated C++/WASM size after Milestone 3B:
    assets/wasm/ofg_cpp/ofg_cpp.js -> 83218 bytes
    assets/wasm/ofg_cpp/ofg_cpp.wasm -> 173363 bytes

    C++ clear-only browser smoke:
    C:\dev\ofg\artifacts\browser-smoke-cpp\clear.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json
    recoveredStatus.initialized -> true
    recoveredStatus.surfaceConfigureCount -> 3
    recoveredStatus.pipelineCreateCount -> 0
    recoveredStatus.bufferCreateCount -> 0
    pixels.width/height -> 640x360
    pixels.sampledPixels -> 25680
    pixels.clearPixels -> 25680
    pixels.nonClearPixels -> 0
    pixels.clearRatio -> 1

Milestone 3B review:

    Scope: additive C++/WASM clear-only WebGPU frame submission, C++ browser smoke screenshot/pixel classification, WebGPU helper split, C++ coverage, TypeScript adapter preservation, active docs, and this ExecPlan.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: split browser-only WebGPU string/enum/format helpers from C:\dev\ofg\cpp\src\web\browser_game.cpp into C:\dev\ofg\cpp\include\ofg\web\webgpu_utils.hpp and C:\dev\ofg\cpp\src\web\webgpu_utils.cpp, bringing C:\dev\ofg\cpp\src\web\browser_game.cpp from 674 lines to 514 before Milestone 3C adds triangle rendering; added purpose comments to browser boundary files; removed stale plan wording that implied browser `wgpuSurfacePresent` was called.
    Follow-ups recorded: Milestone 3C still needs durable shader, pipeline, vertex-buffer resources, triangle screenshot classification, and nonzero pipeline/buffer counters; Milestone 4 still needs native Dawn smoke or an explicit exception; later milestones still need default runtime switch, packaging/deployment verification, and Rust retirement.
    Rejected findings: none.
    Validation rerun: npm run smoke:browser:cpp, npm run coverage:cpp, npm test, and git diff --check all passed.
    Remaining risk: Milestone 3B proves clear-only browser command submission and per-frame handle release, but it still has no triangle geometry, durable render resources, native Dawn path, default app switch, or deployable C++ package path.

Milestone 3C implementation evidence:

    Added or changed source:
    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\include\ofg\render\bootstrap_scene.hpp
    C:\dev\ofg\cpp\src\render\bootstrap_scene.cpp
    C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp
    C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp
    C:\dev\ofg\cpp\include\ofg\runtime\browser_runtime.hpp
    C:\dev\ofg\cpp\src\runtime\browser_runtime.cpp
    C:\dev\ofg\cpp\tests\bootstrap_scene_test.cpp
    C:\dev\ofg\cpp\tests\browser_runtime_test.cpp
    C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp
    C:\dev\ofg\cpp\src\web\browser_game.cpp
    C:\dev\ofg\tools\browser-smoke-cpp.mjs

    Commands:
    npm run test:cpp -> exitCode=0
    npm run build:wasm:cpp -> exitCode=0
    npm run smoke:browser:cpp -> exitCode=0
    npm run coverage:cpp -> exitCode=0
    npm test -> exitCode=0

    Generated C++/WASM size after Milestone 3C:
    assets/wasm/ofg_cpp/ofg_cpp.js -> 86211 bytes
    assets/wasm/ofg_cpp/ofg_cpp.wasm -> 176816 bytes

    C++ bootstrap triangle browser smoke:
    C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json
    recoveredStatus.initialized -> true
    recoveredStatus.pipelineCreateCount -> 1
    recoveredStatus.bufferCreateCount -> 1
    recoveredStatus.surfaceConfigureCount -> 3
    recoveredStatus.lastError -> null
    pixels.width/height -> 640x360
    pixels.sampledPixels -> 25680
    pixels.trianglePixels -> 5852
    pixels.backgroundPixels -> 19828
    pixels.triangleRatio -> 0.2278816199376947
    pixels.backgroundRatio -> 0.7721183800623053
    pixels.nonBackgroundColorBuckets -> 27

    C++ coverage after Milestone 3C:
    C:\dev\ofg\artifacts\coverage\cpp\cpp-summary.json
    C:\dev\ofg\docs\coverage\cpp-summary.json
    cpp/src/core/frame_state.cpp -> 100.00%
    cpp/src/render/bootstrap_scene.cpp -> 100.00%
    cpp/src/runtime/browser_runtime.cpp -> 100.00%
    cpp/src/runtime/runtime_debug_status.cpp -> 100.00%

Milestone 3C review:

    Scope: portable C++ bootstrap scene data, browser-only C++ WebGPU bootstrap renderer, BrowserGame renderer wiring, runtime resource counters, C++ triangle browser smoke screenshot/pixel classification, C++ coverage wrapper/docs, active contracts, and this ExecPlan.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: expanded C:\dev\ofg\tools\cpp-coverage.mjs so C:\dev\ofg\cpp\src\render\bootstrap_scene.cpp is checked by `npm run coverage:cpp`; updated C++ coverage docs for the new checked render-scene file and browser-only renderer exception; changed a stale WebGPU encoder label from "clear" to "bootstrap"; clarified this plan so shader/upload counters remain a future diagnostics extension rather than a Milestone 3C public status requirement.
    Follow-ups recorded: Milestone 4 still needs a Clang-built native Dawn smoke path or an explicit temporary exception; later milestones still need default runtime switch, packaging/deployment verification, Rust command retirement, and broader GPU performance/upload counters.
    Rejected findings: none.
    Validation rerun: npm run test:cpp, npm run build:wasm:cpp, npm run smoke:browser:cpp, npm run coverage:cpp, npm test, and git diff --check all passed.
    Remaining risk: Milestone 3C proves browser C++ rendering parity only. Native Dawn/offscreen smoke, default app/deploy switch, and Rust retirement remain open.

Milestone 4 implementation evidence:

    Added or changed source:
    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\include\ofg\native\png_writer.hpp
    C:\dev\ofg\cpp\include\ofg\native\render_smoke.hpp
    C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp
    C:\dev\ofg\cpp\include\ofg\render\webgpu_common.hpp
    C:\dev\ofg\cpp\src\native\png_writer.cpp
    C:\dev\ofg\cpp\src\native\render_smoke.cpp
    C:\dev\ofg\cpp\src\native\render_smoke_main.cpp
    C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp
    C:\dev\ofg\cpp\src\render\webgpu_common.cpp
    C:\dev\ofg\tools\setup-dawn.mjs
    C:\dev\ofg\tools\smoke-render-cpp.mjs
    C:\dev\ofg\dawn-version.txt
    C:\dev\ofg\package.json
    C:\dev\ofg\AGENTS.md
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md

    Comment/readability pass also touched the C++ migration core/runtime/web headers and sources, doctest files, TypeScript C++ adapter, C++ browser smoke fixture/script, and C++ setup/build/test/coverage scripts so newly written functions and files have explicit purpose comments.

    Commands:
    npm run test:cpp -> exitCode=0
    npm run build:wasm:cpp -> exitCode=0
    npm run smoke:browser:cpp -> exitCode=0
    npm run smoke:render -> exitCode=0
    npm run coverage:cpp -> exitCode=0
    npm test -> exitCode=0
    npm run smoke -> exitCode=0
    git diff --check -> exitCode=0, with only LF-to-CRLF warnings for .gitignore and package.json

    Native C++ Dawn render smoke:
    C:\dev\ofg\artifacts\render-smoke\bootstrap.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    width/height -> 800x450
    textureFormat -> Rgba8Unorm
    adapterName -> NVIDIA GeForce RTX 3050 Ti Laptop GPU
    backend -> Vulkan
    sampledPixels -> 40050
    trianglePixels -> 9216
    backgroundPixels -> 30834
    triangleRatio -> 0.230112
    backgroundRatio -> 0.769888
    nonBackgroundColorBuckets -> 28
    passed -> true

    Focused C++ browser smoke after the comment pass:
    C:\dev\ofg\artifacts\browser-smoke-cpp\triangle.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json
    backend -> BrowserWebGpu
    surfaceFormat -> Bgra8Unorm
    pipelineCreateCount -> 1
    bufferCreateCount -> 1
    recoveredStatus.initialized -> true
    pixels.triangleRatio -> 0.2278816199376947
    pixels.backgroundRatio -> 0.7721183800623053
    pixels.nonBackgroundColorBuckets -> 27

    Notes:
    Browser C++/WASM remains on Emdawnwebgpu. Native Dawn is used only for offline render smoke.
    The first native Dawn build is large and cached under C:\dev\ofg\artifacts\build\cpp-render-smoke.

Milestone 4 review:

    Scope: Clang-native C++ Dawn render smoke, pinned Dawn setup, native PNG/report helpers, shared WebGPU renderer use in browser/native paths, replacement of `npm run smoke:render`, active command/docs/contracts updates, and the comment/readability rule across newly written migration files.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: kept Dawn native-only/offline while preserving browser Emdawnwebgpu ownership; fixed a native command-encoder lifetime issue so `wgpuCommandEncoderRelease` still runs after `wgpuCommandEncoderFinish`; expanded function/file comments across the new C++ migration code, tests, TypeScript adapter, smoke fixture, and tooling after the user flagged the readability rule; reran validation.
    Follow-ups recorded: C:\dev\ofg\cpp\src\native\render_smoke.cpp is in the 500-1000 line concern band and should be split into GPU setup/readback, pixel/report writing, and argument parsing before more native render behavior is added.
    Rejected findings: none.
    Validation rerun: npm run test:cpp, npm run build:wasm:cpp, npm run smoke:browser:cpp, npm run smoke:render, npm run coverage:cpp, npm test, npm run smoke, and git diff --check all passed.
    Remaining risk: the first Dawn build is still heavy; the native smoke is currently a Windows/Vulkan path with D3D disabled because this machine's Windows SDK is too old for the pinned Dawn D3D12 backend. Default app/deploy commands still use Rust until Milestone 5.

Milestone 5 implementation and review evidence:

    Added or changed active runtime/build/deploy files:
    C:\dev\ofg\src\app\wasmRuntime.ts
    C:\dev\ofg\src\app\main.ts
    C:\dev\ofg\package.json
    C:\dev\ofg\tools\build-cpp-wasm.mjs
    C:\dev\ofg\tools\cloudflare-build.mjs
    C:\dev\ofg\tools\package-site.mjs
    C:\dev\ofg\tools\ts-coverage.mjs
    C:\dev\ofg\tools\dev-server.mjs
    C:\dev\ofg\tools\browser-smoke.mjs
    C:\dev\ofg\tests\ts\wasmRuntime.test.ts
    C:\dev\ofg\tests\ts\ownershipBoundary.test.ts
    C:\dev\ofg\AGENTS.md
    C:\dev\ofg\README.md
    C:\dev\ofg\COVERAGE.md
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md
    C:\dev\ofg\docs\coverage\latest.md

    Removed active Rust source/tooling:
    C:\dev\ofg\Cargo.toml
    C:\dev\ofg\Cargo.lock
    C:\dev\ofg\rust-toolchain.toml
    C:\dev\ofg\.cargo\config.toml
    C:\dev\ofg\crates\...
    C:\dev\ofg\tools\build-wasm.mjs
    C:\dev\ofg\tools\rust-coverage.mjs
    C:\dev\ofg\tools\wasm-bindgen-version.mjs
    C:\dev\ofg\docs\coverage\rust-summary.pretty.json

    Default command results:
    npm run build -> exitCode=0, builds C++/WASM and TypeScript
    npm test -> exitCode=0, runs `test:cpp` and `test:ts`
    npm run smoke -> exitCode=0, runs default C++ browser smoke plus native Dawn smoke
    npm run coverage -> exitCode=0, runs C++ and TypeScript coverage
    npm run package:site -> exitCode=0
    npm run build:cloudflare -> exitCode=0, packaged WASM size 176816 bytes
    OFG_DEV_ROOT=C:\dev\ofg\.deploy node tools/browser-smoke.mjs -> exitCode=0
    git diff --check -> exitCode=0, with only LF-to-CRLF warnings for .gitignore, docs/coverage/ts-coverage-summary.json, and package.json

    Default browser smoke after the switch:
    C:\dev\ofg\artifacts\browser-smoke\bootstrap.png
    C:\dev\ofg\artifacts\browser-smoke\report.json
    backend -> BrowserWebGpu
    surfaceFormat -> Bgra8Unorm
    pipelineCreateCount -> 1
    bufferCreateCount -> 1
    triangleRatio -> 0.2301123595505618
    backgroundRatio -> 0.7698876404494382
    nonBackgroundColorBuckets -> 28

    Deployment package after the switch:
    C:\dev\ofg\.deploy\_headers
    C:\dev\ofg\.deploy\assets\wasm\ofg_cpp\ofg_cpp.js
    C:\dev\ofg\.deploy\assets\wasm\ofg_cpp\ofg_cpp.wasm
    C:\dev\ofg\.deploy\dist\app\canvasHost.js
    C:\dev\ofg\.deploy\dist\app\main.js
    C:\dev\ofg\.deploy\dist\app\wasmRuntime.js
    C:\dev\ofg\.deploy\index.html
    C:\dev\ofg\.deploy\src\app\styles.css

    Milestone review:
    Scope: default C++/WASM runtime switch, Rust source/tool retirement, package/deploy output switch, active docs/contracts/coverage updates, default browser/native smoke, packaged-output smoke, and stale Rust command search.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: updated `tools/ts-coverage.mjs` to build C++/WASM after it tried to call removed `tools/build-wasm.mjs`; added `OFG_DEV_ROOT` support to `tools/dev-server.mjs` and used it to smoke packaged `.deploy` output; refreshed active docs and committed coverage summaries; removed old generated Rust output directories before trimming .gitignore.
    Follow-ups recorded: `npm run build:cloudflare` passes, but Windows `setup:emscripten` still emits a Node `[DEP0190]` warning because it invokes `emsdk.bat` through shell mode; clean this up before hardening CI logs. C:\dev\ofg\cpp\src\native\render_smoke.cpp still needs the split recorded in the Milestone 4 review before native render behavior grows.
    Rejected findings: none.
    Validation rerun: npm test, npm run smoke, npm run coverage, npm run build:cloudflare, packaged-output browser smoke through `OFG_DEV_ROOT=.deploy`, stale Rust command search excluding this migration plan/archives, and git diff --check all passed.
    Remaining risk: default build and deploy now depend on Emscripten/Ninja setup on clean machines; `build:cloudflare` installs/checks them, but first-run downloads are heavier than the retired Rust path.

Milestone 6 implementation and review evidence:

    New active follow-up plan:
    C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md

    The replacement plan keeps these design goals from the archived renderer/resource plan:
    typed asset handles
    generic typed stores
    mutable Texture, Shader, Material, and Mesh resources
    PropertyBag for material and draw-scope parameters
    DrawList and OpaqueRenderer
    browser and native smoke rendering a ground plane plus animated cubes

    The replacement plan changes these implementation assumptions:
    C++20 under C:\dev\ofg\cpp, not a new Rust workspace member
    webgpu.h handles and explicit WGPUDevice/WGPUQueue context, not Rust wgpu types
    doctest/CTest and npm run test:cpp, not cargo test
    npm run coverage:cpp through Clang/LLVM, not Rust coverage
    browser WebGPU through Emdawnwebgpu and native/offline rendering through pinned Dawn
    comment/readability gates for every function and top-of-file purpose comments

    Milestone review:
    Scope: C++ translation of the archived renderer/resource plan, active plan status, stale language/tooling references, API contract coverage, validation commands, and comment/readability acceptance.
    Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not used because this milestone review was required by the ExecPlan workflow rather than explicitly requested as a delegated sub-agent review.
    Required findings fixed: removed a stale "crate" wording from the replacement plan; changed undefined `Result<T, E>` interface sketches to match the current C++ `std::optional`/`std::unique_ptr`/`bool` plus `std::string& error` style; added explicit decisions for generated-pixel textures, local math types, and error-reporting style.
    Follow-ups recorded: C:\dev\ofg\cpp\src\native\render_smoke.cpp should be split before renderer-resource implementation expands native smoke behavior; Windows setup:emscripten still has the recorded Node [DEP0190] warning cleanup before CI hardening.
    Rejected findings: none.
    Validation rerun: stale Rust/Cargo/crate/tooling search against C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md found no matches; git diff --check passed with only the existing LF-to-CRLF warnings.
    Remaining risk: this milestone is a plan translation only. Implementation risk moves to the new renderer/resource plan, especially around per-draw uniform packing, durable resource lifetime, and keeping browser/native smoke expectations aligned.

Final migration completion audit:

    npm test -> exitCode=0
    npm run smoke -> exitCode=0
    npm run coverage -> exitCode=0
    npm run build:cloudflare -> exitCode=0, packaged WASM size 176816 bytes
    OFG_DEV_ROOT=C:\dev\ofg\.deploy node tools/browser-smoke.mjs -> exitCode=0
    Active plan stale retired-tooling search -> no matches
    git diff --check -> exitCode=0, with only LF-to-CRLF warnings for .gitignore, docs/coverage/ts-coverage-summary.json, and package.json

    Final browser smoke report:
    C:\dev\ofg\artifacts\browser-smoke\bootstrap.png
    C:\dev\ofg\artifacts\browser-smoke\report.json
    backend -> BrowserWebGpu
    surfaceFormat -> Bgra8Unorm
    pipelineCreateCount -> 1
    bufferCreateCount -> 1
    triangleRatio -> 0.2301123595505618
    backgroundRatio -> 0.7698876404494382
    nonBackgroundColorBuckets -> 28

    Final native smoke report:
    C:\dev\ofg\artifacts\render-smoke\bootstrap.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    backend -> Vulkan
    textureFormat -> Rgba8Unorm
    passed -> true
    triangleRatio -> 0.230112
    backgroundRatio -> 0.769888
    nonBackgroundColorBuckets -> 28

Useful external references checked while drafting this plan:

    Emscripten WebGPU support:
    https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html

    Emscripten SDK:
    https://emscripten.org/docs/tools_reference/emsdk.html

    Emscripten modularized ES module output:
    https://emscripten.org/docs/compiling/Modularized-Output.html

    Emscripten Embind:
    https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html

    Emdawnwebgpu:
    https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md

    Chrome C++ WebGPU app guide:
    https://developer.chrome.com/docs/web-platform/webgpu/build-app

    webgpu.h headers:
    https://github.com/webgpu-native/webgpu-headers

    Dawn overview:
    https://dawn.googlesource.com/dawn

    CMake:
    https://cmake.org/

    CTest:
    https://cmake.org/cmake/help/latest/manual/ctest.1.html

    doctest:
    https://github.com/doctest/doctest

    Clang source-based coverage:
    https://clang.llvm.org/docs/SourceBasedCodeCoverage.html

Expected durable implementation artifacts after migration:

    C:\dev\ofg\emscripten-version.txt
    C:\dev\ofg\llvm-version.txt
    C:\dev\ofg\ninja-version.txt
    C:\dev\ofg\cpp\CMakeLists.txt
    C:\dev\ofg\cpp\include\ofg\...
    C:\dev\ofg\cpp\src\...
    C:\dev\ofg\cpp\tests\...
    C:\dev\ofg\cpp\third_party\doctest\doctest.h
    C:\dev\ofg\cpp\third_party\doctest\LICENSE.txt
    C:\dev\ofg\tools\build-cpp-wasm.mjs
    C:\dev\ofg\tools\setup-emscripten.mjs
    C:\dev\ofg\tools\setup-llvm.mjs
    C:\dev\ofg\tools\setup-ninja.mjs
    C:\dev\ofg\tools\test-cpp.mjs
    C:\dev\ofg\tools\build-wasm.mjs
    C:\dev\ofg\tools\cpp-coverage.mjs
    C:\dev\ofg\tools\browser-smoke.mjs
    C:\dev\ofg\tools\package-site.mjs
    C:\dev\ofg\tools\cloudflare-build.mjs
    C:\dev\ofg\src\app\wasmRuntime.ts
    C:\dev\ofg\src\app\wasmRuntimeCpp.ts
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md
    C:\dev\ofg\COVERAGE.md
    C:\dev\ofg\README.md

Expected generated artifacts:

    C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js
    C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm
    C:\dev\ofg\artifacts\build\cpp-native\...
    C:\dev\ofg\artifacts\build\cpp-wasm\...
    C:\dev\ofg\artifacts\build\cpp-coverage\...
    C:\dev\ofg\artifacts\migration-baseline\...
    C:\dev\ofg\artifacts\browser-smoke\bootstrap.png
    C:\dev\ofg\artifacts\browser-smoke\report.json
    C:\dev\ofg\artifacts\render-smoke\bootstrap.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\coverage\cpp\cpp-summary.json

## Interfaces and Dependencies

The external TypeScript interface should remain:

    export interface BrowserGameRuntime {
      resize(width: number, height: number, devicePixelRatio: number): void;
      frame(timeMs: number): void;
      debugStatus(): RuntimeDebugStatus;
      dispose(): void;
    }

The TypeScript adapter maps Emscripten/Embind names to a stable application-facing shape. The expected mapping is:

    Public TypeScript method       Raw generated/adapter method       C++ method / source
    createBrowserGameRuntime       createOfgCppModule + create        async C++/JS bridge that creates BrowserGame
    resize                         resize                             BrowserGame::resize
    frame                          frame                              BrowserGame::frame
    debugStatus                    debug_status_json                  BrowserGame::debug_status_json
    dispose                        dispose + delete                   BrowserGame::dispose, then Embind delete()

`free()` is wasm-bindgen-specific and should disappear after the default switch to Emscripten. During the additive phase, TypeScript tests may contain both fake raw shapes, but the application-facing `BrowserGameRuntime` remains stable.

The normalized raw C++ browser game shape should be:

    interface RawCppBrowserGame {
      resize(width: number, height: number, devicePixelRatio: number): void;
      frame(timeMs: number): void;
      debug_status_json(): string;
      dispose(): void;
      delete(): void;
    }

The generated C++ module is loaded from `/assets/wasm/ofg_cpp/ofg_cpp.js`. It should be emitted as a modularized ES module whose default export is `createOfgCppModule`. The TypeScript adapter should do the equivalent of:

    import createOfgCppModule from "/assets/wasm/ofg_cpp/ofg_cpp.js";

    const module = await createOfgCppModule({
      locateFile(path: string) {
        return `/assets/wasm/ofg_cpp/${path}`;
      }
    });
    const raw = await module.BrowserGame.create(canvas);

Async WebGPU setup lives behind `module.BrowserGame.create(canvas)`. TypeScript may pass the canvas and await the result, but it must not own adapter/device/surface/pipeline/draw submission. Avoid Emscripten Asyncify unless a future plan explicitly budgets its size and performance cost.

The C++ runtime should expose a narrow browser class similar to:

    namespace ofg {
    class BrowserGame {
    public:
      static std::shared_ptr<BrowserGame> create(emscripten::val canvas);
      void resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio);
      void frame(double time_ms);
      std::string debug_status_json() const;
      void dispose();
    };
    }

Embind ownership should be deterministic: JavaScript owns the bound `BrowserGame` object after creation, calls `dispose()` for GPU/runtime teardown, and then calls Embind `delete()` exactly once. C++ methods should return clear errors after disposal, and TypeScript should guard against double disposal.

The TypeScript adapter may wrap a synchronous Embind `create` result with `Promise.resolve(...)` during early milestones. If later WebGPU setup requires asynchronous browser calls that Embind cannot expose cleanly, the generated JavaScript adapter may provide the promise-returning `create` function while C++ still owns the runtime object and renderer.

Portable C++ core interfaces should be plain C++ and testable without a browser:

    namespace ofg {
    class FrameState {
    public:
      void tick(double time_ms);
      std::uint64_t frame_count() const;
      double last_time_ms() const;
    };

    struct RuntimeDebugStatus {
      bool initialized;
      std::uint64_t frame_count;
      std::uint32_t canvas_width;
      std::uint32_t canvas_height;
      double device_pixel_ratio;
      std::string surface_format;
      std::string adapter_name;
      std::string backend;
      std::uint32_t pipeline_create_count;
      std::uint32_t buffer_create_count;
      std::uint32_t surface_configure_count;
      std::optional<std::string> last_error;

      std::string to_json() const;
      static RuntimeDebugStatus uninitialized(std::string message);
    };
    }

`RuntimeDebugStatus::to_json()` must emit the existing camelCase browser contract. A representative payload is:

    {
      "initialized": true,
      "frameCount": 2,
      "canvasWidth": 800,
      "canvasHeight": 450,
      "devicePixelRatio": 1,
      "surfaceFormat": "Bgra8UnormSrgb",
      "adapterName": "test adapter",
      "backend": "BrowserWebGpu",
      "pipelineCreateCount": 1,
      "bufferCreateCount": 1,
      "surfaceConfigureCount": 1,
      "lastError": null
    }

Zero-size behavior must match the current recoverable browser contract. `resize(0, height, dpr)` or `resize(width, 0, dpr)` records the zero dimension, clears or avoids surface configuration, reports `initialized: false`, preserves the valid DPR, leaves `lastError: null`, and lets `frame(timeMs)` advance frame state or return success while skipping render-target acquisition.

The first render interface should be small:

    namespace ofg {
    struct RendererCounters {
      std::uint32_t pipeline_create_count;
      std::uint32_t buffer_create_count;
    };

    class BootstrapRenderer {
    public:
      BootstrapRenderer(WGPUDevice device, WGPUTextureFormat format);
      void render_to_view(WGPUCommandEncoder encoder, WGPUTextureView view);
      RendererCounters counters() const;
    };
    }

Dependencies to prescribe or spike:

C++ standard: C++20.

Compiler: Clang only. Native builds use desktop Clang/LLVM; browser builds use Emscripten's Clang. MSVC is not an OFG C++ build target.

Build: CMake orchestrated by npm scripts. CMake presets or script arguments should force Clang for native build and coverage configurations.

Browser WASM: Emscripten with ES module/modularized output, generated as `/assets/wasm/ofg_cpp/ofg_cpp.js` and `/assets/wasm/ofg_cpp/ofg_cpp.wasm`.

JS binding: Embind for the narrow BrowserGame facade, unless the Milestone 1 spike proves a smaller C ABI plus JS adapter is cleaner. Embind must remain facade-only and off hot paths.

Browser WebGPU: Emdawnwebgpu and `webgpu.h`.

Native WebGPU smoke: Dawn native through the same or closely related WebGPU C/C++ API, preferred but allowed to become a documented follow-up if it dominates the migration.

JSON: use a small, tested local serializer for the fixed `RuntimeDebugStatus` schema. Do not hand-concatenate unescaped strings for runtime status.

Testing: native C++ tests should use doctest test executables registered with CTest behind `npm run test:cpp`; TypeScript tests remain Mocha.

Coverage: C++ coverage uses Clang source-based coverage, `llvm-profdata`, and `llvm-cov`, and produces machine-readable summaries under C:\dev\ofg\artifacts\coverage, with committed latest summaries under C:\dev\ofg\docs\coverage.

Performance gates: track deployable JS/WASM byte size, gzip/Brotli size when available, startup-to-first-frame timing, JS/WASM calls per animation frame, durable GPU object creation counts, upload calls/bytes, and WebGPU validation/device-loss signals. Initial migration should stay single-threaded; pthreads, SIMD, workers, and shared memory are future optimization plans unless explicitly added later.
