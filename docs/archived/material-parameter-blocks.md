# Share scene and material parameter blocks

This ExecPlan follows [PLANS.md](../../PLANS.md). Implementation is complete.

## Purpose and contract
Replace per-pass material rebuilding with persistent material shader objects and one immutable scene parameter block per PBR pass. Reflect instance bindings only when preparing a material. Preserve custom diagnostic shaders, material edits, queued draw snapshots, device restart, and WebGPU limits. CPU resources own GPU caches; Graphics holds weak references and clears surviving caches on shutdown.

## Progress
- [x] (2026-10-07) Inspected pinned RHI shader-object, snapshot and parameter-block APIs and existing renderer.
- [x] (2026-10-07) Implemented common imported scene type, finalized material blocks, cached instance cursors and setter invalidation. Focused existing GPU tests passed 265 assertions; new variant/edit/queued snapshot regression passed 42 assertions before full-suite validation.
- [x] (2026-10-07) All 9 native Release CTests passed (68.24 s); focused WGSL compile/browser validation passed. Native presentation resize/minimize/restore/close passed, with empty stderr and captures in artifacts/terrain/parameter-blocks/native.
- [x] (2026-10-07) Web build and PBR/terrain/texture-checkerboard browser smoke passed on Chrome 154.0.8037.98, Intel gen-12lp. No unexpected browser errors; screenshots inspected. Native capture inspected too.
- [x] (2026-10-07) Isolated 60-second Release profile passed all 9802 assertions; unchanged material binding/preparation functions have zero calls in the final 15 seconds.
- [x] (2026-10-07) Updated architecture/resource/PBR contracts and development commands; formatter 22.1.3 dry-run and git diff --check passed. Final measurements/results recorded below.

## Discoveries and decisions
The preceding per-stage root map removed repeated values within a pass but still reflects materials every frame. RHI snapshots bindings when encoding draws. Imported scene types provide common type identity across PBR feature variants. Material mutations invalidate their cache; replacement preserves previously encoded draws. Root draw state remains mutable and retains its finalized material child.

## Work and validation
Use build-native and build-web skills. Run native-release build and all CTests; focused GPU regressions must cover cached material mutations, interleaved instances, shader variants and scene changes without a host wait. Run tools/pbr-shader-check.mjs, web build, PBR and terrain smoke, native presentation capture. Profile the existing 60-second stationary fixture with function timers, validation off, 1440x1200 D3D12; compare its final 15 seconds with artifacts/terrain/release-profile/binding-fix. Do not overlap profiling with GPU tests/builds. No new coverage threshold: behavioral GPU tests and native core suite are the applicable checks.

## Recovery and dependencies
Preserve existing uncommitted profiling/binding fixes. Reuse configured native-release and web build trees and pinned Slang RHI submodule. Material edits and shutdown invalidate caches; failures publish no partial cache. No dependency upgrades. Slang RHI remains `16324a68af477baaede620e713644f5e9613b1a2`, using Slang 2026.17.1.

The pinned D3D12 binding builder has an explicit TODO for descriptor-data caching. Parameter blocks remove application reflection but do not avoid its per-draw descriptor allocation. Cached roots detach pass globals after encoding to avoid pinning old environment/shadow resources.

Full-suite discovery: RHI validation rejects null subobjects, even though the raw backend permits them. Detachment restores the original resource-free child instead. The independent aerial-sampling regression also needed the new production scene declaration/binding path; its numerical assertions remain unchanged.

## Outcomes and review
Implementation review completed: no new rendering interface or registry; Material owns its cache and Graphics observes weakly. Cached cursors retain no resources independently and are cleared with the root on shutdown. Setter invalidation replaces immutable material children, clones start uncached, and pass references are released after encoding. Existing diagnostic shader contracts and reflected matrix packing remain tested. Resource counts include both parameter blocks. Native tests and all applicable browser smoke checks pass. All planned validation is complete.

## Release results and evidence

Hardware: Intel Core i7-12700H, NVIDIA RTX 3050 Ti Laptop GPU, driver 596.08; MSVC 19.51.36248.0 Release `/O2 /Ob2 /DNDEBUG`, native D3D12, validation off, opt-in function timers active. Same 1440x1200 offscreen terrain workload, per-frame queue drain, final 15 seconds of a 60-second run, without concurrent builds/GPU tests. These are host elapsed scope times, not GPU timestamps or sampled CPU attribution. The measured frame sum excludes CSV/assertion diagnostics outside its phases.

| Measurement | Prior per-stage cache | Parameter blocks |
| --- | ---: | ---: |
| Graphics::render, mean host time | 9.1322 ms | 7.5263 ms |
| Graphics::render, exclusive instrumented time | 2.0031 ms | 0.7320 ms |
| bindMaterial calls/frame after warm-up | 1 | 0 |
| bindMaterial + bindFrame + bindOutdoor | 0.1116 ms | 0.0634 ms |
| ShadowRenderer::render, unchanged | 6.2510 ms | 6.0722 ms |
| Remaining queue wait | 2.8003 ms | 3.4952 ms |
| Median measured frame | 11.5180 ms | 10.8438 ms |
| Mean measured frame | 12.5350 ms | 11.6316 ms |
| p95 measured frame | 16.4427 ms | 14.7613 ms |

The final selected cut, draw count and triangle count are unchanged: 1529 nodes, 219 draws, 295066 triangles, depth 6, zero jobs/unresolved demand. The increase in residual queue wait does not establish slower GPU work: the CPU reaches the wait earlier. This single comparison establishes removal of repeated material preparation and a lower measured host cost, not a universal frame-rate guarantee. Shadow rendering remains the next measured bottleneck; its separate shaders/binding path are unchanged.

Raw evidence: `artifacts/terrain/release-profile/parameter-blocks/{stationary.csv,functions.csv,run.log,metadata.json}` and the comparison `summary.json` in its parent. Native/browser screenshots and reports are preserved under `artifacts/terrain/parameter-blocks`; full native CTest log is `artifacts/terrain/parameter-blocks-ctest.log`. Presentation resize/minimize/restore/close and all browser smoke checks passed; reviewed native terrain, browser terrain and maximum-layout PBR captures. Web compilation retains the existing Asyncify/exception warning; the browser allows the known Windows power-preference warning and expected negative-test asset/capability failures only.

The implementation is intentionally concrete: two PBR blocks, a private material cache, cached matrix bindings, and one imported scene type. No generic binding framework or new backend wrapper was added. Existing custom diagnostic shaders retain their public layout. The original resource-free scene child is restored after encoding because RHI validation disallows a null child, preventing idle material caches from pinning old pass resources.
