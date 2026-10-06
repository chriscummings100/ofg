# Implement and prove terrain streaming

This living ExecPlan follows [PLANS.md](../../PLANS.md). Status: complete.
The user authorized implementation of the expanded terrain streaming specification on 2026-10-04.

## Purpose / Big Picture

Build a sparse 3D grid of octrees, first with deterministic noise, on native D3D12 and browser WebGPU.
Terrain generation and streaming are separate responsibilities. Each admitted root has exactly one complete
displayed cut. Replacement waits for all eight children and compatible boundaries; ancestors remain resident.
Background CPU workers build immutable values. The application thread owns topology and publication.
Noise defines a height surface, but meshes obey all six node faces. Use Transvoxel regular/transition cells,
matching boundaries and a 2:1 neighbor spacing restriction. Popping is permitted; skirts are not.
New roots may visibly load. Island hydrology, caves, editing, foliage, walking and error-driven resolution follow later.

## Progress

- [x] (2026-10-04) Inspected clean working tree, current renderer, build workflows and test registration.
- [x] (2026-10-04) Implemented addressing, state machine, atomic cuts and independent coverage/geometry oracles.
- [x] (2026-10-04) Implemented boundary plans, reservations, cancellation, retirement, exhaustive/randomized tests.
- [x] (2026-10-04) Native threads and browser WASM workers generate deterministic noise/Transvoxel geometry.
- [x] (2026-10-04) Native/browser preparation, actual completion and relative rendering are integrated.
- [x] (2026-10-04) Added laboratory controls, browser smoke, inspected native/browser images and ten-minute routes.
- [x] (2026-10-04) All eight mutation checks detected their faults; completed documentation and milestone review.
- [x] (2026-10-04) Final browser build, terrain smoke and worker isolation smoke passed; plan archived.

## Surprises & Discoveries

The current tree includes animation and a shared compute-to-vertex proof added since planning. Preserve these.
The pinned WebGPU RHI fence advances at submission, not GPU completion. Retirement must use actual queue callbacks.
The previous terrain proposal existed in conversation only; this file records its implementation contract.

## Decision Log

2026-10-04: user selected height-only noise, exact 3D bounds, matching boundaries, independently refining siblings,
retained ancestors, visible loading for new roots and background CPU workers on both platforms.
Browser workers use a separate GPU-independent WASM generator, without changing renderer threading.
Use concrete terrain data/functions, not the existing gameplay State inheritance tree.

## Outcomes & Retrospective

Native D3D12 and browser WebGPU now stream deterministic noise terrain with atomic prepared cuts, bounded payload
reservations, cancellation identities and completion-aware retirement. Laboratory controls expose demand, LOD, bounds,
fault injection and residency. CPU, allocation, exhaustive, randomized, mutation, GPU and browser checks are implemented.
Ten-minute routes on both hosts maintained payload budgets and drained obsolete terrain afterward.

This is a rendering/streaming foundation. New root coverage loads visibly, detail changes pop, and the generator uses
32 intervals per node. Hydrology, caves, foliage, walking and error-driven sample counts remain future work. Payload
accounting is exact in requested buffer/vector-capacity bytes; the separate worker/metadata allowance is not measured
process RSS. The main lesson was to bound planning work and revalidate every candidate rather than cache publishable
cuts. Final browser verification after the last capacity guard passed.

## Contract and Quality Baseline

Signed 64-bit root coordinates plus integer octree coordinates identify nodes. Use floor division, half-open
ownership, integer-cell/double-local observer positions and camera-relative rendering. Child groups are all eight
or absent. Node states are Pending, Loading, Loaded and Failed; absence implies not loaded. Loading covers workers
and uploads. Demand and the displayed cut are independent of build state. Empty is an explicit successful payload;
only a conservative emptiness certificate prunes future refinement.

I1: admitted roots are covered exactly once. I2: displayed nodes never overlap ancestors/descendants.
I3: every displayed payload and boundary configuration is prepared. I4: children exist in complete groups.
I5: displayed descendants retain loaded ancestor bases. I6: completions require exact request identity.
I7: cancellation preserves displayed fallback. I8: neighboring spacing differs by at most 2:1.
I9: allocations/reservations are charged exactly once. I10: outstanding draws/submissions prevent reclamation.
I11: pending/loading/failed payloads never enter render snapshots. I12: publication is atomic.

Root admission is distinct from demand: loading roots have no coverage claim. Culling does not alter the cut.
Generator reset increments an epoch and explicitly withdraws old coverage. Retry allocates a fresh request sequence.
Workers never receive node/scene/GPU pointers. Cancellation invalidates the owner before notifying the worker, but
does not release job memory until terminal acknowledgement. Duplicate/late messages cannot clear a newer request.

