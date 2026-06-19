# API Contracts

This file records active contracts between OFG systems. The bootstrap contracts were established by the completed plan archived at `docs/archived/initial-bootstrap-plan.md`.

## OFG-BOOT-001 TypeScript Host Ownership

TypeScript may own DOM boot, canvas lookup/creation, canvas resize policy, fatal-error display, local dev ergonomics, WASM module loading, and Playwright smoke helpers. TypeScript must not own gameplay simulation, scene graph state, GPU pipeline creation, render draw submission, or game-world data structures.

## OFG-BOOT-002 Rust Runtime Ownership

Rust owns frame state, debug status, scene data for the bootstrap triangle, renderer setup, WebGPU resource creation, draw submission, and native offscreen rendering.

## OFG-BOOT-003 WASM Facade

The browser facade is narrow. TypeScript can create the runtime, resize it, request a frame, read debug status, and dispose it. The facade should not expose raw renderer internals, GPU handles, or mutable scene ownership to TypeScript.

## OFG-BOOT-004 Renderer Compatibility

Milestones 2 and 3 must make browser and native smoke use the same WGSL source at `crates/ofg_render/src/shaders/bootstrap.wgsl`, the same bootstrap scene data from `crates/ofg_render/src/bootstrap_scene.rs`, and the same renderer module from `crates/ofg_render/src/renderer.rs`. Allowed differences are only the final output target and reported adapter/surface format.

## OFG-BOOT-005 WebGPU Baseline

The renderer must request no optional GPU features, must not manually request limits above the adapter defaults, must use one render pipeline for the bootstrap scene, and must record adapter/backend/format data in smoke reports. The first visual contract is a dark blue-gray clear color with a red/green/blue triangle. Surface or texture formats must be reported; native smoke uses `Rgba8Unorm` so PNG readback preserves byte-identical clear-color classification with browser smoke.

## OFG-BOOT-006 Resource Lifetime

Pipeline, shader module, vertex buffer, and bind-group-like resources must be created during initialization or explicit resize, not every frame. Resize reconfigures the surface only when physical width, physical height, or clamped device-pixel-ratio changes. Zero-size canvas axes must be preserved by the browser host so the Rust facade can skip surface configuration and report a recoverable debug status instead of panicking.

## OFG-BOOT-007 Generated Artifacts

`dist/`, `dist-test/`, `target/`, `.deploy/`, `artifacts/`, and `assets/wasm/ofg_web/` are generated and ignored. `package-lock.json` and `Cargo.lock` are source-controlled.

## OFG-BOOT-008 Deployment

The default deployment target is Cloudflare Pages with build output directory `.deploy`. Workers static-assets deployment is documentation-only until a later plan adds `wrangler.jsonc`, a pinned `wrangler` dependency, and Workers validation.

## OFG-BOOT-009 Coverage

Implementation files should meet 90% line coverage unless an exception is recorded in the active ExecPlan. Browser-only WASM/WebGPU code is covered by WASM facade tests and browser smoke rather than native Rust coverage alone.
