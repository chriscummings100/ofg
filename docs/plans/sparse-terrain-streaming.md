# Simplify sparse terrain streaming and share C++ workers across hosts

This ExecPlan follows [PLANS.md](../../PLANS.md). The streaming redesign is a saved proposal;
only the browser C++ threading migration is authorized for implementation now (2026-10-07).
Do not treat completion of that migration as completion of the tree redesign.

## Purpose / Big Picture

Support sparse, roughly 128 km islands with 16 m finest cells, while keeping streaming understandable
and rendering independent of traversal latency. Replace repeated per-operation replacement planning
with complete demand, boundary and display-selection passes. Give a single streaming thread ownership
of the mutable tree in a later milestone. First establish shared-memory C++ generation threads on web
and native, removing the separate browser generator WASM and JavaScript message/copy transport.

## Progress

- [x] (2026-10-07) Inspected current address, planner, native worker, web transport and build configuration.
- [x] (2026-10-07) Saved the proposed streaming design and its correctness/testing contracts before implementation.
- [x] (2026-10-07) Migrated browser generation to the shared C++ worker implementation and verified both hosts.
- [x] (2026-10-07) Shared worker implementation compiles; all nine native Release CTest groups pass (53.93 s).
- [x] (2026-10-07) Browser worker proof reaches stage 5 with shared memory, geometry, cancellation, failure and restart.
- [x] (2026-10-07) Completed application link with threaded pinned Slang and browser integration evidence.
- [x] (2026-10-07) Pinned Slang source build, full web application and compute proof builds succeed.
- [x] (2026-10-07) Chrome worker lifecycle/isolation proof, terrain origin/distant/UI/resize/reload,
  compute-to-vertex and standard texture/checkerboard smoke pass.
- [x] (2026-10-07) Fifteen-second moving route drains roots, jobs and tracked CPU/GPU/retired terrain bytes to zero;
  inspected captured motion frames. Ownership/obsolete-code/documentation review and formatting checks complete.
- [ ] Future authorization: implement complete sparse demand and boundary traversals.
- [ ] Future authorization: implement balanced ready display selection and remove replacement plans.
- [ ] Future authorization: move tree ownership to a streaming thread and publish immutable selections.
- [ ] Future authorization: exercise depth 13 and configure large-island generation/render ranges.

## Surprises & Discoveries

The current web implementation has two dedicated workers, each with an independent generator WASM
heap. It copies geometry out of that heap into transferable buffers, then into the application heap.
It is not using Emscripten pthreads. Native already has two persistent std::threads with a bounded
queue, cancellation flags and condition variables. The existing address format supports depth 16.
Current planning copies and balances an entire candidate cut for each attempted local operation.
The laboratory still hardcodes 1,024 m roots and a 4,096 m admission radius; increasing tree depth
alone does not implement a large visible island. Existing renderer/profiling changes are uncommitted
and must be preserved.

The full pthread application link rejected the supplied Slang archive: --shared-memory is disallowed
because its objects lack atomics/bulk-memory features. The focused worker proof already runs successfully.
Build the identical Slang v2026.17.1 source (ca6e0a657c52881166295f11c14d57afdf0de481) with -pthread,
using native code generators. This is a build prerequisite, not a dependency upgrade. The initial smoke
queried an obsolete Emscripten runningWorkers field; current runtime tracks live threads in PThread.pthreads.

## Decision Log

- 2026-10-07: A natural island root is 131,072 m, with 13 subdivisions to 16 m: 14 size levels.
  World extent is independent of render distance. Only demanded branches are allocated.
- 2026-10-07: Complete traversal first; cap generation submissions rather than truncate demand discovery.
  Early exit would otherwise confuse unvisited nodes with unwanted nodes during retirement.
- 2026-10-07: Keep target and actual display balancing explicit. Distance bands may eventually guarantee
  target balance, but asynchronous readiness can still make the displayed cut unbalanced.
- 2026-10-07: Prefer one tree-owning streaming thread over parallel tree mutation. Immutable display
  snapshots isolate rendering; no per-node locking or general task framework is required.
- 2026-10-07: Reuse ordinary C++ threads on web through Emscripten pthreads and shared WASM memory.
  Cross-origin isolation becomes a deployment requirement. Keep graphics/UI calls on their existing
  application context. Moving the streaming controller off-thread remains a separate future milestone.
