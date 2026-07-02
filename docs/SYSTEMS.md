# Systems

This is a living document for independent OFG systems. When a new system is created, add a section that names what it owns, its public interfaces, and the contracts it relies on when communicating with other systems.

The active cross-system contracts are recorded in `docs/API_CONTRACTS.md`. This file is the practical map of those contracts to concrete modules, commands, and artifacts.

## BrowserHost

The browser host owns the web page shell. It creates or finds the canvas, sizes it from the viewport and device pixel ratio, collects raw control input from DOM events, reports fatal startup errors, loads the C++/WASM module, and provides the local static dev server. It does not own gameplay simulation, player movement, camera behavior, scene graph data, GPU pipeline creation, or draw submission.

Public interfaces:

- `src/app/canvasHost.ts` exposes the canvas host factory and host operations used by the TypeScript entrypoint.
- `src/app/controlInput.ts` collects keyboard, pointer-lock, mouse-delta state, and one-frame camera-mode cycle edges into raw per-frame control input snapshots.
- `src/app/main.ts` boots the browser page, loads the WASM runtime, wires resize/frame callbacks, and reports fatal errors.
- `src/app/wasmRuntime.ts` adapts the generated Emscripten/Embind module to the stable TypeScript runtime interface.
- `tools/dev-server.mjs` serves the built static app for human review and browser smoke.
- `index.html` and `src/app/styles.css` provide the minimal browser shell.

Communication contracts:

- BrowserHost supplies physical canvas width, physical canvas height, and clamped device pixel ratio to the C++ `BrowserGame` facade.
- BrowserHost supplies raw control input snapshots to `BrowserGame` once per animation frame before requesting the C++ frame update. The raw snapshots contain movement axes, look deltas, look-active state, speed modifiers, and a one-frame camera-mode cycle edge only.
- BrowserHost must preserve zero-size canvas axes. Zero size is a recoverable state that C++ reports through debug status instead of a host-side failure.
- BrowserHost may inspect runtime debug JSON and display fatal startup failures, but must not reach into scene objects, camera components, renderer internals, or generated WASM glue.

## CppRuntime

The C++ runtime is the C++/WASM facade exposed to TypeScript. `BrowserGame` owns the browser-specific frame driver: WebGPU instance/adapter/device/surface setup, surface resize policy, setup-phase control-input buffering, surface texture acquisition, command encoder creation, command-buffer finish, queue submit, and Embind lifecycle ownership. `Game` exposes a static public lifecycle backed by one private singleton instance: single-shot `create`, repeated `prepare`, `resize`, `update`, `render`, repeated `release`, and single-shot `destroy`. `Resources` and `Renderer` expose their own static lifecycles. `Resources` owns active high-level resource storage for the borrowed WebGPU device; `Renderer` owns pass creation, pass lifetime, resize propagation, command recording, transient draw-list construction, and renderer counters. `Game` owns frame counting, debug-status serialization, the current `Scene` pointer, the latest raw `ControlInput` snapshot, demo-scene binding state, target validation, and the active scene passed to `Renderer::render`.

Public interfaces:

- `cpp/CMakeLists.txt` defines the C++ core library, doctest executable, CTest registration, browser Emscripten module, and native Dawn smoke target.
- `cpp/include/ofg/game/game.hpp` exposes the static `Game` lifecycle facade used by browser and native frame drivers.
- `cpp/include/ofg/core/control_input.hpp` exposes the native-checkable raw control snapshot consumed by C++ components.
- `cpp/include/ofg/scene/player.hpp` exposes the scene-owned player component.
- `cpp/include/ofg/resources/resources.hpp` exposes the static `Resources` lifecycle facade and `Resources::create_*` allocation APIs for high-level resource objects.
- `cpp/include/ofg/render/renderer.hpp` exposes the static `Renderer` lifecycle facade and scene render entry point.
- `cpp/include/ofg/scene/scene.hpp` exposes the first ECS scene graph: a root entity tree, local transforms, scene-owned component containers, indexed `MeshRenderer` iteration, and scene-owned `Camera` selection.
- `cpp/include/ofg/web/browser_game.hpp` exposes the Embind-facing `BrowserGame` facade.
- `tools/build-cpp-wasm.mjs` builds `assets/wasm/ofg_cpp/ofg_cpp.js` and `assets/wasm/ofg_cpp/ofg_cpp.wasm` through CMake and Emscripten.
- `tools/test-cpp.mjs` runs the C++ doctest executable through CMake/CTest.
- `src/app/wasmRuntime.ts` loads `/assets/wasm/ofg_cpp/ofg_cpp.js`, resolves `ofg_cpp.wasm`, and maps Embind `delete()` into the app runtime interface.
- Generated files under `assets/wasm/ofg_cpp/` are runtime artifacts produced by `npm run build:wasm`.
- CMake wrapper scripts reuse `artifacts/build/*` directories by default so installed Dawn and Emscripten object files stay incremental. Pass `-- --fresh` to the npm command only when a clean configure/build is intentionally required.

Communication contracts:

