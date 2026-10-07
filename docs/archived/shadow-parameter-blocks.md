# Apply parameter-block caching to shadow rendering

This ExecPlan follows [PLANS.md](../../PLANS.md). Implementation is complete.

## Purpose and contract
Complete the binding optimization across the shadow path. Each cascade owns one immutable pass block with clipFromWorld; each Material owns its cached immutable shadow properties and alpha resources; each draw only binds objects and writes worldFromLocal through a cached cursor. Preserve opaque/masked, transformed UV, vertex alpha, culling/mirrored geometry, skinning, queued snapshots and device restart.

## Progress
- [x] (2026-10-07) Inspected the measured 6.07 ms shadow path: it creates a root, reflects fields and rereads material properties for every caster in every cascade.
- [x] (2026-10-07) Implemented cached shadow materials, shared cascade blocks, instance-only writes and invalidation/lifetime. Existing outdoor and skinning tests passed 1059 assertions.
- [x] (2026-10-07) Shadow-specific regression passed 220 assertions: every cascade, opaque/masked variants, mirrored culling, clone isolation, texture replacement, UV edits, queued depth snapshots, and renderer replacement.
- [x] (2026-10-07) All nine native CTest groups pass: eight unaffected groups in the full run and the corrected scene suite on retry (41.60 s). Outdoor WebGPU smoke passed. Native presentation resize/minimize/restore/close passed with empty stderr.
- [x] (2026-10-07) Isolated Release profile passed 14440 assertions, with zero steady shadow-material preparation calls. Recorded results, reviewed ownership/formatting/docs and archived this plan.

## Decisions and discoveries
Use a small shared imported ShadowPassParameters type across the existing opaque/textured shader variants. Keep separate material GPU cache ownership so shadow-only casters need no visible PBR preparation. Material setters invalidate both caches. ShadowRenderer tracks caches weakly and clears surviving handles on destruction. No interface, registry or dependency change is needed. RHI snapshots draw bindings; backend descriptor assembly remains per draw.

## Validation and execution
Use build-native and build-web skills, current native-release and web trees. Run cmake --build --preset native-release and ctest --preset native-release --output-on-failure; build web and run node tools/outdoor-smoke.mjs to exercise real cascades and masked materials. Inspect saved captures. Native tests must compare actual depth coverage across all four cascades, interleaved materials/instances, material edits and independent queued outputs, as well as skinning and lifecycle. Existing no-sleep deterministic tests remain the coverage policy; no artificial coverage percentage. Profile the 60-second stationary terrain fixture with function timers active, validation off, 1440x1200 D3D12 and no concurrent builds/GPU tests; compare final 15 seconds to release-profile/parameter-blocks.

## Recovery and dependencies
Preserve all existing uncommitted work. Slang RHI remains 16324a68af477baaede620e713644f5e9613b1a2, Slang 2026.17.1. Failed cache preparation does not publish partial data. Material edits replace old objects; submitted commands retain resources. Native queue drains before Graphics shutdown.

Regression fixture discovery: the standalone shadow input layout must use numeric PBR semantic indices 0..5, matching the production layout; the first test attempt used diagnostic semantic indices and failed pipeline creation. Corrected the fixture without changing the production ABI. RHI also requires Extent3D::kWholeTexture for a whole-array depth copy; corrected the fixture copy extent. All original coverage assertions now pass.

## Outcomes and review
Review complete: direct Material ownership, weak renderer tracking, replacement after edits, clone independence and shutdown invalidation follow the main pass contract. Both variants share the imported pass type. The shadow draw loop contains no named reflection/material property lookup. Existing culling and skinning rules remain. Native and browser checks and isolated profiling passed. Inspected native terrain and browser noon captures, including patterned mask shadows. The web link retains the existing Asyncify/exception warning; browser diagnostics have no errors. Formatting and whitespace checks passed.

## Release results

Same conditions as the preceding main-pass run: Intel Core i7-12700H, NVIDIA RTX 3050 Ti Laptop GPU, driver 596.08, MSVC 19.51.36248.0 Release `/O2 /Ob2 /DNDEBUG`, D3D12, 1440x1200 offscreen, validation off, function timers active. Measure the final 15 seconds of a 60-second stationary run, draining the queue each frame, without concurrent builds or GPU tests. These are host elapsed scopes and residual queue waits, not GPU timestamps or sampled CPU measurements. Graphics totals include the shadow child scope. Measured frame sums exclude diagnostics/CSV outside the timed phases.

| Measurement | Main-pass blocks only | Shadow blocks too |
| --- | ---: | ---: |
| ShadowRenderer::render mean host time | 6.0722 ms | 3.8392 ms |
| Graphics::render mean host time | 7.5263 ms | 5.2327 ms |
| Residual queue wait | 3.4952 ms | 2.0684 ms |
| Median measured frame | 10.8438 ms | 7.3440 ms |
| Mean measured frame | 11.6316 ms | 7.8456 ms |
| p95 measured frame | 14.7613 ms | 10.0221 ms |

Shadow material preparation has zero calls after warm-up. Final workload remains 1529 selected nodes, 219 visible draws, 295066 visible triangles, depth 6, CPU payload 108996096 bytes, zero jobs/unresolved demand and idle planner. The remaining 3.84 ms includes culling, draw encoding, backend descriptor construction and submission; this profile does not attribute it further. No backend descriptor caching was added.

Evidence: `artifacts/terrain/release-profile/shadow-parameter-blocks/{run.log,stationary.csv,functions.csv,metadata.json}` and the parent `summary.json`. Native and browser captures/reports are under `artifacts/terrain/shadow-parameter-blocks`. The initial full CTest log and corrected scene-suite retry are `artifacts/terrain/shadow-parameter-blocks-ctest.log` and `artifacts/terrain/shadow-parameter-blocks-ctest-retry.log`. The focused shadow test passed all 220 assertions. Native terrain and browser noon screenshots were inspected; browser outdoor controls cover lighting changes and masked geometry.

## Retrospective

The main-pass optimization was unnecessarily narrow: the same material/pass/instance ownership split applies directly to shadows. The measured shadow cost justified including this path. Explicit per-material caches keep shadow-only casters independent of visible-pass shader compilation, without introducing a general binding framework. Every material setter invalidates both caches, accepting occasional unnecessary rebuilds on unrelated property edits in exchange for simple, reliable invalidation.