- 2026-10-07: Web worker threads detach and capture shared CPU State. Destruction cancels and wakes them;
  their last reference releases state on exit. This avoids UI-thread joins or Asyncify teardown and never
  exposes laboratory/GPU pointers to detached work. Native destruction retains synchronous joins.
  The lifecycle browser test verifies old and replacement workers drain from the runtime.

## Outcomes & Retrospective

Design saved and shared C++ threading migration complete. Native Release and browser functional checks,
moving-route drainage and final review pass. No new streaming algorithm, larger render
range or traversal performance improvement is claimed. The primary additional build cost is preparing
threaded Slang from the same pinned source version once; incremental application builds reuse it.

## Contract and Quality Baseline

The tree is sparse and dyadic. Creating children creates all eight metadata nodes or none, within the
node budget. Geometry resolution is independent of cell width; fine levels may have more samples or
additional features. Certified empty regions need no mesh but count as ready coverage.

Each admitted root has complete selected coverage with no selected ancestor/descendant overlap.
Face-adjacent selected leaves differ by at most one level. Transition faces are generated with each
payload and selected from the actual displayed topology; neighbour changes never rebuild geometry.
Every selected payload is upload-ready. A parent remains usable until a complete child replacement
is ready. Failures and memory pressure retain valid fallback coverage rather than opening holes.
Initial root admission and withdrawal are explicit coverage boundaries, including adjacent roots.

Node build state is Pending, Loading, Loaded or Failed. Visibility is membership in the immutable
selected cut, not an independently mutable lifecycle state. Absent metadata represents not loaded.
Loaded hidden nodes support fallback, incomplete sibling groups or pending retirement.
Request identity includes epoch and sequence: cancelled/stale results never overwrite current content.
Memory budgets include queued/running reservations, resident payloads and pending GPU retirement.

The proposed controller must implement these transitions explicitly:

| Event | State and ownership effect |
| --- | --- |
| Create missing child group | All eight metadata nodes become Pending together; no jobs start implicitly |
| Submit eligible request | Pending becomes Loading with unique ID and reserved memory; Loading includes queue time |
| Matching CPU result | Remains Loading while geometry is validated and GPU preparation completes; empty certified results need no upload |
| Matching upload-ready result | Becomes Loaded; display membership is computed separately for complete compatible coverage |
| Matching generation/upload failure | Becomes Failed with diagnostic; release worker reservations and retire any partially submitted GPU resources |
| Explicit retry | Failed becomes Pending; the next dispatch gets a fresh ID, never reusing failed identity |
| Cancel or supersede request | Detach request from node immediately; keep its outstanding reservation/lifetime record until acknowledgement |
| Late result from detached ID | Release/retire its resources; do not alter the replacement node or current cut |
| Demand lost while Loaded | Retain while needed by fallback, snapshots or GPU work; otherwise evict payload and eventually metadata |

Cancellation is a request-lifetime fact, not an additional visible node state. If the node still needs
content after cancellation, it is Pending; an unwanted node can be removed when no ownership dependency
remains. A selected node cannot be evicted before a replacement snapshot removes it. Running traversals
do not advance state incrementally where rendering could observe a half-complete sibling decision.

Topology balance alone does not permit arbitrary boundary sample spacing. The existing Transvoxel
generator expects compatible 2:1 sample spacing at transition faces. Keep its current sampling contract
during the redesign; increasing interior vertex density independently requires compatible boundary
samples or a separately proven transition scheme. A deeper octree does not settle that generator choice.

## Context and Orientation

`src/terrain/terrain-address.{h,cpp}` provides exact integer root/local coordinates, parent/child
addresses and camera-relative conversion. `terrain-stream.{h,cpp}` currently owns nodes and implements
per-operation candidate plans. A cut means a non-overlapping set of leaves covering admitted roots.
`terrain-workers-native.cpp` currently implements two persistent C++ threads; `terrain-workers-web.cpp`,
`terrain-worker-main.cpp`, `terrain-worker-wire.h` and `web/terrain-worker*.js` implement the old browser
transport. `src/lab/terrain-laboratory.cpp` schedules, uploads and renders the current stream.

