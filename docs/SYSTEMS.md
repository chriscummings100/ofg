# Systems

This is a living document for independent OFG systems. When a new system is created, add a section that names what it owns, its public interfaces, and the contracts it relies on when communicating with other systems.

The active cross-system contracts are recorded in `docs/API_CONTRACTS.md`. This file is the practical map of those contracts to concrete modules, commands, and artifacts.

## BrowserHost

The browser host owns the web page shell. It creates or finds the canvas, sizes it from the viewport and device pixel ratio, reports fatal startup errors, and provides the local static dev server. It does not own gameplay simulation, scene graph data, GPU pipeline creation, or draw submission.

Public interfaces:

- `src/app/canvasHost.ts` exposes the canvas host factory and host operations used by the TypeScript entrypoint.
- `src/app/main.ts` boots the browser page, loads the WASM runtime, wires resize/frame callbacks, and reports fatal errors.
- `tools/dev-server.mjs` serves the built static app for human review and browser smoke.
- `index.html` and `src/app/styles.css` provide the minimal browser shell.

Communication contracts:

- BrowserHost supplies physical canvas width, physical canvas height, and clamped device pixel ratio to BootstrapRuntime.
- BrowserHost must preserve zero-size canvas axes. Zero size is a recoverable state that Rust reports through debug status instead of a host-side failure.
- BrowserHost may inspect runtime debug JSON and display fatal startup failures, but must not reach into renderer internals or generated WASM glue.

## BootstrapRuntime

The bootstrap runtime is the Rust/WASM facade exposed to TypeScript. It owns frame counting, WebGPU instance/adapter/device/surface setup in the browser, surface resize policy, frame submission, and debug-status serialization.

Public interfaces:

- `crates/ofg_web/src/browser.rs` exposes `BrowserGame.create(canvas)`, `resize(width, height, devicePixelRatio)`, `frame(timeMs)`, `debug_status_json()`, and `dispose()` through `wasm-bindgen`.
- `crates/ofg_web/src/status.rs` defines `RuntimeDebugStatus`, the JSON shape used by tests and smoke diagnostics.
- `src/app/wasmRuntime.ts` is the TypeScript wrapper that imports generated WASM glue and presents a narrow host-facing runtime API.
- Generated files under `assets/wasm/ofg_web/` are runtime artifacts produced by `npm run build:wasm`.

Communication contracts:

- BootstrapRuntime consumes only the canvas and resize/frame calls supplied by BrowserHost.
- BootstrapRuntime delegates all draw submission to BootstrapRenderer and should keep simulation/game ownership in Rust.
- The `debug_status_json()` fields are a public inspection contract for TypeScript tests, Playwright smoke, and later diagnostics.
- `dispose()` makes the runtime inert while preserving a useful disposed status for callers.

## BootstrapRenderer

The bootstrap renderer is shared Rust rendering code. It owns the initial WGSL shader, triangle vertex data, clear color, render pipeline, vertex buffer, resource counters, and draw submission used by both browser and native render paths.

Public interfaces:

- `crates/ofg_render/src/renderer.rs` exposes `BootstrapRenderer::new(device, format)`, `render_to_view(encoder, view)`, and `counters()`.
- `crates/ofg_render/src/bootstrap_scene.rs` owns bootstrap scene data and the public clear-color helper.
- `crates/ofg_render/src/shaders/bootstrap.wgsl` is the shared shader source.

Communication contracts:

- BootstrapRuntime and NativeRenderSmoke must use the same renderer, shader, scene data, and clear color.
- The renderer requests no optional GPU features and creates durable resources during initialization, not every frame.
- Surface/texture format may differ between browser and native targets, but the visual contract remains a dark blue-gray background plus red/green/blue triangle.

## BrowserSmoke

Browser smoke validates that the built site loads in a real browser-like environment controlled by Playwright core. It proves the TypeScript host can load generated WASM, initialize WebGPU, resize, render frames, and read debug status.

Public interfaces:

- `npm run smoke:browser` runs the browser smoke.
- `tools/browser-smoke.mjs` controls the browser through Playwright core and writes smoke artifacts under `artifacts/browser-smoke`.
- `tools/smoke-contract.json` provides the shared visual and resize expectations used by browser and native smoke.

Communication contracts:

- BrowserSmoke depends on `npm run build` output and the local static serving behavior.
- BrowserSmoke treats debug status JSON as the public runtime inspection interface.
- BrowserSmoke may verify page behavior and pixels, but must not depend on private generated WASM internals.

## NativeRenderSmoke

The native render smoke is a browser-free render harness. It creates a native `wgpu` device, renders BootstrapRenderer into an offscreen texture, copies pixels through a padded readback buffer, writes a PNG, and records color-coverage diagnostics.

Public interfaces:

- `npm run smoke:render` runs the harness with the default output directory.
- `crates/ofg_test_harness/src/bin/ofg-render-frame.rs` provides the `ofg-render-frame [--out <dir>]` binary.
- Output artifacts are `bootstrap.png` and `report.json` in the chosen output directory, normally `artifacts/render-smoke`.

Communication contracts:

- NativeRenderSmoke reads `tools/smoke-contract.json` and fails if the contract disagrees with `ofg_render::clear_color_rgba8()`.
- NativeRenderSmoke shares renderer code with BootstrapRuntime but uses an offscreen texture instead of a browser surface.
- The report JSON is the machine-readable contract for CI, human review, and coverage runs.

## CoverageGuardrails

Coverage guardrails own the first quality gates around test visibility and TypeScript/Rust ownership boundaries. They are pass/fail gates, not just report generators.

Public interfaces:

- `npm run coverage:rust` runs Rust coverage through `tools/rust-coverage.mjs`.
- `npm run coverage:ts` runs TypeScript coverage through `tools/ts-coverage.mjs`.
- `npm run coverage` runs both coverage gates.
- `COVERAGE.md` explains how to run and interpret coverage.
- `docs/coverage/` stores the latest committed coverage summaries.
- `artifacts/coverage/` stores generated local coverage output and is not source-controlled.

Communication contracts:

- Checked implementation files must meet the documented line coverage threshold, currently 90%.
- Exceptions must be explicit. Current exceptions cover browser-only WASM/WebGPU code through `test:wasm` and `smoke:browser`, and native-smoke failure handling through the instrumented smoke path.
- Global summary percentages can include exception files; use the wrapper pass/fail output to decide whether the gate passed.

## DeploymentPackaging

Deployment packaging owns the static Cloudflare Pages output. It rebuilds the app, copies only runtime files into `.deploy`, writes cross-origin isolation headers required by WebGPU, verifies required files, and reports the generated WASM size.

Public interfaces:

- `npm run package:site` rebuilds and packages the deploy directory.
- `npm run package:site:from-build` packages an already-built app.
- `npm run build:cloudflare` is the Cloudflare Pages build command.
- `.deploy/` is the generated Pages output directory.
- `.deploy/_headers` defines the cross-origin isolation and cache policy for the static app.

Communication contracts:

- Cloudflare Pages should use build command `npm run build:cloudflare` and output directory `.deploy`.
- DeploymentPackaging must not publish source-only files, tests, or large build directories.
- Browser WebGPU requires cross-origin isolation headers, so `_headers` is part of the deployment contract.
