# Systems

This is a living document for independent OFG systems. When a new system is created, add a section that names what it owns, its public interfaces, and the contracts it relies on when communicating with other systems.

The active cross-system contracts are recorded in `docs/API_CONTRACTS.md`. This file is the practical map of those contracts to concrete modules, commands, and artifacts.

## BrowserHost

The browser host owns the web page shell. It creates or finds the canvas, sizes it from the viewport and device pixel ratio, reports fatal startup errors, loads the C++/WASM module, and provides the local static dev server. It does not own gameplay simulation, scene graph data, GPU pipeline creation, or draw submission.

Public interfaces:

- `src/app/canvasHost.ts` exposes the canvas host factory and host operations used by the TypeScript entrypoint.
- `src/app/main.ts` boots the browser page, loads the WASM runtime, wires resize/frame callbacks, and reports fatal errors.
- `src/app/wasmRuntime.ts` adapts the generated Emscripten/Embind module to the stable `BrowserGameRuntime` TypeScript interface.
- `tools/dev-server.mjs` serves the built static app for human review and browser smoke.
- `index.html` and `src/app/styles.css` provide the minimal browser shell.

Communication contracts:

- BrowserHost supplies physical canvas width, physical canvas height, and clamped device pixel ratio to the C++ `BrowserGame` facade.
- BrowserHost must preserve zero-size canvas axes. Zero size is a recoverable state that C++ reports through debug status instead of a host-side failure.
- BrowserHost may inspect runtime debug JSON and display fatal startup failures, but must not reach into renderer internals or generated WASM glue.

## CppRuntime

The C++ runtime is the C++/WASM facade exposed to TypeScript. `BrowserGame` owns the browser-specific frame driver: WebGPU instance/adapter/device/surface setup, surface resize policy, surface texture acquisition, command encoder creation, command-buffer finish, queue submit, and Embind lifecycle ownership. Shared `Game` owns frame counting, debug-status serialization, the stable resource arena, demo-scene state, draw-list construction, target validation, and render command recording for one WebGPU device lifetime.

Public interfaces:

- `cpp/CMakeLists.txt` defines the C++ core library, doctest executable, CTest registration, browser Emscripten module, and native Dawn smoke target.
- `cpp/include/ofg/game/game.hpp` exposes the shared device-bound `Game` object used by browser and native frame drivers.
- `cpp/include/ofg/game/game_runtime.hpp` owns portable runtime state, lifecycle behavior, debug status, and renderer counters.
- `cpp/include/ofg/web/browser_game.hpp` exposes the Embind-facing `BrowserGame` facade.
- `tools/build-cpp-wasm.mjs` builds `assets/wasm/ofg_cpp/ofg_cpp.js` and `assets/wasm/ofg_cpp/ofg_cpp.wasm` through CMake and Emscripten.
- `tools/test-cpp.mjs` runs the C++ doctest executable through CMake/CTest.
- `src/app/wasmRuntime.ts` loads `/assets/wasm/ofg_cpp/ofg_cpp.js`, resolves `ofg_cpp.wasm`, and maps Embind `delete()` into the app runtime interface.
- Generated files under `assets/wasm/ofg_cpp/` are runtime artifacts produced by `npm run build:wasm`.

Communication contracts:

- CppRuntime consumes only the canvas and resize/frame calls supplied by BrowserHost.
- `BrowserGame` delegates frame state and render command recording to `Game`, while keeping browser surface acquisition and one queue submit per frame in the browser frame driver.
- The `debug_status_json()` fields are a public inspection contract for TypeScript tests, Playwright smoke, and later diagnostics.
- `dispose()` destroys `Game` before releasing borrowed WebGPU device and queue handles, then releases browser WebGPU resources.

## CppRenderer

The C++ renderer owns the current high-level render resources and draw-list renderer used behind shared `Game` in browser and native frame drivers. `Game` owns a `ResourceArena`, `DemoScene`, `DrawList`, `RenderView`, and `Renderer`. The demo scene creates a mipmapped checker texture, a white texture, opaque materials, a ground mesh, a cube mesh, and per-frame draw commands. The renderer owns the opaque pass, frame and draw uniform buffers, depth texture, pipeline cache, resource counters, and WebGPU command recording for the current visual contract.

Public interfaces:

- `cpp/include/ofg/resources/resource_arena.hpp`, `texture.hpp`, `shader.hpp`, `material.hpp`, `mesh.hpp`, and `property_bag.hpp` expose the first high-level resource model.
- `cpp/include/ofg/render/demo_scene.hpp` builds the generated demo resources and updates the per-frame plane-and-cubes draw list.
- `cpp/include/ofg/render/draw_list.hpp`, `camera.hpp`, `renderer.hpp`, `opaque_pass.hpp`, and `pipeline_cache.hpp` expose the draw-list renderer surface.
- `cpp/include/ofg/render/bootstrap_scene.hpp` remains as legacy triangle layout regression data plus the shared clear-color helper.
- `cpp/include/ofg/render/webgpu_common.hpp` provides small WebGPU string/enum helpers shared by browser and native paths.

Communication contracts:

- Shared `Game` must use equivalent resource data, shader source, draw-list submission, renderer passes, and clear color regardless of whether the frame target comes from the browser surface or native offscreen texture.
- Resource objects are high-level assets, not wrappers for every WebGPU type. They may store the borrowed `GpuContext` that prepared them, but they do not own or release the platform device or queue.
- The renderer requests no optional GPU features and creates durable resources during initialization, explicit mutation, or resize, not every ordinary frame.
- Surface/texture format may differ between browser and native targets, but the visual contract remains a dark blue-gray background, a large textured checker ground plane, and multiple colored cubes rendered through the opaque draw-list path.

## BrowserSmoke

Browser smoke validates that the built site loads in a real browser-like environment controlled by Playwright core. It proves the TypeScript host can load generated C++/WASM, initialize WebGPU, resize, render frames, and read debug status.

Public interfaces:

- `npm run smoke:browser` runs the default browser smoke against the built app.
- `tools/browser-smoke.mjs` controls the browser through Playwright core and writes smoke artifacts under `artifacts/browser-smoke`.
- `npm run smoke:browser:cpp` runs the focused C++ fixture smoke under `tools/cpp-webgpu-smoke.html`.
- `tools/browser-smoke-cpp.mjs` writes focused artifacts under `artifacts/browser-smoke-cpp`.
- `tools/smoke-contract.json` provides the shared visual and resize expectations used by browser and native smoke.

Communication contracts:

- BrowserSmoke depends on `npm run build` output and the local static serving behavior.
- BrowserSmoke treats debug status JSON as the public runtime inspection interface.
- BrowserSmoke may verify page behavior and pixels, but must not depend on private generated WASM internals.

## NativeRenderSmoke

The native render smoke is a browser-free C++ render harness. It builds an installed Dawn native backend with Clang, creates a Vulkan WebGPU device, renders the shared `Game` into an offscreen texture, copies pixels through a padded readback buffer, writes a PNG, and records color-coverage diagnostics.

Public interfaces:

- `npm run smoke:render` runs the harness with the default output directory.
- `OFG_DAWN_SOURCE_DIR` points at the installed Dawn source checkout used by the native C++ smoke.
- `tools/smoke-render-cpp.mjs` verifies the Dawn checkout, configures/builds/runs the C++ Dawn executable, and passes in `tools/smoke-contract.json`.
- `cpp/src/native/render_smoke_main.cpp` provides the native C++ smoke executable entry point.
- Output artifacts are `opaque-demo.png` and `report.json` in the chosen output directory, normally `artifacts/render-smoke`.

Communication contracts:

- NativeRenderSmoke reads `tools/smoke-contract.json` through the Node wrapper and passes those values to the native executable.
- NativeRenderSmoke shares the C++ `Game` render path with browser smoke but uses an offscreen texture instead of a browser surface.
- The report JSON is the machine-readable contract for CI, human review, and coverage runs.

## CoverageGuardrails

Coverage guardrails own the quality gates around test visibility and C++/TypeScript ownership boundaries. They are pass/fail gates, not just report generators.

Public interfaces:

- `npm run coverage:cpp` runs C++ coverage through `tools/cpp-coverage.mjs`.
- `npm run coverage:ts` runs TypeScript coverage through `tools/ts-coverage.mjs`.
- `npm run coverage` runs both coverage gates.
- `COVERAGE.md` explains how to run and interpret coverage.
- `docs/coverage/` stores the latest committed coverage summaries.
- `artifacts/coverage/` stores generated local coverage output and is not source-controlled.

Communication contracts:

- Checked implementation files must meet the documented line coverage threshold, currently 90%.
- Exceptions must be explicit. Current exceptions cover browser-only C++ WASM/WebGPU glue and frame-driver submission through browser smoke, device-bound `Game` command encoding through WASM/native smoke, native Dawn smoke behavior through `npm run smoke:render`, and browser entrypoint behavior through `npm run smoke:browser`.
- Global summary percentages can include exception files; use the wrapper pass/fail output to decide whether the gate passed.

## DeploymentPackaging

Deployment packaging owns the static Cloudflare Pages output. It rebuilds the app, copies only runtime files into `.deploy`, writes cross-origin isolation headers required by WebGPU, verifies required files, and reports the generated C++ WASM size.

Public interfaces:

- `npm run package:site` rebuilds and packages the deploy directory.
- `npm run package:site:from-build` packages an already-built app.
- `npm run build:cloudflare` is the Cloudflare Pages build command.
- `npm run deploy -- --project-name=ofg` packages and uploads the site through local Wrangler.
- `.deploy/` is the generated Pages output directory.
- `.deploy/_headers` defines the cross-origin isolation and cache policy for the static app.

Communication contracts:

- Cloudflare Pages should use build command `npm run build:cloudflare` and output directory `.deploy`.
- DeploymentPackaging must publish `assets/wasm/ofg_cpp/ofg_cpp.js` and `assets/wasm/ofg_cpp/ofg_cpp.wasm`.
- DeploymentPackaging must not publish source-only files, tests, or large build directories.
- Browser WebGPU requires cross-origin isolation headers, so `_headers` is part of the deployment contract.