Build prerequisites and exact environment setup are in DEVELOPING.md and the build-native/build-web
skills. At migration start Slang RHI is pinned to 16324a68af477baaede620e713644f5e9613b1a2,
ImGui to b48d1afbe8ee8b238e2961dc363a949dd7304e23, and the installed Emscripten SDK is documented
as 6.0.0. Verify actual toolchain/build compatibility rather than updating dependency pins.

## Plan of Work

### Milestone 1: Shared-memory browser generation (authorized now)

Rename the native implementation to a shared terrain-workers.cpp. Enable -pthread consistently across
compiled web code and executable links. Prewarm enough workers for the two persistent terrain threads.
Remove the separate generator module, wire format and JS transport. Add COOP/COEP headers to the
development server and an actionable startup failure when isolation is missing. Keep browser frame
polling nonblocking. Shutdown wakes/cancels workers: native joins, while browser workers detach and
retain shared CPU state until exit. Verify eventual drainage rather than blocking the browser UI on joins.

Replace the old browser worker smoke with a small C++ pthread proof compiled separately from graphics,
driven asynchronously by a browser frame loop. It must exercise the actual shared worker implementation:
independent completion with a held job, cancellation, failure, release, shutdown and reuse. Check shared
memory and page responsiveness, then run real terrain rendering, motion, retirement and reload checks.
Keep the complete doctest suite native. Record compiler and runtime limitations honestly.

### Milestone 2: Demand and boundary closure (proposal)

Use direct child links for tree traversal and an address hash index for cross-tree lookup. A lookup
does not submit a job. Ensuring a deep node also ensures its ancestor path and complete sibling groups.
Maintain counts rather than scanning the tree to count it. Reuse scratch queues and vectors.

On an observer/settings change, recompute demand with hysteresis using distance to each node's bounds:

```text
queue = relevant roots
while queue has nodes:
    node = pop_front(queue)
    mark node needed as payload/fallback
    if node is not certified empty and distance rule requests refinement:
        ensure all eight children, respecting metadata budget
        append children to queue
    else:
        add node to desired leaves

queue = desired leaves
while queue has affected leaves:
    leaf = pop_front(queue)
    skip entries no longer in desired leaves
    for each face neighbour in desired leaves:
        if neighbour is more than one level coarser:
            split that coarse leaf into all eight children
            mark children and ancestors needed
            enqueue affected leaves until no violations remain
```

Closure must finish before a target is treated as authoritative for retirement. If a budget prevents
closure, coarsen the target locally to a valid affordable selection; never publish a partial marking pass.
Neighbour lookup must handle finer neighbours, negative coordinates and adjacent roots. The existing
fine-to-coarse face lookup can detect violations by visiting every leaf without enumerating all finer faces.

Collect eligible sibling groups and submit within worker/memory limits. Parent content must be ready
before child generation; pending groups use coarse-level then distance priority with stable address ties.
An unavailable distant group must not prevent eligible nearer work. Traversal order alone is not priority.

### Milestone 3: Ready display selection (proposal)

```text
queue = admitted roots with loaded coverage
while queue has nodes:
    node = pop_front(queue)
    if refinement is needed and all eight child payloads are ready:
        append all children to queue
    else:
        select node

while selected face neighbours differ by more than one level:
    coarsen the finer region to an available loaded ancestor
    remove every selected descendant of that ancestor
    recheck affected boundaries

for each selected leaf:
    enable prebuilt transition faces toward selected neighbours one level coarser
publish complete selection atomically
```

Coarsening only moves upward, so the correction terminates. Retain loaded ancestors required for
fallback. Coarsening a region replaces all its selected descendants, not one sibling in isolation.
Root admission must wait for compatible adjacent coverage or participate in the same balancing step.
Do not mutate the previous render snapshot while assembling the next. Remove the old per-operation
plans, attempted-operation tracking and pending plan requirements once equivalent contracts are proven.

### Milestone 4: Single-owner streaming worker (proposal)

One persistent C++ streaming thread owns all mutable nodes, demand, job identities and selections.
It consumes the latest observer value (coalescing updates), completed geometry and upload readiness.
Generation workers own their running mesher/output until completion is transferred. Rendering owns
GPU preparation/submission and reports readiness and completed submission values back to streaming.
No GPU call is moved to a worker without verifying the actual RHI/backend ownership contract.

