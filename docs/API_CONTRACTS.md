# API Contracts

This file records active contracts between OFG systems. Historical migration details belong in archived plans or the active migration ExecPlan, not in current ownership claims.

## OFG-BOOT-001 TypeScript Host Ownership

TypeScript may own DOM boot, canvas lookup/creation, canvas resize policy, fatal-error display, local dev ergonomics, WASM module loading, and Playwright smoke helpers. TypeScript must not own gameplay simulation, scene graph state, GPU pipeline creation, render draw submission, or game-world data structures.

## OFG-BOOT-002 C++ Runtime Ownership

C++ owns frame state, debug status, bootstrap scene data, renderer setup, WebGPU resource creation, draw submission, browser WebGPU runtime behavior, and native Dawn offscreen rendering. The TypeScript host may call the narrow runtime facade, but it must not own renderer internals or GPU handles.

## OFG-BOOT-003 WASM Facade

The browser facade is narrow. TypeScript can create the runtime, resize it, request a frame, read debug status, and dispose it. The facade should not expose raw renderer internals, GPU handles, or mutable scene ownership to TypeScript.

## OFG-BOOT-004 Renderer Compatibility

Browser and native smoke must validate equivalent bootstrap renderer behavior: the same dark blue-gray clear color, the same red/green/blue triangle categories, durable pipeline/buffer creation outside ordinary frames, and reported adapter/backend/format diagnostics. Browser smoke uses Emdawnwebgpu and the browser's WebGPU implementation; native smoke uses pinned Dawn through the same `webgpu.h` style renderer API. Their visual contract and smoke thresholds stay aligned through `tools/smoke-contract.json`.

## OFG-BOOT-005 WebGPU Baseline

The renderer must request no optional GPU features, must not manually request limits above the adapter defaults, must use one render pipeline for the bootstrap scene, and must record adapter/backend/format data in smoke reports. The first visual contract is a dark blue-gray clear color with a red/green/blue triangle. Surface or texture formats must be reported; native smoke uses `Rgba8Unorm` so PNG readback preserves byte-identical clear-color classification with browser smoke.

## OFG-BOOT-006 Resource Lifetime

Pipeline, shader module, vertex buffer, and bind-group-like resources must be created during initialization or explicit resize/mutation, not every frame. Resize reconfigures the surface only when physical width, physical height, or clamped device-pixel-ratio changes. Zero-size canvas axes must be preserved by the browser host so the C++ runtime can skip surface configuration and report a recoverable debug status instead of failing.

## OFG-BOOT-007 Generated Artifacts

`dist/`, `dist-test/`, `.deploy/`, `artifacts/`, and `assets/wasm/ofg_cpp/` are generated and ignored. `package-lock.json`, C++ source, TypeScript source, and toolchain version files are source-controlled.

## OFG-BOOT-008 Deployment

The default deployment target is Cloudflare Pages with build output directory `.deploy`. Packaged runtime files include the TypeScript app output and `assets/wasm/ofg_cpp/ofg_cpp.js` / `assets/wasm/ofg_cpp/ofg_cpp.wasm`. Workers static-assets deployment is documentation-only until a later plan adds `wrangler.jsonc`, a pinned `wrangler` dependency, and Workers validation.

## OFG-BOOT-009 Coverage

Implementation files should meet 90% line coverage unless an exception is recorded in the active ExecPlan. C++ native-checkable code is covered by `npm run coverage:cpp`; TypeScript is covered by `npm run coverage:ts`. Browser-only C++ WebGPU glue and draw submission are covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke. Native C++ Dawn rendering is covered by `npm run smoke:render`.
