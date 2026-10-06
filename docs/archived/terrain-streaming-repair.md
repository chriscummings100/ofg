# Repair terrain streaming priority, convergence and frame cost

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: complete. The user reported irregular nearby detail,
loading that does not proceed outward from the player, and approximately one frame per second on 2026-10-05.

## Purpose and contract

Make the noise terrain laboratory useful interactively. Preserve atomic compatible cuts, retained fallback,
request identity, memory limits and both platform targets. Prioritize coverage and refinement by observer distance.
Measure the existing behavior before optimizing it; verify meaningful nearby detail, not merely a selected-node count.
Existing uncommitted implementation work belongs to this task and must be preserved.

## Progress

- [x] (2026-10-05) Inspected the screenshot and current controller/adapter. Jobs are zero while detail remains coarse.
- [x] (2026-10-05) Reproduced stationary stagnation at 514 nodes; measured CPU update and GPU phases separately.
- [x] (2026-10-05) Fixed priority, dependency waves, idle retry loops and redundant planning; focused regression tests pass.
- [x] (2026-10-05) Verified native/browser visuals, controls and measured native frame behavior; updated documentation and launched the optimized repair.

## Discoveries and decisions

The previous acceptance test stopped at 200 selected nodes, before stationary refinement settled. The planner sorts
operations by distance but applies a rotating cursor to the entire changing list, so sorting does not establish priority.
Every candidate previously rebuilt balance and boundary requirements for the entire cut. Profiling below confirmed
the resulting repeated planning cost. Worker count remains unchanged; the added seventh band later justified a measured
payload-budget increase, recorded below.

## Work and validation

Add a repeatable finite profiling scenario using the actual laboratory and renderer, recording phases and nearby depth.
Use focused deterministic CPU tests for priority, stalled capacity, convergence and fallback. Rerun existing exhaustive,
randomized and allocation checks for controller changes. Use build-native/build-web workflows for integration and inspect
fresh captures, including LOD colors. Record backend, hardware, viewport and debug settings with performance evidence.
Keep diagnostics and benchmark commands reproducible; avoid timing-dependent unit assertions.

## Outcomes and review

The repaired native workload converges, follows the moving observer and renders interactively. Browser convergence,
presentation and worker checks passed. The previously completed plan's tests established coverage/lifetime properties but did not establish adequate
loading priority, stationary refinement convergence at runtime budgets or interactive performance at the user's viewport.


Baseline: `artifacts/terrain/profile/before.csv`, NVIDIA RTX 3050 Ti Laptop GPU, i7-12700H, D3D12 validation,
Debug, 1440x1200 offscreen, sixty seconds. Streaming updates repeatedly cost 120-225 ms at a stalled cut while
render submission cost about 26 ms and completion wait about 9 ms. This reproduces a CPU stall, though not the
exact one-second frame at the user's larger interactive window.

First repaired Debug run: `after-debug.csv`; finer surface LOD 4, 1088 selected nodes, 98 MiB resident CPU payload,
five refinements legitimately blocked by conservative sibling reservations at the configured budget. Idle streaming
updates cost about 0.6 ms. Active preparation still costs tens of milliseconds in Debug; the optimized build with
symbols is a separate measured comparison, never substituted for same-build evidence.

Implemented nearest-first passes that sleep until meaningful input changes, dependency waves with complete sibling
base reservations, incremental balancing lookup, reuse of unchanged cut dependencies, and conservative per-node noise
height bounds. Tests cover outward dispatch despite list changes, closure sizes larger than the job limit, allocation
failure recovery, no allocations while budget-blocked, and local empty certificates within global height bounds.
The noise bound follows bilinear extrema with monotone quintic interpolation, not a sampled emptiness guess.


2026-10-05 steering: the user specified roughly circular distance bands, 16-metre finest cells through 1024-metre roots,
and parent-before-child generation. Added a seventh depth and a near-surface spawn. The band test drives the production
controller on an exactly known plane and samples 64 directions at twelve radii. It compares actual selected depth to an
independent dyadic AABB-distance calculation (one extra level is allowed for neighbor balance), checks all seven widths,
and checks the parent state before accepting any result from the dispatch batch. Actual cut maps at 8192/1024/128-metre
spans are generated from the test CSV. Grid quantization explains coarse corners; it does not permit missing detail
inside the required band. Per-node vertex counts remain fixed at 32 intervals for this proof; variable counts need a
separate sample-spacing/boundary-ratio contract and are not claimed implemented.


The measured seven-band noise workload exceeded the previous 128 MiB payload budget: it stopped with 102 requested
refinements unresolved. The payload budget is now 256 MiB, still with the separate 128 MiB non-payload allowance and
unchanged 256 MiB GPU limit / two workers / 32 job limit. With that budget the real stationary noise workload reaches
16-metre surface cells and zero unresolved refinements. This is a measured capacity correction for the added seventh
band, not a substitute for the same-build planning fixes. Final timing and visual checks follow below.