Finish a bounded reconciliation instead of continually restarting for new observer positions. Recompute
distance demand only when its inputs change; readiness can trigger selection without redoing distance
work. Publish immutable snapshots whose payload ownership outlives all readers and GPU submissions.
Rendering adopts the latest complete snapshot without waiting for traversal. Shutdown stops submissions,
cancels/drains generation, joins workers safely and retires graphics resources with existing fence rules.
Snapshots carry world addresses and payload references, not transforms frozen relative to an old camera.
Rendering rebases them against the live camera each frame even while streaming uses an older observer.

### Milestone 5: Large-root proof (proposal)

Configure root size/depth rather than retaining laboratory 1,024 m literals. Exercise 131,072 m roots
with 16 m leaves, including crossing roots and distant negative addresses. Independently choose render
distance, camera clipping/shadow range and coarse generator sampling. An island height/hydrology
representation is future generation work; sparse streaming does not itself provide that content.

## Concrete Steps

Run from C:/dev/ofg. Use the skill's Visual Studio amd64 environment for native commands and set
EMSDK=C:/tools/emsdk plus Emscripten/Ninja paths for web commands. Existing commands:

```text
cmake --preset native-release
cmake --build --preset native-release --parallel 6
ctest --preset native-release --output-on-failure
cmake --preset web
cmake --build --preset web --parallel 4
node tools/terrain-workers-smoke.mjs
node tools/terrain-smoke.mjs
node tools/terrain-smoke.mjs --stress-seconds 15
npm run smoke:web
git diff --check
```

The replacement worker proof target and its async test hooks are proposed until added. Record its exact
build command and validation results below. Use installed clang-format 22.1.3 for touched C++ files.

## Validation and Acceptance

Migration acceptance: the actual browser heap is shared; the actual shared C++ worker implementation
passes independent completion, identity, cancellation, exception, held release, shutdown/restart and
geometry-part-range checks while the browser event loop advances. No old worker module is requested.
Missing isolation produces a clear failure. Real WebGPU terrain reaches 16 m detail, survives distant
teleport, renders, responds to UI/resize/reload and drains resources after a moving route. Browser
console errors or unexpected validation warnings fail the smoke. Native worker tests and integration
groups remain passing. No measured speedup is claimed from migration alone.

For the future algorithm, add deterministic tests with small trees and controlled completion order:

| Scenario | Required observable assertion |
| --- | --- |
| 7/8 siblings ready, then eighth ready | Parent remains, then complete replacement with no overlap/hole |
| Arbitrary build/upload order, failures and retries | Every published cut has ready payloads and complete admitted coverage |
| Ready fine branch beside delayed coarse branch | Every displayed face differs by at most one level |
| Boundary refinement cascade, root edge/corner | Closure terminates and face rules hold across roots; corner-only contact is distinguished |
| Actual versus desired neighbour changes | Correct six-face masks; no geometry rebuild solely for masks |
| Hysteresis oscillation, fast motion, teleport | Stable thresholds, stale results ignored, eventual convergence at rest |
| Queue saturation and nearest/coarse priority | All-or-none sibling demand; eligible priority order, bounded in-flight reservations |
| Budget/metadata exhaustion | Valid fallback remains; no partial group, leak or infinite closure loop |
| Cancel/reset during generation or upload | Unique request identity, eventual released reservations, no stale publication |
| Snapshot replacement with old GPU work outstanding | Old payload survives readers/fences, then residency falls to zero |
| 131,072 m root, depth 13 | Finest 16 m bounds; sparse node counts scale with demand, not 8^13 |
| Distant negative positions and shared borders | Exact address identity and compatible shared samples within existing tolerances |
| Streaming shutdown with held/running work | No deadlock, use-after-free or worker access after owner destruction |

Use seeded randomized schedules in addition to focused regressions. The native C++ suite remains the
primary state-machine oracle; browser smoke proves real threads, shared heap and rendering. No new
numerical coverage percentage is invented: require each contract/failure row above to have an explicit
assertion and review untested branches. Record traversal nodes, elapsed time and worst reconciliation
latency, generation throughput, snapshot age and live CPU/GPU bytes on named hardware/backend/viewport.
Set numeric performance acceptance after baseline measurements, not from an unmeasured guess.

## Milestone Review

