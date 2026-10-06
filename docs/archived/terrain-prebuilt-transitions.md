# Generate terrain transition faces once per node

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: completed 2026-10-06.
Checkpoint commit `4adb43d` preserves the complete prior terrain laboratory and repair.

## Purpose and contract

Every node builds its regular surface and all six transition faces together. One immutable vertex/index allocation
contains seven contiguous index ranges. The published cut chooses the active faces; changing neighbors never creates
another generation request for an already loaded node. Keep current shared-plane transition geometry, consistent noise
samples, 2:1 neighbor balance, parent-before-child dispatch, atomic compatible publication, cancellation identities,
bounded uploads and real GPU retirement. No new shader, variable sample count or terrain generation feature is included.

## Progress

- [x] (2026-10-06) Committed all prior work; inspected geometry, state ownership, worker transport and submesh drawing.
- [x] (2026-10-06) Generate and transport seven validated ranges; render selected regular/face submeshes in color and shadow passes.
- [x] (2026-10-06) Remove content variants; keep one build slot per node and derive face masks only during cut publication.
- [x] (2026-10-06) Updated regressions, verified native/browser behavior, inspected captures and updated contracts and evidence.

## Discoveries and decisions

The existing regular mesh and each face use fixed samples and positions; the mask only selects which triangles to append.
Mesh already supports submeshes, so use it directly rather than adding a terrain graphics interface. Inspection found
Mesh rejects empty submeshes: each payload maps seven terrain parts onto only the nonempty Mesh submeshes, using -1 for
empty parts. The general Mesh contract is unchanged. Entirely empty nodes still use successful empty payloads.
The worker protocol carries eight index boundaries for seven ranges and no neighbor mask. Tests select ranges
when assembling mixed-LOD surfaces, preserving the existing independent welded-edge oracle.
Keep balanced candidate topology planning in this milestone; removing mesh variants is the requested simplification,
and a second scheduling redesign would obscure its correctness and cost.

## Validation and concrete steps

Use build-native and build-web skills and the commands in DEVELOPING.md. Build optimized native, run all nine CTests,
and run allocation, exhaustive, randomized, band and mutation checks without weakening their behavioral assertions.
Add regressions proving masks can change both ways with the same payload and no regeneration; all face ranges must be
ready at initial load. Check malformed range transport, all six faces and simultaneous edge/corner joins. GPU integration
must prove selected submeshes actually draw and toggling does not allocate/reupload buffers. Rebuild web and run terrain
worker and browser smoke; inspect screenshots. Run a sixty-second stationary profile to require real noise convergence
within unchanged budgets. Timing comparisons require isolated runs with hardware/backend/viewport recorded.

## Outcomes and review

The implementation and contract checks are complete. Review covered single-payload ownership, publication boundaries,
worker transport, empty parts, cancellation, retirement and comments. No dependency or shader changes were needed.
No coverage percentage is claimed; contract tests and seeded mutations remain the focused coverage policy.

Initial optimized validation: all six CPU suites passed, including the new same-payload/no-job face-toggle regression.
The subsequent full optimized run passed all nine CTests (116 seconds), including new malformed-range validation and
GPU readback for seven separately selected submeshes with zero further upload allowance. Existing welded-edge tests
still cover all six faces and simultaneous edge/corner joins.
Operation codes now use a named enum. The controller has one Slot per Node, NodeAddress-only jobs and dependencies,
and computes face masks only when publishing a complete topology. Removed ContentKey and per-node variant maps.

All eight controller mutation faults were detected after adapting their sites to address-only content; restored source
passed. Native presentation smoke passed startup, resize, minimize/restore and close, with empty stderr. Inspected the
new `artifacts/terrain/prebuilt/window.png`: cut 1529, zero remaining refinement, finest cell 16 m, zero failed jobs;
CPU payload 103.9 MiB, GPU payload 68.5 MiB (previous snapshot 151.1 / 96.2 MiB). This capture overlapped compilation,
so its displayed frame rate is not a performance comparison. The isolated profile follows below.

The sixty-second stationary native profile subsequently passed 1698 assertions: 1529 cut entries, zero jobs/unresolved
refinements, finest depth 6, CPU payload 108996096 bytes. Evidence: `artifacts/terrain/prebuilt/stationary.csv` and
`profile.json`. Same i7-12700H / RTX 3050 Ti Laptop GPU, native D3D12 validation, optimized build, 1440x1200 offscreen,
no simultaneous build or browser tests. Last fifteen seconds: median streaming update 0.1931 ms, median complete frame
70.9759 ms, p95 94.7889 ms. The selected triangle count remains 295066; visible draw count grows from the previous
150 to 219 because active face ranges are separate submeshes. This run is slower than the prior day's 26.1365 ms median;
the additional draw calls are an explicit tradeoff, and the entire difference has not been isolated to this change.
No rendering performance improvement is claimed. Budgets and worker count remain unchanged.

Browser worker smoke first failed with RuntimeError: unreachable when accessing an unexported HEAPU32 view. The
transfer now copies the 32 metadata bytes through the already-exported HEAPU8 and views that owned copy as Uint32Array.
CMake reconfiguration copied the corrected worker script. Worker smoke then passed independent-worker execution,
all eight range offsets, cancellation, injected failure, release and shutdown.

Full WebGPU terrain smoke passed origin and distant-coordinate convergence, UI face/bounds toggles, resize and reload.
The report contains no errors; origin and distant states both reach depth 6, zero unresolved refinements, zero jobs and
an idle planner within unchanged budgets. Inspected origin, distant, LOD/bounds and reloaded captures; the reload panel
also confirms 16 m finest cells, 1529 cut entries and zero failures. Browser evidence is preserved under
`artifacts/terrain/prebuilt/browser/`. Final focused terrain-fast regression, formatting and whitespace checks passed.

Remaining limitation: rendering performance needs separate investigation. The requested build-state simplification is
verified, but the measured frame cost above is worse than the previous recording. Seven resident parts trade extra draw
submissions for immediate face selection; no renderer redesign or speculative optimization is included here.