- CppRuntime consumes only the canvas and resize/frame calls supplied by BrowserHost.
- `BrowserGame` accepts sanitized raw control input from TypeScript. If input arrives before async WebGPU setup has created `Game`, `BrowserGame` stores the latest snapshot and forwards it once `Game` becomes active.
- `BrowserGame` creates the static `Game` singleton after browser WebGPU device setup, drives `Game::prepare()` from frame processing until ready, delegates frame state, component updates, and render command recording to `Game`, and keeps browser surface acquisition and one queue submit per frame in the browser frame driver.
- `Game::prepare()` prepares `Resources`, builds the current demo scene, then prepares `Renderer`. `Game::release()` drains `Renderer`, clears scene state, then drains `Resources`; `Game::destroy()` then destroys `Renderer` before `Resources`.
- The `debug_status_json()` fields are a public inspection contract for TypeScript tests, Playwright smoke, and later diagnostics.
- `dispose()` drains `Game::release()`, calls `Game::destroy()`, then releases borrowed WebGPU device and queue handles and browser WebGPU resources.

## CppRenderer

The C++ renderer owns the current pass-based draw-list renderer used behind static `Game` in browser and native frame drivers. `Renderer` is a static lifecycle facade that creates and owns its internal pass list; the current list contains one opaque pass. `Resources` owns active high-level resource storage and the borrowed `GpuContext`; `Game` uses that facade to build demo resources, then maintains a current ECS `Scene` containing scene-owned `Camera`, `Player`, and `MeshRenderer` components, floor entity, player entity, cube entities, and local transforms. `Scene::update` updates player components before camera components so same-frame camera follow observes player movement. `Camera` owns debug, first-person, and third-person mode behavior for the single active camera entity. `CameraProperties` is the renderer-facing camera snapshot that carries resolved camera matrices, source camera, aspect, and clip distances. `Renderer::render` resolves `Scene::main_camera()` into `CameraProperties`, iterates scene mesh renderers, and converts them into a private transient `DrawList` before executing its passes. `Resources::create_*` only allocates labeled resources; the explicit texture, shader, material, and mesh `init_*` methods validate data and create GPU state. The demo scene creates a camera entity, mipmapped checker texture, a white texture, opaque materials, a ground mesh, a cube mesh, a visible player box, and stable floor/player/cube entities whose transforms are updated per frame. The renderer owns the opaque pass, transient draw list, frame and draw uniform buffers, depth texture, pipeline cache, resource counters, and WebGPU command recording for the current visual contract.

Public interfaces:

- `cpp/include/ofg/resources/resources.hpp`, `texture.hpp`, `shader.hpp`, `material.hpp`, `mesh.hpp`, and `property_bag.hpp` expose the first high-level resource model.
- `cpp/include/ofg/render/demo_scene.hpp` builds the generated demo resources and updates the per-frame plane-and-cubes scene objects.
- `cpp/include/ofg/scene/scene.hpp` exposes the explicit scene view passed from `Game` to `Renderer`.
- `cpp/include/ofg/render/draw_list.hpp`, `camera_properties.hpp`, `renderer.hpp`, `opaque_pass.hpp`, and `pipeline_cache.hpp` expose the current renderer and pass internals.
- `cpp/include/ofg/render/bootstrap_scene.hpp` remains as legacy triangle layout regression data plus the shared clear-color helper.
- `cpp/include/ofg/gpu/common.hpp` provides small WebGPU string/enum helpers and reusable depth target helpers shared by browser, native, resource, and renderer paths.

Communication contracts:

- Static `Game` must use equivalent resource data, shader source, ECS mesh-renderer submission, renderer passes, and clear color regardless of whether the frame target comes from the browser surface or native offscreen texture.
- Resource objects are high-level assets, not wrappers for every WebGPU type. They may store the borrowed `GpuContext` that prepared them, but they do not own or release the platform device or queue.
- The renderer requests no optional GPU features and creates durable resources during lifecycle creation, first render for a new material/shader pipeline key, explicit mutation, or resize, not every ordinary steady-state frame.
- Surface/texture format may differ between browser and native targets, but the visual contract remains a dark blue-gray background, a large textured checker ground plane, a visible player box, and multiple colored cubes rendered through the opaque draw-list path.

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

The native render smoke is a browser-free C++ render harness. It builds an installed Dawn native backend with Clang, creates a Vulkan WebGPU device, renders through static `Game` into an offscreen texture, copies pixels through a padded readback buffer, writes a PNG, and records color-coverage diagnostics.

Public interfaces:

- `npm run smoke:render` runs the harness with the default output directory.
- `OFG_DAWN_SOURCE_DIR` points at the installed Dawn source checkout used by the native C++ smoke.
- `tools/smoke-render-cpp.mjs` verifies the Dawn checkout, configures/builds/runs the C++ Dawn executable, and passes in `tools/smoke-contract.json`.
- `cpp/src/native/render_smoke_main.cpp` provides the native C++ smoke executable entry point.
- Output artifacts are `opaque-demo.png` and `report.json` in the chosen output directory, normally `artifacts/render-smoke`.
- The native smoke build reuses `artifacts/build/cpp-render-smoke`; `npm run smoke:render -- --fresh` is the explicit slow clean path.

Communication contracts:

- NativeRenderSmoke reads `tools/smoke-contract.json` through the Node wrapper and passes those values to the native executable.
- NativeRenderSmoke shares the C++ `Game` render path with browser smoke but uses an offscreen texture instead of a browser surface. It releases and destroys the static `Game` singleton before borrowed Dawn device and queue handles are released.
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
- Coverage runs clear generated profile/report output, but reuse the coverage CMake build tree unless `-- --fresh` is requested.

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
