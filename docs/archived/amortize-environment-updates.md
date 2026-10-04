# Spread outdoor environment updates across frames

This ExecPlan follows [PLANS.md](../../PLANS.md). Implementation is complete and validated on native D3D12 and browser WebGPU.

## Purpose / Big Picture

Remove the six capture plus 126 filtering draws from a single outdoor frame. Keep the last complete environment
available while its replacement is built, including during continuously moving time, camera and weather inputs.

## Progress

- [x] (2026-10-04) Inspected render ownership, double-buffered cube layout, graphics bindings and existing tests.
- [x] (2026-10-04) Implemented frozen capture inputs, 24-frame jobs, complete publication and progress diagnostics.
- [x] (2026-10-04 13:04Z) Verified partial visibility, frozen snapshots, edits during work, paused reuse and both backends.
- [x] (2026-10-04 13:04Z) Updated documentation, inspected startup/complete/pending-transition captures and completed review.

## Surprises & Discoveries

The live sky-view lookup changes independently of cube capture. Copying CPU settings alone cannot freeze a job.
The two filtered generations also serve the temporal blend: the old slot cannot be overwritten until blending ends.
Existing screenshot tests assume one render produces a complete IBL and must wait for actual publication instead.
RHI validation rejected an empty copy extent even for whole-texture ranges; use the explicit `Extent3D::kWholeTexture`.
After correcting it, the focused native regression passes 1,027 assertions, including exact frozen-reference readback
of all 126 face/mip subresources. The browser compiler requires parentheses around JavaScript object literals inside
EM_ASM to protect commas from the C preprocessor; corrected before linking.
The first browser transition capture showed fully white geometry under night exposure while daylight IBL remained
active. This is a visible regression, not acceptable update latency. Each slot now retains its source sun height;
PBR scales the old/new samples independently by source/current exposure, using current controls so manual edits
remain effective. A shared pure CPU exposure function avoids duplicating the clock curve. Browser smoke now rejects
whiteout in the actual pending-transition screenshot, in addition to testing final images.

## Decision Log

- Freeze the sky-view texture once per job and copy only the lighting/camera inputs, never scene draw resources.
- Use one capture face per frame for six frames, then one filter/face mip chain (seven passes) per frame for eighteen.
  This makes filter batches comparable in texel/sample cost without a generic scheduler or new user policy.
- Finish jobs despite edits; coalesce newer values into the next job. Never restart every frame and starve publication.
- Compensate old/new generation exposure separately during time changes; preserve authored manual EV edits.
- Retain two filtered generations; wait for the 0.1-second blend before reusing the old slot. No queue waits/readback
  are introduced into production rendering. Startup uses zero indirect light until the first complete generation.

## Outcomes & Retrospective

Completed on both targets. Native CTest passes 4/4 (84.80 seconds), including core/startup/checkerboard and the
scene GPU suite. The focused outdoor regression passes 1,033 assertions, including all face/mip comparisons and
a noon-to-midnight pending-frame whiteout check. The core suite passes 98 cases / 2,251 assertions. Native window
resize, maximize, minimize/restore and clean exit (code 0) were verified; the refreshed capture was inspected.

The final outdoor WebGPU smoke and ordinary PBR smoke both pass. Chrome 154.0.8037.95 selected Intel Gen12LP;
the outdoor device requested 16 sampled textures and 16 samplers. The report contains no errors, bounded 0/1/7-pass
updates, six time/weather/UI capture scenarios and four completed generations during preset/running-clock checks.
The PBR WGSL ABI/derivative check, including actual browser compilation, also passes. Native GPU testing used an
NVIDIA GeForce RTX 3050 Ti Laptop GPU with D3D12 validation enabled. clang-format 22.1.3 and git diff --check pass.

Visual inspection caught and resolved daylight IBL being overexposed after a midnight preset. The corrected pending
capture retains visible surfaces without whiteout. Total IBL filtering work is unchanged; scheduling now bounds that
work per frame. Atmospheric regeneration, first-use compilation, total frame time and 24-frame update latency remain
outside this performance claim. Browser timing collected alongside another browser smoke is not a speed benchmark.