Optimized stationary result after budget correction: 1529 selected nodes, 16-metre finest surface cells, zero jobs and
zero unresolved refinements; 158391552 bytes resident CPU payload. This meets the real noise convergence condition,
not only the flat-plane policy test. CPU streaming settles around 0.1 ms. The profile now asserts convergence; browser
smoke also waits for all refinement demand to complete instead of stopping at a small arbitrary node count.


Moving-route review caught an additional starvation bug: ineligible ancestor merges were included in the operation
queue and each small observer displacement restarted its priority pass, starving eligible bottom-up merges. Candidate
merges now require eight selected immediate children; sub-threshold movement reevaluates demand without restarting all
blocked work. A dedicated continuously-moving observer regression proves coarsening progresses. The route now follows
the height surface with eight metres clearance, and its final mesh bounds must remain within 64 metres of the observer;
a stale fine cut elsewhere cannot count as a successful moving test.


Browser convergence initially timed out. A thirty-second diagnostic showed only 88 coarse roots admitted with zero
worker failures: each 256-cell batch used a nested zero-delay timer. Replaced that continuation with a MessageChannel
message task, preserving the same bounded batches and cancellation yield points. Worker responsiveness and full browser
convergence subsequently passed; the original timeout is retained as a failed diagnostic run.

## Validation evidence and review

On 2026-10-05, all nine optimized CTests passed (56.74 seconds), including GPU rendering and the six CPU suites.
The Debug build and all six CPU CTests also passed (107.46 seconds). All eight isolated seeded mutations were detected;
the restored controller passed. The final stationary profile passed 4259 assertions and the moving profile passed 6285,
including actual terrain remaining within 64 metres of the moving observer. Both profiles run for sixty seconds. The
previous implementation's ten-minute traversals were not rerun for this repair and are historical evidence only.
Final review added an explicit outward-refinement regression alongside outward root admission: with one sibling group
of job capacity, all four groups dispatch in increasing parent distance while prior cuts publish. The focused optimized
terrain-fast suite was rebuilt and passed again (0.22 seconds).

Measured alone on an i7-12700H / NVIDIA RTX 3050 Ti Laptop GPU, native D3D12 with validation, at 1440x1200 offscreen:

| Workload/build | Median terrain update | Median complete frame | Frame p95 |
| --- | ---: | ---: | ---: |
| Original Debug, stationary overview | 161.429 ms | 197.4822 ms | 260.9713 ms |
| Intermediate repaired Debug, stationary overview | 0.5822 ms | 51.9662 ms | 63.4148 ms |
| Final optimized, seven bands, near-surface stationary | 0.1068 ms | 26.1365 ms | 35.4499 ms |
| Final optimized, near-surface movement at 96 m/s | 5.1405 ms | 18.2263 ms | 26.7978 ms |

Stationary measurements use the last fifteen seconds; movement discards the first five seconds. Frame time sums CPU
update, extraction, submission and explicit queue-completion wait. CSVs and `comparison.json` are in
`artifacts/terrain/profile`. The intermediate Debug run retained the earlier six-depth setup; the final run adds the
seventh band and changes camera height. Compiler optimization and different workloads are not attributed to controller
optimization. These are measured medians, not a frame-rate guarantee at arbitrary window sizes or during teleports.

`tools/terrain-native-smoke.ps1 -Executable build/native-relwithdebinfo/ofg.exe -ArtifactDirectory artifacts/terrain/repair`
passed startup, resize, minimize/restore and clean close. Inspected `window.png` shows 16-metre cells, zero remaining
refinements, idle planning and about 46 fps at that instant. The first harness run captured successfully but could not
read the closed process exit code; retaining the Windows process handle fixed the harness, and the rerun passed.
The flat-plane `bands.png` was inspected at all three zooms. It shows the published production-controller cut, not an
illustration of the intended policy. clang-format 22.1.3 dry-run passed for all changed project C++ files; git diff check
passed. Ownership, atomic publication, cancellation, retirement, comments and bounded work were reviewed; the review
found and fixed moving-merge starvation and the browser timer bottleneck described above.

The final WebAssembly build, `node tools/terrain-smoke.mjs` and `node tools/terrain-workers-smoke.mjs` all passed.
Origin converged to 1529 selected nodes, distant negative coordinates to 1419; both had 16-metre finest cells, zero jobs,
zero unresolved refinement and idle planning. UI convergence, LOD/bounds controls, resize and reload also passed. Inspected
origin, colored bounds, resized and reloaded screenshots under `artifacts/terrain/browser`; both JSON reports have zero
recorded errors. The known Emscripten Asyncify/exception build warning remains. Browser checks establish correctness,
not native performance parity: this headless Chrome run displayed about 6-7 fps, and its selected adapter was not queried.

Launched `build/native-relwithdebinfo/ofg.exe --terrain` interactively and verified that its titled native window remained
alive. The user can inspect LOD colors and return near the surface with F. Remaining limitations: per-node sample counts
are fixed, transitions can pop, sustained fast motion has pending refinements, and payload counters exclude total process
and driver overhead. The band proof, measured native performance and current smoke evidence address this repair's scope.