Migration review completed 2026-10-07. The existing concrete queue/mesher implementation is shared rather
than introducing another worker interface. The only platform lifetime branch detaches web threads, with
shared State retaining all data they touch; native joins are preserved. Worker code never owns renderer
objects. Proof verifies shutdown/restart drains all runtime pthreads. Cross-origin isolation is supplied by
both test servers and required explicitly by the application shell. Slang/RHI revisions remain unchanged;
the source preparation script fails on a different Slang checkout revision or failed build command.
Removed all five obsolete transport source files and their build/serve entries, plus stale generated worker
outputs. Updated the host/development/terrain contracts and build-web skill. C++ formatting (clang-format
22.1.3), JS syntax checks and git diff --check pass. Pre-existing renderer/profiling edits remain untouched.

## Idempotence and Recovery

Reuse configured build trees and pinned dependencies. Preserve unrelated uncommitted renderer work.
Re-run deterministic checks freely; store evidence under artifacts/terrain. Browser page closure owns
and terminates its worker environment. Do not leave a second fallback transport hiding missing isolation.

## Artifacts and Notes

Prior performance evidence is under artifacts/terrain/release-profile/generation-moving; it is baseline
context, not evidence for this migration. New commands/results will be appended here as executed.

Executed migration checks (2026-10-07):

- `tools/build-slang-web.ps1 -Parallel 8` with EMSDK=C:/tools/emsdk built host generators and shared-memory
  Slang libraries; log: artifacts/terrain/slang-threaded-build.log. The initial generator-output path was
  corrected to generators/Release/bin; the script now reproduces the successful build.
- `cmake --build --preset native-release --parallel 6` and `ctest --preset native-release --output-on-failure`:
  all 9 groups passed in 53.93 s; artifacts/terrain/cpp-threads/native-ctest.log.
- `cmake --preset web`, then `cmake --build build/web --target ofg-web ofg-compute-proof-web --parallel 4`:
  successful. Final incremental web build succeeds; logs under artifacts/terrain/cpp-threads.
- `node tools/terrain-workers-smoke.mjs`: shared heap, two independent worker IDs, valid regular/transition
  geometry, exact 64-bit request identity, cancellation, exceptions, release, restart, zero remaining live
  pthreads, responsive page and actionable missing-isolation diagnostic; no errors. workers.json records it.
- `node tools/terrain-smoke.mjs`: origin cut 1,529 / distant cut 1,419, depth 6, zero unresolved requests,
  zero jobs/failures at convergence. Actual application reports isolated=true, shared=true, workers=2.
  UI/resize/reload pass. Inspected origin, distant and UI captures; evidence under artifacts/terrain/cpp-threads.
- `node tools/compute-vertex-smoke.mjs`: left/right/left compute geometry passes on WebGPU.
- `npm run smoke:web`: scene textures, checkerboard, resize/reload, asset cancellation and missing-WebGPU
  handling pass in Chrome 154.0.8037.98. These are functional checks, not performance benchmarks.
- `node tools/terrain-smoke.mjs --stress-seconds 15`: 16 recorded route samples, zero failures/errors;
  sampled maximum tracked CPU terrain residency 154,304,768 bytes and GPU residency 147,449,636 bytes,
  within 256 MiB budgets. Final root/job/CPU/GPU/retired counts are all zero. Evidence: traversal.json,
  traversal.webm and inspected motion-86/93/99.png under artifacts/terrain/cpp-threads. The last frame is
  after the deliberate high-altitude unload teleport. Browser compute diagnostics identify Intel gen-12lp
  WebGPU; terrain viewport 1200x800, video 900x600. These are sampled residency checks, not total process memory.

Known build warnings remain explicit: Asyncify with WASM exceptions has upstream limitations, and pthreads
with growable memory can slow JavaScript heap access. The worker exception path is tested with Asyncify;
this does not establish every possible exception across a suspended graphics call. No warning is suppressed.

## Interfaces and Dependencies

Use the existing TerrainWorkers value-request/owned-result interface. C++ vectors move through shared
queues without serialization; synchronization still guards publication. Keep raw mutable tree pointers
off the render thread. Shared CPU memory does not eliminate GPU uploads or resource retirement.
Threading reference: https://emscripten.org/docs/porting/pthreads.html (consulted 2026-10-07).