## Contract and Quality Baseline

A render performs at most one capture draw or seven filter draws, never all 132. Capture uses the same frozen
sky/cloud/camera/time for all faces. Materials sample only complete generations; the old blend slot is not written
while sampled. The source snapshot age includes generation time. Retain the 16-texture portable material profile.
Use actual GPU integration assertions rather than mock RHI tests; no numerical coverage threshold is introduced.

## Context and Orientation

`src/render/environment-renderer.*` owns the job and two filtered slots. `sky-renderer.*` owns capture and sky lookup
textures. `graphics.*` selects valid material bindings and reports diagnostics; `render-settings-panel.cpp` displays
them. `tests/outdoor-rendering-test.cpp` and `tools/outdoor-smoke.mjs` already exercise D3D12/WebGPU.
Dependencies remain Slang RHI 16324a68af477baaede620e713644f5e9613b1a2 and ImGui b48d1afbe8ee8b238e2961dc363a949dd7304e23.

## Plan of Work

First implement a concrete job state inside EnvironmentRenderer and split SkyRenderer capture into begin/face
operations. Then prove frozen captures and publication with real texture readback, and expose progress for browser
smoke checks. Finally build both targets, inspect final/transition captures and update the public lighting contract.

## Concrete Steps

Use the x64 Visual Studio discovery/environment procedure in DEVELOPING.md. Run `cmake --preset native-debug`,
`cmake --build --preset native-debug --parallel`, `ctest --test-dir build/native --output-on-failure`.
Use EMSDK=C:/tools/emsdk with the documented Ninja path for `cmake --preset web` and
`cmake --build --preset web --parallel`, then `npm.cmd run smoke:outdoor`.
Also ran `node tools/pbr-shader-check.mjs`, `node tools/pbr-smoke.mjs` and the focused native outdoor test.
Used installed clang-format 22.1.3 on changed C++ files and `git diff --check`.

## Milestone Review

Reviewed queue ordering: sky update precedes snapshot copy; each face uses frozen CPU inputs and the copied LUT;
all filter submissions precede generation publication/material draws. Previous-frame draws precede subsequent writes
on the same queue, and the blend gate prevents overwriting a still-selected previous slot. Commands own GPU references;
the CPU job retains no scene geometry. No worker thread, generic scheduler, new texture/sampler binding, scene
setting or dependency was added. One float4 uniform compensates source exposure per slot. Startup binds the existing
valid baked texture with zero intensity, rather than reading uninitialized dynamic storage. Source age now includes generation time, independently of publication/blend time. Updated obsolete immediate
rebake/4-Hz assumptions in the UI and lighting guide. Native validation, formatting, PBR WGSL/actual-browser shader
checks and final browser runtime checks pass. Reviewed screenshot evidence for startup, completed environments and
the repaired pending midnight transition.

## Validation and Acceptance

Verify first publication only after 24 updates; later jobs retain the published slot during partial work; paused
identical settings do no work; edits during jobs eventually publish coherent snapshots. Compare frozen-job output
with a static reference on the same GPU. Capture outdoor time presets only after complete publication. Check browser
validation errors, running clock, UI edits and resize. Report frame timing only with backend, device and workload;
do not claim isolated GPU improvements from callback/wall timing.

## Idempotence and Recovery

Preserve existing uncommitted lighting work. Build trees are reused. Submitted commands retain their resources and
the ordered graphics queue makes final publication safe without host waits. Shutdown follows existing queue drain.

## Artifacts and Notes

Evidence is saved under artifacts/lighting. `native/amortized-test.log` records the focused 1,033-assertion pass;
`native/window-report.json` records presentation/exit checks. `browser/ui-midnight-before-exposure-fix.png` preserves
the discovered regression; the final smoke refreshed `browser/ui-midnight-updating.png` with the corrected capture.
The final browser report includes per-job step/pass/publication histories and running-clock updates.

## Interfaces and Dependencies

No new dependency or public scene setting. A frozen 512x256 RGBA16F sky lookup adds 1 MiB of texture payload.
Progress diagnostics describe submitted work; they do not claim GPU completion or residency.