## State machine and pseudocode

`TerrainNode` owns an address, base BuildSlot, hysteresis flag and optional complete ChildGroup. BuildSlot owns
Pending/Loading/Loaded/Failed, optional RequestId, retaining prepared payload and error. Boundary variants have
separate slots keyed by content and neighbor configuration; rebuilding one does not invalidate the current variant.

```text
update:
  apply reset/retry commands; consume actual GPU completions and drain retirement
  update distance demand; reconcile root demand and plans
  invalidate obsolete requests, protecting the displayed cut and its ancestors
  consume worker/upload events after exact identity validation
  plan root admission/removal, splits and bottom-up merges
  reserve complete dependency groups; dispatch bounded work and advance uploads
  revalidate and commit prepared replacements; prune unused complete child groups
  extract one retaining render snapshot; check invariants

accept result:
  find outstanding request and its current owner
  missing/cancelled/mismatched owner => dispose result and finalize only that job
  duplicate terminal result => dispose incoming data without modifying owner
  failure => Failed(error); cancellation => Pending; finalize job
  validate geometry and reserved limit
  empty => Loaded(explicit empty payload); finalize job
  geometry => bounded upload queue; remain Loading until renderer preparation

refine:
  replace selected parent with eight children in a candidate cut
  recursively split coarser neighbors until candidate is 2:1 balanced
  include changed boundary variants on both sides; reserve dependencies
  lack of capacity => keep current cut and report budget blockage

coarsen:
  require all eight immediate children are selected leaves
  replace them with retained parent in a candidate cut
  defer if neighbor balance would fail; prepare changed boundaries

commit:
  reject obsolete demand or changed neighbor identities
  wait for every base and boundary payload
  allocate complete next snapshot, then verify coverage and compatibility
  publish through nonthrowing ownership swap; retire detached submitted data
```

Distance less than 2 node widths requests refinement; greater than 2.5 widths requests coarsening. At thresholds
and inside the hysteresis band preserve the previous flag. Balance demand is separate. Maximum depth stops splits.
New root admission must match existing neighbor detail, potentially building a refined initial cut.
Conflicting plans serialize/revalidate; independent regions must progress despite another region's held/failed work.
Failures are terminal until retry or a genuinely new request. Coarsen one level at a time to avoid deadlock.
Publication must not allocate, call workers or perform GPU operations after its final validation.

## Context and Orientation

`ofg-core` is GPU independent. Graphics prepares immutable Mesh resources lazily today; terrain needs explicit
preparation before cut publication. The scene uses local float transforms; terrain needs separate global addressing.
Native/browser hosts share Game, Graphics and ImGui. Native uses MSVC x64; web uses the pinned Emscripten integration.
Use the repository build skills and preserve existing fixtures. The historical repository remains read-only.

## Plan of Work

Checkpoint 1 establishes the controller and deterministic tests before workers. Checkpoint 2 adds complete boundary
transactions, budgets, cancellation and generated schedules. Checkpoint 3 implements real workers and meshing.
Checkpoint 4 integrates renderer preparation and actual GPU completion. Checkpoint 5 delivers --terrain / demo=terrain,
ImGui LOD/bounds diagnostics, observer freeze, teleports, fault injection and stress validation.

Defaults: root width 1024 m, smallest node 32 m, 32 sample intervals per axis plus boundary samples, root demand
4096 m and retention 5120 m, two workers, 128 MiB CPU payload plus 128 MiB worker/metadata allowance, 256 MiB GPU payload, 16 MiB incomplete
staging and 4 MiB uploads/frame. Ancestors, variants, cancelled jobs and retirement remain charged. Job scratch is
limited to 16 MiB and each browser worker heap to 48 MiB. The allowance covers metadata/transport without claiming
exact total residency. These are diagnostic settings, not performance promises.

## Concrete Steps

