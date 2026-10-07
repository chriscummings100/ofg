# Remove repeated per-draw binding preparation

This ExecPlan follows [PLANS.md](../../PLANS.md). Authorized repair, completed 2026-10-07.

## Purpose and scope

Release profiling measured about 15 ms/frame rebuilding identical material/frame/outdoor bindings for 219 terrain
draws. Move invariant binding preparation out of the per-draw path without changing shaders, geometry, ordering or
resource lifetime. Preserve the existing uncommitted Release profiling work. Use build-native and build-web skills.

## Progress

- [x] (2026-10-07) Traced bindMaterial/bindFrame/bindOutdoor and pinned RHI command recording on D3D12 and WebGPU.
- [x] (2026-10-07) Reuse one root binding object per material per stage of Graphics::render; update only draw transforms.
- [x] (2026-10-07) Added pixel regression for interleaved materials, mirrored/distinct transforms and material edits
  between submissions without a host wait. All nine Release CTests passed (49.43 seconds).
- [x] (2026-10-07) Verified native, browser, visual output and isolated Release function timings; recorded results.

## Surprises & Discoveries

The expensive functions include shader reflection traversal, type checking and per-field packing, not merely driver
descriptor binding. ShaderObject::setData itself copies bytes and increments a version. RenderPassEncoder::drawIndexed
calls writeRenderState/getBindingData immediately. Both D3D12 and WebGPU builders create draw-owned binding data then;
later shader-object edits must not retroactively affect already recorded draws. This existing RHI contract permits
reuse without an application-owned persistent cache or material revision tracking.

## Decision Log

Use a local map keyed by Material identity inside each render stage. Initialize its root once with material/resources
and frame/outdoor data; keep distinct draw matrices for each recorded draw. Stage-local ownership automatically
refreshes all edits, camera and lighting changes on the next render call, and preserves per-stage output settings.
No shader ABI change, persistent invalidation system, vendored changes or terrain optimization is needed.
Existing per-draw transform reflection remains initially; follow the measured result before broadening the repair.

## Validation and concrete steps

Build native-release, run all nine CTests and a focused shared-root snapshot pixel regression. Profile the unchanged
60-second 1440x1200 workload serially without concurrent builds; preserve before/after CSVs under
artifacts/terrain/release-profile. Require unchanged cut/triangle/draw counts and successful convergence, not a
machine-dependent timing assertion. Build web and run terrain and PBR smoke, inspect captures and diagnostics.
Use native presentation smoke for a fresh capture. Document host elapsed timing and remaining GPU wait separately.

## Outcomes & Retrospective

Native presentation smoke passed resize, minimize/restore and close with empty stderr. Inspected
artifacts/terrain/binding-fix/native/window.png: full terrain, 1529 selected nodes, 16 m finest cells, no jobs/failures.
This capture overlapped the browser link and is visual evidence, not the isolated timing comparison.
Browser build passed (the existing Asyncify/exception warning remains). PBR and terrain browser smoke passed with no
errors. Terrain reached depth 6, no outstanding work and idle planning at origin and distant coordinates; UI toggles,
resize and reload passed. Inspected fresh PBR overview, distant terrain and reloaded terrain captures, preserved under
artifacts/terrain/binding-fix/{pbr,browser}. Browser captures verify rendering, not the native performance claim.
The isolated native profile ran after compilation and the native window had exited.

Baseline: Graphics::render 24.8571 ms/frame, bindMaterial 5.3787, bindOutdoor 5.2276, bindFrame 4.4282.
Timer scopes active, D3D12 validation off, i7-12700H / RTX3050TiLaptopGPU, Release /O2, last fifteen seconds of sixty.

The after run uses the same configuration, driver 596.08 and 1440x1200 offscreen viewport. All 9070 workload assertions
passed. It retains 1529 selected nodes, 219 visible draws and 295066 visible triangles, with zero jobs/unresolved demand
and depth 6 at completion. Convergence took 5.60774 seconds versus 8.67491 before. Function times below are average
host elapsed milliseconds per frame, not GPU shader execution; nested Graphics totals include binding and shadow work.

| Measurement | Before | After |
| --- | ---: | ---: |
| bindMaterial | 5.3787 ms | 0.0471 ms |
| bindOutdoor | 5.2276 ms | 0.0343 ms |
| bindFrame | 4.4282 ms | 0.0302 ms |
| Three binding functions combined | 15.0345 ms | 0.1116 ms |
| Calls per binding function per frame | 219 | 1 |
| Graphics::render | 24.8571 ms | 9.1322 ms |
| ShadowRenderer::render (unchanged code) | 6.0850 ms | 6.2510 ms |
| Remaining queue wait | 6.0410 ms | 2.8003 ms |
| Median measured frame | 29.4712 ms | 11.5180 ms |
| Mean measured frame | 31.5338 ms | 12.5350 ms |
| p95 measured frame | 39.7425 ms | 16.4427 ms |

The near-constant shadow cost and unchanged geometry support attribution of the host improvement to eliminating
repeated binding preparation. Queue wait is only residual latency after submissions; its reduction does not prove
a particular shader sped up. No remaining shadow optimization or persistent binding cache was added. Reflection still
runs once per material per stage, so workloads with many distinct materials may need separate measurement.
Evidence: artifacts/terrain/release-profile/binding-fix/{stationary.csv,functions.csv,run.log}, and the refreshed
summary.json comparing the preserved baseline/instrumented/validation runs. The new native pixel regression plus
existing material edits, typed-layout failures, textured/PBR rendering and resource-retirement tests all passed.

Final review checked snapshot ownership in both pinned backends, map lifetime and key identity, material/lighting edits,
stage-specific settings, mirrored draws, existing failure diagnostics, comments and stale documentation. No persistent
cache, shader/dependency change or new graphics abstraction was introduced. Formatting and whitespace checks passed.
