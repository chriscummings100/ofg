# API Contracts

This file records active contracts between OFG systems. Historical migration details belong in archived plans or the active migration ExecPlan, not in current ownership claims.

## OFG-BOOT-001 TypeScript Host Ownership

TypeScript may own DOM boot, canvas lookup/creation, canvas resize policy, raw control-input event collection, fatal-error display, local dev ergonomics, WASM module loading, and Playwright smoke helpers. TypeScript must not own gameplay simulation, player movement, camera mode behavior, scene graph state, GPU pipeline creation, render draw submission, or game-world data structures.

## OFG-BOOT-002 C++ Runtime Ownership

C++ owns frame state, debug status, the current scene graph, stored raw control input, player movement behavior, camera mode behavior, demo-scene binding data, high-level renderer resources, draw-list construction, renderer/pass setup, WebGPU resource creation, browser WebGPU runtime behavior, and native Dawn offscreen rendering. `Game`, `Resources`, and `Renderer` are static lifecycle facades backed by one private singleton instance each for the active WebGPU device lifetime. Their public lifecycle is single-shot `create`, repeated `prepare` until ready, steady-state calls, repeated `release` until done, and single-shot `destroy`. `Resources` owns active resource storage and the borrowed `GpuContext`; `Renderer` owns its pass list internally; `Game` owns the current `Scene` pointer, latest raw `ControlInput` snapshot, and demo binding state, then passes the active scene into `Renderer::render`. The current `Scene` stores a root entity tree, entity local transforms, scene-owned `MeshRenderer`, `Player`, and `Camera` component containers, optional main-camera selection with first-camera fallback, and an explicit component update pass that updates players before cameras. `Camera` owns control mode state for debug, first-person, and third-person behavior. `Renderer` resolves the selected scene camera into `CameraProperties`, iterates mesh renderers to build a private transient `DrawList`, and passes the camera snapshot plus draw list to its pass queue. Browser/native C++ frame drivers own platform target acquisition, command-buffer finish, and queue submit. The TypeScript host may call the narrow runtime facade, but it must not own scene graph state, player movement, camera behavior, renderer internals, resource objects, draw commands, or GPU handles.

## OFG-BOOT-003 WASM Facade

The browser facade is narrow. TypeScript can create the runtime, resize it, pass one raw `ControlInput` snapshot per animation frame, request a frame, read debug status, and dispose it. Browser disposal drains `Game::release()` synchronously, calls `Game::destroy()`, then releases browser WebGPU handles and the Embind wrapper. The facade should not expose raw renderer internals, GPU handles, camera pointers, player pointers, or mutable scene ownership to TypeScript.

## OFG-BOOT-004 Renderer Compatibility

Browser and native smoke must validate equivalent draw-list renderer behavior: the same dark blue-gray clear color, the same textured checker ground plane, the same saturated cube-color categories, the same visible player box, the same C++ resource layer and opaque-pass shader path, durable renderer resource creation outside ordinary frames, and reported adapter/backend/format diagnostics. Browser smoke uses Emdawnwebgpu and the browser's WebGPU implementation; native smoke uses an installed Dawn checkout through the same `webgpu.h` style renderer API. Their visual contract and smoke thresholds stay aligned through `tools/smoke-contract.json`.

## OFG-BOOT-005 WebGPU Baseline

The renderer must request no optional GPU features, must not manually request limits above the adapter defaults, and must record adapter/backend/format data in smoke reports. The current draw-list visual uses an opaque textured material path, a perspective camera, depth buffering, one generated checker texture, one generated white texture, a ground plane, a visible player box, and four animated cubes. Debug counters must show durable renderer resources were created and must not assume exactly one pipeline once multiple material bind-group layouts or later variants exist. Surface or texture formats must be reported; native smoke uses `Rgba8Unorm` so PNG readback preserves byte-identical clear-color classification with browser smoke.

## OFG-BOOT-006 Resource Lifetime

Texture, shader, material, mesh, pass, and pipeline resources must be created during initialization, preparation, first use for a changed scene/material combination, explicit resize, or explicit mutation, not every ordinary steady-state frame. Per-frame demo animation may rebuild draw commands and model matrices, but it must reuse the existing C++ resource objects. `Resources::create_*` allocates and stores labeled high-level resources only; explicit resource `init_*` calls validate resource data and initialize GPU state. Device-bound resource objects may store the borrowed `GpuContext` that created them so later mutation methods can refresh WebGPU state without a global device. Resize reconfigures the surface only when physical width, physical height, or clamped device-pixel-ratio changes. Zero-size canvas axes must be preserved by the browser host so the C++ runtime can skip surface configuration and report a recoverable debug status instead of failing.

## OFG-BOOT-007 Generated Artifacts

`dist/`, `dist-test/`, `.deploy/`, `artifacts/`, and `assets/wasm/ofg_cpp/` are generated and ignored. `package-lock.json`, C++ source, TypeScript source, and toolchain version files are source-controlled.

## OFG-BOOT-008 Deployment

The default deployment target is Cloudflare Pages with build output directory `.deploy`. Packaged runtime files include the TypeScript app output and `assets/wasm/ofg_cpp/ofg_cpp.js` / `assets/wasm/ofg_cpp/ofg_cpp.wasm`. Local deployment uses `npm run deploy -- --project-name=ofg`, which packages the site and uploads through the Wrangler dependency pinned in `package-lock.json`. Workers static-assets deployment is documentation-only until a later plan adds `wrangler.jsonc` and Workers validation.

## OFG-BOOT-009 Coverage

Implementation files should meet 90% line coverage unless an exception is recorded in the active ExecPlan. C++ native-checkable code is covered by `npm run coverage:cpp`; TypeScript is covered by `npm run coverage:ts`. Browser-only C++ WebGPU glue and frame-driver submission are covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke. Native C++ Dawn rendering is covered by `npm run smoke:render`.
