# System

This is a living document to track what independent systems exist in OFG. When a new system is created, add it as a section with a short description of what the system manages and what state it keeps track of.

Each system should also document its core public types and interfaces, as a reference for how it is designed to talk to other systems within OFG. 

## BrowserHost

The browser host owns the web page shell: DOM startup, canvas creation or lookup, viewport sizing, device-pixel-ratio clamping, fatal-error display, and the local static dev server. It deliberately does not own game simulation or WebGPU draw submission.

## BootstrapRuntime

The bootstrap runtime is the Rust/WASM facade exposed to TypeScript. It owns frame counting, WebGPU adapter/device/surface setup in the browser, resize handling, frame submission, and debug-status serialization.

## BootstrapRenderer

The bootstrap renderer is shared Rust rendering code. It owns the initial WGSL shader, bootstrap triangle vertex data, clear color, render pipeline, vertex buffer, and draw submission used by the browser renderer and native PNG smoke.

## NativeRenderSmoke

The native render smoke is a browser-free test harness. It creates a native `wgpu` device, renders the shared bootstrap renderer into an offscreen texture, copies pixels through a padded readback buffer, writes a PNG, and records color-coverage diagnostics.

## CoverageGuardrails

Coverage guardrails own the first quality gates around test visibility and TypeScript/Rust ownership boundaries. They run Rust coverage with `cargo-llvm-cov`, TypeScript coverage with `c8`, and a source-level TypeScript boundary test that keeps WebGPU draw ownership and generated WASM internals out of the app shell.

## DeploymentPackaging

Deployment packaging owns the static Cloudflare Pages output. `package:site` rebuilds the app before packaging, the internal from-build packager copies only browser runtime files into `.deploy`, `_headers` applies cross-origin isolation, required deploy files are verified recursively, and the Cloudflare build wrapper reports the generated WASM size.