From C:\dev\ofg, initialize MSVC x64 using DEVELOPING.md. Existing commands are `cmake --preset cpu-tests`,
`cmake --build --preset cpu-tests --parallel`, `ctest --preset cpu-tests`; repeat native-debug for GPU integration.
The web workflow uses the existing web preset. Terrain fast, allocation, exhaustive and randomized CTests are registered
separately; the CPU preset remains GPU independent. Actual build, smoke, mutation and long-route commands are in
[DEVELOPING.md](../../DEVELOPING.md#terrain-streaming-laboratory).

## Validation and Acceptance

Tests drive the production controller with explicit worker/upload/completion events. No sleeps establish correctness.
Independent finest-cell coverage counts must equal one throughout each admitted root. Pairwise integer AABB checks
verify neighbor spacing; boundary identities must match actual neighbors. Deliberately corrupt snapshots to test oracles.

Required named/parameterized cases:

| ID | Scenario and required result |
| --- | --- |
| S01 | Root lifecycle: one request; no admission before preparation. |
| S02 | Eight-child creation, correct addresses, atomic allocation failure. |
| S03 | All 256 prepared subsets: only all eight plus boundaries permit replacement. |
| S04 | All 40320 child completion orders preserve parent until final dependency. |
| S05 | CPU completion alone does not admit geometry. |
| S06 | Partial uploads retain fallback. |
| S07 | Mixed empty/nonempty children publish together. |
| S08 | Sampled-empty coarse mesh does not suppress finer surfaces. |
| S09 | Certified empty regions avoid unnecessary jobs. |
| S10 | One sibling refines independently; seven others remain represented. |
| S11 | One-level merge reuses retained parent without regeneration. |
| S12 | Multi-level bottom-up merge converges without gaps or deadlock. |
| S13 | Hysteresis below/at/between/above thresholds. |
| S14 | Reverse demand before dispatch, during generation/upload and before commit. |
| S15 | Cancel/recreate: old success/empty/failure/cancel cannot mutate new request. |
| S16 | Duplicate terminal events cannot republish or double-release. |
| S17 | Failure of each child preserves parent; no retry loop. |
| S18 | Allocation/partial-upload failure preserves fallback and retires submitted data. |
| S19 | Explicit retry uses fresh identity, rejects stale failure and converges. |
| S20 | Held/failed seams preserve current geometry. |
| S21 | Changed neighbor invalidates old boundary result. |
| S22 | Same-root/cross-root 2:1 balance through splits and merges. |
| S23 | Conflicting operations serialize; unrelated regions progress. |
| S24 | Admission beside detailed root never publishes incompatible topology. |
| S25 | Removal updates surviving neighbor boundaries atomically. |
| S26 | Teleport cancels old work, prioritizes coarse coverage, reports loading. |
| S27 | Reset/reseed isolates epochs and drains old allocations. |
| S28 | Zero/exact/one-byte-over capacity boundaries. |
| S29 | Complete group reservation prevents a stranded eighth child. |
| S30 | Oversized/malformed geometry fails usefully and releases ownership. |
| S31 | Cancelled job remains charged until acknowledgement. |
| S32 | GPU retirement waits for completion, not submission. |
| S33 | Draw snapshot retains resources after tree release. |
| S34 | Cancellation during submitted upload respects last-use serial. |
| S35 | Snapshot allocation failure preserves old cut. |
| S36 | Settled updates perform no work/allocations/publications. |
| S37 | Released capacity permits previously blocked progress. |
| S38 | Shutdown in every phase terminates workers and releases data safely. |
| S39 | Negative/distant addresses, all octants, boundaries and +/-1e12 m. |
| S40 | Frustum changes affect draws, not coverage/demand. |

Check invariants/accounting after every update. Exercise both event orders and same-update cancellation vs completion,
with eviction/recreation/retry/reset/shutdown. Run 32 fixed seeds x 5000 operations over two adjacent roots at depth 3,
recording replayable traces. Under settled demand, sufficient capacity and successful one-step work, require tiny
fixtures to converge within 64 updates and depth-3 fixtures within 1024, then become idle.

Seed and remove eight faults to prove sensitivity: seven-child publication, CPU-ready admission, address-only identity,
early cancellation release, early parent removal, stale seams, submission-time retirement, partial-group reservation.

Geometry tests cover flat/sloped/noisy surfaces, exact zeros, six faces, edge/corner joins and distant translations.
Shared samples agree exactly; corresponding positions agree within 1e-5 times sample spacing. Check boundary topology.
Real worker checks prove off-thread execution, independent progress, responsiveness, cancellation and teardown.
GPU/browser tests prove uploads, rendering, actual completion, lifetime, camera-relative shadows, resize/reload/teardown.
Inspect native/browser images and transition motion, with no unexpected validation errors. Run ten-minute routes on
both hosts; residency is bounded and obsolete work drains. Save evidence under artifacts/terrain.

## Milestone Review

2026-10-04: reviewed controller publication and cancellation, snapshot ownership, queue completion, address precision,
seam tolerances, worker limits, readability, source comments, dependency notices and existing-fixture regressions.
Resolved two final findings: cancellation before GPU buffer creation now charges only actual buffers, and exhausted
root metadata capacity defers admission instead of throwing out of the frame loop. Targeted regressions pass.
Diagnostic text wraps after panel resize. The historical repository and existing dependency pins remain unchanged.

Coverage policy is behavioral: the S01-S40 matrix, independent oracle self-tests, exhaustive schedules, allocation
faults and eight mutation checks cover the important contracts. No line-coverage percentage or formal proof is claimed.
Native/browser integration remains necessary because CPU tests cannot establish shader or presentation behavior.

## Idempotence and Recovery

Keep existing work and dependency pins. Retry failures explicitly. Reset invalidates old epochs without reusing IDs.
Retain valid display fallback when new generation/preparation fails. Record unavailable checks honestly.

## Artifacts and Notes

Validation on 2026-10-04 (commands in DEVELOPING.md):

| Check | Observed result and durable evidence |
| --- | --- |
| Native build and full CTest | All eight checks passed, 202.53 seconds, including existing core/scene/GPU tests. |
| Fast and allocation checks after final fixes | Both passed; GPU partial-upload preparation passed 18 assertions. |
| Exhaustive controller schedules | All 40320 child completion orders; 2,217,601 assertions passed. |
| Randomized controller schedules | 32 seeds x 5000 operations; 480,608 assertions passed; `artifacts/terrain/traces`. |
| Allocation fault sweep | 3463 assertions passed, including settled updates with allocations disabled. |
| Eight deliberate controller mutations | All compiled and were detected; restored controller passed; `artifacts/terrain/mutations/report.json`. |
| Native ten-minute route | 4131 frames in 600.06 seconds, 12444 assertions, no terrain failures; `artifacts/terrain/native/traversal.csv`. |
| Browser ten-minute route | 601 samples through 600 seconds, no unexpected errors; `artifacts/terrain/browser/traversal.json` and WebM recording. |
| Native presentation | Window capture, resize, minimize/restore and clean close passed; `artifacts/terrain/native/window.png`, `resized.png` and `restored.png`. |
| Browser presentation and worker checks | Terrain teleport, LOD/bounds controls, resize and reload; independent workers, heartbeat, held job, cancellation, failure and termination. Final post-capacity-fix terrain and worker smoke both passed with no unexpected errors. |
| Existing browser regression | `npm.cmd run smoke:web` passed scene/checkerboard in Chrome 154.0.8037.95. |
| Formatting | clang-format 22.1.3 dry-run passed for changed project C++ files; vendored tables excluded. |

Hardware: Intel Core i7-12700H, NVIDIA RTX 3050 Ti Laptop GPU. Native route used D3D12 Debug validation at 960x640
offscreen; browser route used Chrome WebGPU with a 1200x800 viewport and 900x600 video recording. These are correctness/stress runs, not performance targets.
Native peak charged CPU payload/reservations/retirement was 134062336 bytes and GPU 118494504 bytes; browser peak
charged CPU was 134204788 bytes. Both stayed within configured budgets and drained all terrain nodes/jobs/payloads
at the end. Inspectable captures include `browser/origin.png`, `browser/lod-bounds.png`, `browser/resized.png`,
`browser/reloaded.png`, native terrain captures and extracted browser motion frames at 150 and 330 seconds.

The full native suite, mutation run and long routes preceded the final small GPU-byte accounting/root-capacity fixes;
focused native checks were rerun afterward. Final browser build and both smoke scripts passed after those fixes and
after tightening the worker heap/scratch caps.
The ten-minute routes were not repeated for those localized guards. Payload accounting does not include allocator,
driver or preexisting lighting-resource overhead. New-root loading gaps are allowed by the admission contract.

Implementation discoveries: an unbounded candidate scan took 328 ms at 150 selected nodes and 8157 ms at 360 in Debug.
Rotating through two candidates per update reduced initial admission frames to 20-36 ms in that observation, but does
not establish a settled performance claim. MSVC debug iterator proxies allocate inside noexcept constructors, so the
isolated allocation test compiles every participating translation unit with `_ITERATOR_DEBUG_LEVEL=0`. Production
builds keep their usual settings. A seam oracle originally used rounding buckets, which split positions one float ULP
apart at bucket boundaries; it now welds by actual distance within the stated tolerance. JavaScript nullish coalescing
inside EM_ASM was split by clang-format, so the route getter uses a formatter-safe finite-number test.

## Interfaces and Dependencies

Concrete addresses, requests/results, build slots, controller, retaining snapshots and diagnostics form the CPU boundary.
Renderer preparation and completion tracking own GPU details. Generator source is shared with native/browser workers.
See [the implementation contract](../terrain-streaming.md) for code examples and [Transvoxel provenance](../../external/transvoxel/README.md)
for retained licenses. Eric Lengyel tables are pinned to `51a494f03c5b024cd153b596bcc7152eb3cc93a6`; the inspected
Godot Voxel face-orientation reference is `045ff9326a32fd10ac6fdac6357b5a098aaf2093`. No external engine runtime was added.
