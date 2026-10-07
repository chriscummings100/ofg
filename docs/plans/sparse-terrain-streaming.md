# Simplify sparse terrain streaming and share C++ workers across hosts

This ExecPlan follows [PLANS.md](../../PLANS.md). The browser C++ threading migration is complete.
The streaming redesign below is a concrete proposal, refined at the user's request on 2026-10-07;
its implementation is separate work. Existing implementation and verification are preserved in
commit `eadecbf`, pushed to `origin/main`. Do not confuse the completed migration with the proposed tree redesign.

## Purpose / Big Picture

Support sparse, roughly 128 km islands with 16 m finest cells, while keeping streaming understandable
and rendering independent of traversal latency. Replace repeated per-operation replacement planning
with complete demand, boundary and display-selection passes. Give a single streaming thread ownership
of the mutable tree in a later milestone. The prerequisite shared-memory C++ generation threads now
work on web and native; the separate browser generator WASM and JavaScript copy transport are removed.

The organising rule is **what do we need, and what do we have?** Keep parents while their branches are
needed. A completed child adds available content; it does not evict its parent or initiate a procedural
replacement transaction. Derive the whole visible selection from current demand and current readiness.
Completion order affects time to detail, not the correctness of that decision. Logically the same query
can run every frame; the worker may reuse its previous answer when neither demand nor availability changed.

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
- [x] (2026-10-07) Reread this plan, reviewed and committed outstanding renderer/threading work, and pushed
  `eadecbf` plus the previous terrain commits to `origin/main` before refining the design.
- [x] (2026-10-07) Specified traversal, budget fallback, scheduling, render ownership, retirement messages,
  worker wake-up and deterministic acceptance fixtures. This is documentation, not implemented behavior.
- [x] (2026-10-07) Incorporated the user's clarification: parents remain resident for needed branches;
  selection is a fresh availability/demand query, independent of completion history.
- [ ] Future authorization: implement complete sparse demand and boundary traversals.
- [ ] Future authorization: implement balanced ready display selection and remove replacement plans.
- [ ] Future authorization: move tree ownership to a streaming thread and publish immutable selections.
- [ ] Future authorization: exercise depth 13 and configure large-island generation/render ranges.

## Surprises & Discoveries

Before the migration, the web implementation had two dedicated workers, each with an independent generator WASM
heap. It copied geometry out of that heap into transferable buffers, then into the application heap.
It did not use Emscripten pthreads. Both hosts now share two persistent std::threads with a bounded
queue, cancellation flags and condition variables. The existing address format supports depth 16.
Current planning copies and balances an entire candidate cut for each attempted local operation.
The laboratory still hardcodes 1,024 m roots and a 4,096 m admission radius; increasing tree depth
alone does not implement a large visible island. Renderer/profiling changes are included in `eadecbf`.

The current PreparedPayload contains a Mesh and mutable lastSubmission. Resource loading and the
Object/Ptr observer machinery are not a cross-thread ownership contract. Moving TerrainStream unchanged
to a worker would therefore be unsafe. The proposed worker exchanges CPU values and payload identities;
the render thread keeps meshes, material objects, upload state and submission serials.

The full pthread application link rejected the supplied Slang archive: --shared-memory is disallowed
because its objects lack atomics/bulk-memory features. The fix was to build the identical Slang v2026.17.1
source (ca6e0a657c52881166295f11c14d57afdf0de481) with -pthread, using native code generators.
This is now a build prerequisite, not a dependency upgrade. The initial smoke
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
- 2026-10-07: Use the existing RequestId as payload identity, avoiding another ID allocator. Snapshots contain
  addresses/IDs/masks; mutable graphics resources remain exclusively on the render thread.
- 2026-10-07: First implement complete passes synchronously in the testable controller, then change its owner
  to a thread. Separate algorithm failures from synchronization failures; do not retain two planner implementations.
- 2026-10-07: Boundary repair refines coarse target leaves when metadata permits. Once that pass hits its
  metadata limit, repair only by coarsening. Actual display repair always coarsens to loaded ancestors.
  This supplies a terminating, explicit fallback instead of an incomplete closure or repeated retry loop.
- 2026-10-07: Child completion never retires a parent. Keep ancestor payloads for the lifetime of a needed
  branch. No waiting-for-sibling transaction or completion-order state belongs in the selector: it reads
  current readiness. Out-of-order tests prove this invariant rather than justify special-case machinery.

## Outcomes & Retrospective

Design saved and shared C++ threading migration complete. Native Release and browser functional checks,
moving-route drainage and final review pass. No new streaming algorithm, larger render
range or traversal performance improvement is claimed. The primary additional build cost is preparing
threaded Slang from the same pinned source version once; incremental application builds reuse it.
The follow-up plan refinement defines concrete data ownership, algorithms and executable acceptance cases.
No new runtime behavior or performance result is claimed by that documentation revision.

## Contract and Quality Baseline

The tree is sparse and dyadic. Creating children creates all eight metadata nodes or none, within the
node budget. Geometry resolution is independent of cell width; fine levels may have more samples or
additional features. Certified empty regions need no mesh but count as ready coverage.

Each admitted root has complete selected coverage with no selected ancestor/descendant overlap.
Face-adjacent selected leaves differ by at most one level. Transition faces are generated with each
payload and selected from the actual displayed topology; neighbour changes never rebuild geometry.
Every selected payload is upload-ready. A parent remains resident after its children become ready and
while descendants are displayed, for as long as its branch is needed. Showing children changes selection,
not parent residency. Failures and memory pressure retain valid fallback coverage rather than opening holes.
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
| Matching CPU result | Remains Loading while geometry is validated and GPU preparation completes; valid empty results become Loaded without upload |
| Matching upload-ready result | Becomes Loaded; display membership is computed separately for complete compatible coverage |
| Matching generation/upload failure | Becomes Failed with diagnostic; release finished worker ownership, but keep charges for partial uploads until renderer release |
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
`terrain-workers.cpp` implements the two persistent C++ generation threads on both hosts. The old native/web
split, separate generator executable, wire header and JavaScript transport have been removed.
`src/lab/terrain-laboratory.cpp` currently schedules, uploads and renders the stream on the application thread.

Build prerequisites and exact environment setup are in DEVELOPING.md and the build-native/build-web
skills. At migration start Slang RHI is pinned to 16324a68af477baaede620e713644f5e9613b1a2,
ImGui to b48d1afbe8ee8b238e2961dc363a949dd7304e23, and the installed Emscripten SDK is documented
as 6.0.0. Verify actual toolchain/build compatibility rather than updating dependency pins.

## Plan of Work

### Milestone 1: Shared-memory browser generation (complete)

The work described in this milestone is implemented and verified; evidence appears below.

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

Keep TerrainStream synchronous and GPU-independent during this milestone. A call reconciles a finite
set of inputs completely. Tests supply results explicitly; the laboratory still calls it on the main
thread until milestone 4. Replace per-operation candidate planning with one desired cut and one ready cut.

#### Concrete state and addressing

Use existing NodeAddress: signed 64-bit root coordinates, integer local coordinates and depth. Depth zero
is coarsest; increasing depth means finer detail. Width is rootWidth / 2^depth. A cut is a set of leaves
covering whole admitted roots, including empty leaves. It is not a list filtered by camera visibility.

The following is a proposed shape, not a second implementation to keep alongside the existing controller:

```cpp
// CPU-only metadata; the renderer owns resources identified by id.
struct ReadyContent
{
    RequestId id;
    size_t cpuBytes = 0;
    size_t gpuBytes = 0;
    bool empty = false;
    bool certifiedEmpty = false;
};

// Tree links are owned by one controller; all eight children exist together or none do.
struct Node
{
    NodeAddress address;
    Node* parent = nullptr;
    std::array<std::unique_ptr<Node>, 8> children;
    BuildState state = BuildState::Pending;
    std::optional<RequestId> request;
    std::optional<ReadyContent> content;
    bool distanceSplit = false; // Hysteresis history, independent of boundary-forced splits.
    bool needed = false;        // Replaced only after the complete target is known.
    std::string error;
};
```

Root owners and child unique_ptrs own nodes. An unordered_map<NodeAddress, Node*, AddressHash> is a
non-owning index. Direct child pointers serve traversal; the index serves lookup and result routing.
Maintain node count as groups enter/leave. Creating a group allocates and registers all eight before
attaching it; allocation failure leaves no partial group or dangling index entry. Deletion removes
index entries before owners. No node pointer crosses the future streaming thread boundary.

Provide find(address) and ensureChildren(parent); neither submits jobs. A convenience ensureNode(address)
walks the root-to-node path and ensures complete sibling groups at each missing level. It may leave
valid ancestor groups if a later group hits the cap, but never creates a lone child. Do not call it from
read-only neighbour queries. Root/local integer arithmetic must check INT64 boundaries before crossing roots.

#### Pass A: distance demand

Take one observer/settings value for the whole pass. Traverse roots and children breadth first, sorting
each level by distance then exact address for deterministic allocation under a metadata cap. Distance
is the shortest Euclidean distance from the observer to the node AABB, in metres. Initially retain the
current thresholds: split below 2 * width; retain a previous distance split until distance exceeds
2.5 * width. At equality retain the existing decision; an initially unsplit node does not split at equality.
DistanceSplit records only this test, not boundary closure. These constants are starting behavior to
measure, not a proof that the resulting topology is balanced.

```text
desired = empty leaf set
queue = wanted roots, coarse to fine and near to far within a level
while queue not empty:
    node = pop(queue)
    split = below maximum depth AND inside refinement range
            AND not certified empty AND hysteresisRequestsSplit(node)
    if split AND ensureChildren(node) succeeds:
        enqueue all eight children
    else:
        desired.insert(node.address)
```

The refinement range limits detailed branches, not coverage within a root. Outside it, retain a coarse
leaf. A metadata refusal is recorded as budget-blocked, not as distance hysteresis history. Continue
visiting every queued branch; a full generation queue does not affect this pass. Root admission uses a
finite distance-ordered root set bounded by the metadata budget; roots that cannot yet be represented remain unadmitted.
Root enumeration is an AABB-distance sphere test, not a hardcoded -4..4 loop.

#### Pass B: target boundary closure

Retain the current six-face integer neighbour method: step to the adjacent address at the same depth,
then search that address and its ancestors in the leaf hash set. This returns an equal/coarser neighbour
or none. It need not enumerate finer neighbours: each finer leaf finds the coarse leaf in its own visit.
Run the check from every leaf. Edge/corner-only contacts do not count as face neighbours. Missing roots
are outside the admitted domain, not implicit ready terrain.

Start with complete scans, collecting violations in batches. This is easier to audit than maintaining
a dependency queue and avoids copying a cut for each attempted split. Reuse vectors/hash capacity.
Every scan costs O(6 * leaves * maximumDepth) expected hash work, not a pairwise leaf comparison.
Measure the number of closure rounds; incremental affected-face indexing is deferred until justified.

```text
coarsenOnly = false
loop:
    violations = all (fine, coarse) face pairs with fine.depth > coarse.depth + 1
    if violations empty: break
    if not coarsenOnly:
        for each distinct coarse leaf in stable depth/distance/address order:
            if ensureChildren(coarse) fails:
                coarsenOnly = true
                break
            replace coarse in desired with all eight children
        if not coarsenOnly: continue
        recompute violations after the successful splits
    for each violation still present:
        ancestor = fine ancestor at depth coarse.depth + 1
        replace ALL desired descendants of ancestor with ancestor
```

Once coarsenOnly is set it stays set for this pass; never alternate refinement and coarsening in the
same closure. Refinement consumes a finite number of tree groups; coarsening strictly reduces leaf
count. Therefore the repair terminates. Skip obsolete violation entries and rescan after each batch.
Boundary-forced splits may subdivide certified-empty nodes; their children still get ordinary cheap
empty builds. Inferring ready children directly from an emptiness certificate is not required here.

Only after closure, clear old needed flags and mark every final desired leaf and its ancestors. A needed
split has all eight children needed, even if some are empty. Retain additionally every ancestor of the
currently selected cut until its replacement is published. Remove unused metadata groups after selection;
pending job records live by ID independently of their old node. A reconciliation may perform one additional
complete demand pass after pruning previously retained metadata. Do not repeatedly retry because closure
created then discarded its own groups: that could split/coarsen forever at the cap. After this one cleanup
retry, wait for an external input or capacity-release event before reconsidering blocked topology. With
sufficient space for the final target, discarding obsolete groups allows convergence without a camera move.

#### Submission is a separate bounded step

Treat a root as a group of one and a refinement as a group of eight. An eligible child group has a Loaded
parent and is required by the final desired tree. Pending members need jobs; Loaded/Loading members do
not restart. Failed members wait for explicit retry. A group containing an unretried Failed member does
not start further work just to wait forever. Preserve already completed siblings for that retry.

Sort eligible groups by child depth ascending, distance to parent AABB ascending, then parent address;
sort requests within a group by child distance/address. This is explicitly **coarse before fine, nearest
within a level**, not strict global distance order. Generation starts only after its parent's payload is
ready, including upload readiness. Queue order describes dispatch; two workers may complete out of order.
Root groups use depth zero and their own bounds/address for that ordering.

Atomically reserve all currently Pending members of a group before dispatch. Account worst-case CPU/GPU
payload bounds, existing ancestors, upload staging and pending retirement; release unused reservation
when actual size is known. A new group requires eight request slots (root: one), so validate settings
can admit a group or report insufficient configuration. If a group does not fit current capacity, try
other eligible groups; retain fallback, report the block, and retry on a capacity change. There is no
promise of full refinement under insufficient memory. Reserve cancellation records until acknowledgement.
Handle submission failure by cancelling accepted IDs and releasing unsubmitted reservations exactly once.

Visible result and proof: the current 1,024 m laboratory keeps complete coverage and distance bands,
with deterministic coarse/near dispatch. Tests must cover full queues, metadata refusal during closure,
allocation failure during group creation and eventual convergence after capacity returns.

### Milestone 3: Ready display selection (proposal)

Run selection after demand changes, relevant readiness/failure events or root admission/withdrawal.
Keep every loaded ancestor of the desired/selected branches available as fallback. A loaded root may
enter coverage; an unready root is not admitted. Adjacent new roots participate in the same balancing
pass, which can temporarily coarsen existing coverage. Root withdrawal deliberately changes the domain.

Treat this pass as select(desiredTree, availablePayloads). It does not consult completion history or
incrementally patch last frame's selection. Completion handlers only update a node's availability and
accounting. Whether children finished 0..7 or 7..0 makes no difference for the same available set. Parent
retention means coarsening needs no rebuild and delayed children need no rollback. Boundary balancing
remains a property of the chosen spatial cut; it is not a procedure for repairing completion order.

```text
queue = admitted roots with loaded coverage
while queue has nodes:
    node = pop_front(queue)
    if final desired tree splits node AND all eight immediate children are Loaded:
        append all children to queue
    else:
        select node

while selected face neighbours differ by more than one level:
    collect violating pairs using the same six-face hash lookup
    for each still-valid (fine, coarse) pair:
        ancestor = fine ancestor at depth coarse.depth + 1
        assert ancestor is Loaded
        replace ALL selected descendants of ancestor with ancestor

for each selected leaf:
    enable prebuilt transition faces toward selected neighbours one level coarser
sort selection by address
publish only if addresses, payload IDs, transition masks or admitted domain changed
```

An ancestor is loaded because descent required readiness at every level and those ancestors were retained.
Coarsening removes complete covered subtrees, so coverage cannot develop holes. It strictly reduces the
number of leaves, so correction terminates. Empty leaves obey the same coverage/balance rules even though
they submit no draw. A mesh with no triangles is not necessarily certified empty at finer resolutions.

Example: target A reaches depth 3 beside B at depth 2, but B only has depth-1 coverage available. The
availability query selects A's retained depth-2 ancestor. With B's full child group available, the same
query can select A at depth 3. No completion callback performs that switch. A parent plus seven children
is never a valid selection. All six transition flags
are derived after this repair; changing a flag must submit zero new generation requests by itself.

Prune/cancel only after desired and selected coverage are complete. Keep content needed by either tree
and loaded ancestors. A retained payload may be hidden because descendants are selected, siblings are
unready, or retirement is pending. These are derived reasons, not three more lifecycle states.

Remove Operation/Plan, makePlan, attemptedOperations, pendingRequirements and maximumPlansPerUpdate
when the replacement is proven. Do not retain a selectable old planner. Visible result and proof:
every published cut passes coverage/readiness/balance/mask checks under deliberately reordered uploads;
settled coverage converges to the desired cut when content, metadata and memory are sufficient.

### Milestone 4: Single-owner streaming worker (proposal)

Add a concrete TerrainStreamingService in proposed src/terrain/terrain-streaming-service.{h,cpp}, owning
TerrainStream and TerrainWorkers. It runs one persistent coordinator thread alongside the two generation
threads. The controller remains synchronously testable. Do not put locks in Node or introduce a task graph.

| Owner | Mutable state and responsibility |
| --- | --- |
| Streaming thread | Tree/index, observer snapshot, desired/ready cuts, request ledger, budget reservations, geometry validation |
| Two generation threads | Running TerrainMesher and its temporary/output vectors; publish moved WorkerResult values |
| Application/render thread | Mesh/Material/Ptr objects, upload preparation, GPU handles, active render snapshot, submission serials and retirement |

#### Value messages and snapshots

Move the current CPU geometry validation out of TerrainLaboratory::receiveResults into the service.
Keep Mesh::create, material assignment and Graphics::prepareStreamingMesh in TerrainLaboratory. Split
PreparedPayload accordingly; remove resources/mesh.h from the CPU stream interface. Use RequestId as
payload ID throughout. The node and an outstanding request ledger are separate: pruning a node cannot
forget an upload, cancelled worker or its reserved bytes.

```cpp
// Immutable value entry; no graphics object or node pointer crosses the boundary.
struct SelectedLeaf
{
    NodeAddress address;
    RequestId payload;
    uint8_t transitionFaces = 0;
    bool empty = false;
};

// One complete coverage decision. Revision increases monotonically across resets.
struct RenderSnapshot
{
    uint64_t revision = 0;
    std::vector<SelectedLeaf> leaves;
};
```

RenderPayload is a renderer-owned map entry keyed by RequestId, holding Mesh, submesh ranges, measured
CPU/GPU bytes, last upload/draw submission and a retirement flag. No other thread reads or destroys it.
The request ledger records whether ownership is still with generation or transferred to the renderer,
whether cancellation was requested, and the remaining charge. This is internal ownership bookkeeping,
not extra node states. Account old epoch records until their resources are released.

| Direction | Message and contract |
| --- | --- |
| App -> streaming | Latest observer/settings value, replacing an older unconsumed value |
| App -> streaming | Ordered reset/retry/stop commands and UploadReady, UploadFailed, PayloadReleased acknowledgements; never drop these |
| Generation -> streaming | Exactly one terminal WorkerResult per submitted ID; geometry ownership moves with the value |
| Streaming -> app | Upload(id, moved geometry), CancelUpload(id), Retire(id, afterRevision); ordered durable commands |
| Streaming -> app | Latest immutable RenderSnapshot; intermediate snapshots may be skipped |

Control/reset ordering matters: drain control commands before applying the newest observer value;
tag relevant values with the current epoch so an old observer/result cannot undo a reset. Nonempty CPU
results remain Loading until UploadReady. Empty valid results can become Loaded on the stream thread
without a renderer record. Do not conflate empty with a certificate that all finer content is empty.
Install the renderer record before sending UploadReady, so every published nonempty ID is resolvable.
Publish snapshots as shared_ptr<const RenderSnapshot>; shared ownership applies only to immutable CPU
values. It does not make Mesh, Material or the mutable node graph safe for concurrent access.

Use short mutex-protected inbox/outbox exchanges, moving vectors/values out before any traversal,
generation or upload. The outbox exchanges its command batch and latest snapshot together under one
lock. Only observer values and snapshots coalesce. Data-bearing queues are bounded by request/memory
reservations; coalescing a snapshot must never drop a cancellation or release obligation.

#### Retirement with skipped snapshots

Publish revision R without a payload before emitting Retire(id, R). Once retired, that ID cannot appear
in any newer snapshot; renewed demand uses a new ID. At each frame boundary the renderer drains its
batch, registers upload/cancel commands, adopts the newest snapshot, then processes retirement requests.
An upload still needing frame-budgeted work stays in its renderer queue. The renderer may free a record
only after all three conditions hold: appliedRevision >= R, the active snapshot does not reference it,
and its last upload/draw submission has actually completed. Keep retire requests pending until then.
Send PayloadReleased once, after dropping the record; only then release its stream-side byte charge.
Snapshot adoption occurs before constructing this frame's draw lists and after submitting/discarding the
previous frame's commands. Holding an old CPU snapshot for diagnostics keeps its values, not GPU resources.
Only the active snapshot may produce new draws; an old snapshot cannot be rendered again after adoption.

Example: snapshot 10 uses P, snapshot 11 removes P, snapshot 12 is the first one the renderer receives.
Retire(P, 11) is still valid after adopting 12, but P survives until the fence for its last draw in 10.
A cancellation of a never-selected upload needs no new coverage revision, but still waits for any
submitted upload commands. Empty CPU-only payloads have no GPU retirement obligation.

If cancellation races UploadReady, ignore readiness for the detached ID and continue renderer retirement;
do not release its budget twice. After transferring geometry in Upload, the renderer owns the terminal
release acknowledgement even if cancellation arrives before upload begins. Before that transfer,
WorkerResult is the terminal worker acknowledgement. Stale results cannot modify a replacement node.
Upload failure reports its diagnostic immediately but retains partial GPU allocations/charge until
PayloadReleased. This preserves the existing failure semantics without shared mutable resource objects.

#### Reconciliation and wake-up

Finish each finite reconciliation; do not restart whenever a newer camera position arrives. Demand
passes run only for changed observer/settings/root inputs or newly available metadata/empty certificates.
Readiness changes rerun ready selection and scheduling without repeating unchanged distance work.
Capacity release reruns blocked work. Settled, unchanged input must cause no traversal or publication.

Use one small shared StreamingWake object containing a mutex, condition_variable and sequence counter.
Both inbox producers and TerrainWorkers signal it after publishing an event and releasing their own
queue lock. The service records the wake sequence BEFORE draining inputs/results; after reconciliation
it waits only while that sequence is unchanged. This prevents the event-between-drain-and-wait race.
The signal owns no service pointer; shared lifetime makes late worker notification safe. No callbacks
into the controller, nested queue locks, frame polling or timed sleeps are needed. Existing worker tests
may omit the extra service notification and keep waitForResult.

```text
while not stopping:
    observedWake = read wake sequence
    drain commands, latest observer, generation results and upload/release replies
    if demand inputs changed: compute complete demand and target closure
    if demand/readiness/domain changed: compute complete ready selection
    publish changed snapshot and schedule retirements
    prune unused groups; schedule eligible requests within budgets
    if pruning freed old metadata AND cleanup retry not yet used for this input batch:
        mark cleanup retry used; mark demand dirty; continue
    wait until wake sequence differs from observedWake
```

Renderer use is nonblocking apart from short queue swaps: draw the previous snapshot if none is newer.
Construct camera-relative transforms from world addresses against the live camera every frame. An old
observer affects detail choice, not camera motion. Frustum/shadow culling operates on this coverage;
it does not feed camera-visible fragments back into tree ownership.

#### Shutdown and browser pool

Stop accepts no new requests, publishes empty coverage, cancels generation/uploads, and releases all
CPU-only state after terminal worker acknowledgements. The renderer continues draining messages and
retiring resources until their actual GPU completion; it must not block waiting on a service which is
waiting for its acknowledgements. Native teardown drains that protocol before joining. Browser teardown
is asynchronous; retained CPU service state outlives its detached coordinator/generators, and the app
retains renderer retirement state until drainage. No detached thread retains a laboratory or GPU pointer.
Closing the page terminates its worker environment; laboratory restart must drain without relying on closure.

The web app needs three prewarmed pthread slots (coordinator + two generators), replacing today's two.
A lifecycle proof permitting old/new overlap needs six; retain the existing generator-only proof too.
Prove startup, held-work cancellation, reset/restart, zero live runtime threads after shutdown and a
responsive browser frame loop. Do not change pool size or claim off-thread traversal in milestone 2.

Visible result and proof: camera/UI updates continue while a test gate holds a whole streaming pass;
releasing it yields one valid snapshot, not partial coverage. Native synchronous and threaded executions
of the same scripted inputs converge to identical normalized cuts and resource counts.

### Milestone 5: Large-root proof (proposal)

Configure root size/depth rather than retaining laboratory 1,024 m literals. Exercise 131,072 m roots
with 16 m leaves, including crossing roots and distant negative addresses. Independently choose render
distance, camera clipping/shadow range and coarse generator sampling. An island height/hydrology
representation is future generation work; sparse streaming does not itself provide that content.

The exact level widths are 131072, 65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16 m.
Feed rootWidth through addressing, generator, laboratory roots, uploads, debug bounds and render transforms;
do not change only maximumDepth. Validate settings before starting threads. Root distance determines
which whole roots enter coverage; it is not a hard pixel cutoff inside a 131 km root. The far plane and
render culling determine what is drawn. Record those independently in the UI and performance evidence.

Retaining ancestor meshes creates a real memory floor. First prove 13-level behavior with deterministic
small payloads, then measure real generator reservations/ancestors/transition faces at the intended sample
resolution. A 256 MiB budget is not assumed sufficient. Insufficient capacity must leave valid coarser
coverage with an explicit diagnostic. Increasing interior tessellation or adding hydrology is outside this
streaming change. Preserve compatible boundary sampling and prebuilt six-face payloads.

Visible result and proof: a top-down LOD map spans the configured levels near the observer, retains coarse
distant coverage, and shows grid-shaped approximations to radial bands. Perspective screenshots alone do
not establish metre scale or distance ordering. Run origin, negative-root crossing and distant teleport
fixtures with labelled cell widths, observer position, range and settled resource counters.

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

The worker proof is implemented as ofg-terrain-workers-proof and built by the ofg-web dependency.
For future CPU-only controller milestones use the existing cpu-tests configure/build/test presets;
no GPU device is needed. Add the proposed service tests to the existing ofg-terrain-test target and
its terrain-fast/terrain-randomized suites before claiming those tests run. Use installed clang-format
22.1.3 for touched C++ files. The following additional commands are existing interfaces, but have not
been rerun for this documentation-only refinement:

```text
cmake --preset cpu-tests
cmake --build --preset cpu-tests --parallel 6
ctest --preset cpu-tests --output-on-failure
ctest --preset native-release -L terrain --output-on-failure
```

Implementation order within milestones 2-4: establish CPU-only payload identity/messages first; replace
planning with synchronous passes and keep tests passing; remove old planner state; then add the service
thread and renderer handoff. Update docs/terrain-streaming.md, docs/architecture.md and DEVELOPING.md at
each implemented boundary. They must continue describing current behavior until that boundary ships.

## Validation and Acceptance

Migration acceptance: the actual browser heap is shared; the actual shared C++ worker implementation
passes independent completion, identity, cancellation, exception, held release, shutdown/restart and
geometry-part-range checks while the browser event loop advances. No old worker module is requested.
Missing isolation produces a clear failure. Real WebGPU terrain reaches 16 m detail, survives distant
teleport, renders, responds to UI/resize/reload and drains resources after a moving route. Browser
console errors or unexpected validation warnings fail the smoke. Native worker tests and integration
groups remain passing. No measured speedup is claimed from migration alone.

For the future algorithm, the following are mandatory test cases, not evidence that they currently pass.
Extend tests/terrain-stream-test.cpp and tests/terrain-band-test.cpp for synchronous behavior; add proposed
tests/terrain-streaming-service-test.cpp for ownership/wake-up, registered with the existing terrain test
target. Extend terrain integration cases in tests/graphics-test.cpp for renderer fence behavior,
and tools/terrain-smoke.mjs for actual browser diagnostics.

Every published cut in state-machine fixtures must pass an independent oracle: rasterize selected AABBs
onto a finest-depth integer grid for roots of depth at most 3 and require each admitted voxel covered
exactly once, none outside the admitted domain; compare adjacent occupied voxels for face balance; compute
expected face masks from those adjacencies; require each selected ID to be ready and current. Empty leaves
still occupy voxels. This oracle must not call the production neighbour/closure helpers. At depth 13,
use exact integer volumes, ancestor exclusion and independent pairwise AABB face tests on a small sparse
fixture rather than allocate 8^13 voxels.

| Case | Exact stimulus and required assertion |
| --- | --- |
| S1: complete siblings | All 40,320 readiness permutations for eight children: parent is selected for prefixes 0..7; prefix 8 selects all eight. Parent remains Loaded with the same ID afterward; selecting it again requires zero builds. Equal available sets yield identical selections regardless of completion history. Check every publication with the oracle. |
| S2: upload is part of Loading | Supply eight CPU results but only seven upload acknowledgements; no split. Acknowledge eighth and require split. Repeat with one valid empty result needing no upload. |
| S3: failure/retry | Fail one sibling during generation, then during upload. Parent stays, other completed IDs remain; explicit retry gets a new ID and allows convergence. Partial upload stays charged until release. |
| S4: actual balance | Force desired depth-3 leaves against depth-2 leaves; hold a neighbour at depth 1. Require loaded depth-2 fallback on the fine side, then convergence when the held group is ready. |
| S5: recursive coarsening | Coarsen a parent whose children include refined grandchildren. All selected descendants disappear in the same revision; test a second boundary violation caused by that replacement. |
| S6: root boundaries | Admit an unready adjacent root, then make it ready beside fine terrain; no admission until ready, and balance holds on first admission. Withdraw it and check the reduced domain. Repeat negative axes and edge/corner-only contacts. |
| S7: masks without rebuild | Exercise each of six faces and combinations at edges/corners; compare oracle masks. Change only neighbour selection with payloads already ready; generation count and existing IDs stay unchanged. |
| S8: demand hysteresis | Visit 2w-epsilon, 2w, 2w+epsilon and 2.5w-epsilon/equality/+epsilon with both prior states; epsilon=1e-6*w. Demand is stable at equality and within the hysteresis band. |
| S9: complete demand | Fill all worker slots, move observer, then reconcile. Required groups across the entire domain are updated despite zero new submissions; no unvisited branch is retired. |
| S10: priority | Insert equidistant roots in reverse order; dispatch follows depth, AABB distance, address. No child dispatch precedes parent readiness. Completion order cannot change the final cut. |
| S11: reservations | Leave seven request slots free for an eight-member group: dispatch zero. Free the eighth: dispatch eight. Repeat with byte capacity one byte below and exactly equal to the group's required reservation, independently for CPU/GPU budgets. |
| S12: metadata cap | Provide capacity for seven of eight new nodes and inject allocation/index failure partway through creation: no partial group, invalid index or lost existing cut. During closure, exhaust capacity after an earlier successful split; coarsen-only repair terminates and preserves the oracle. Repeated idle updates must not recreate/prune those groups or resubmit work. |
| S13: capacity recovery | Hold obsolete payloads behind a fence, blocking refinement. No repeated dispatch/publication at rest. Release them; refinement resumes and reaches the target without another camera move. |
| S14: certificate semantics | An ordinary empty coarse mesh may refine to nonempty children; certified empty suppresses distance refinement but can split for balancing. Empty leaves still participate in coverage/mask checks. |
| S15: identity and cancellation | Cross cancellation/reset with queued, held, running, CPU-result-pending, upload-queued, upload-submitted and ready phases. Deliver old replies after a replacement ID; it is never overwritten, all charges release exactly once. Include duplicate stale acknowledgements. |
| S16: reset domain | Reset while an old cut is displayed. Revisions remain monotonic, IDs change epoch, no mixed-epoch cut is published; old resources survive until a replacing/empty revision and real GPU completion. |
| T1: skipped snapshots | Render revision 10 containing P; skip 11 removing P; adopt 12 and receive Retire(P,11). P survives outstanding submission, then releases once. Test retire command arriving in a later drain than snapshot. |
| T2: slow reader | Hold a CPU snapshot and stall service traversal using explicit gates. Renderer draws its previous valid revision and rebases against changing camera; adopting the new cut cannot expose Node pointers or mutate the held value. |
| T3: wake race | Gate a producer before drain, during reconciliation and immediately before wait. Each event is consumed without a second event/timer. Unchanged input sleeps; newest coalesced observer eventually wins. |
| T4: release ownership | Cancel before and after geometry transfers to renderer; race UploadReady/UploadFailed with reset. Exactly the current owner issues terminal resource release and the budget returns to zero. |
| T5: stop/restart | Hold both meshers, stop during traversal/upload, then restart. Gates are released by cancellation; native joins and browser drainage finish, old IDs do not publish, no GPU object is destroyed off-thread. |
| L1: large root | Root 131072 m, depth 13: verify every listed width and finest 16 m bounds; small sparse target respects node cap and converges with sufficient fixture memory. No work scales with the full 8^13 lattice. |
| L2: negative/limit addressing | Cross each negative root face and distant exact integer roots; INT64 limits reject overflow. Shared-border geometry keeps existing generator tolerances; compare origin-equivalent normalized topology. |
| L3: radial bands | Use centred and off-centre observers in a labelled top-down map. Check distance-rule desired splits independently, final balance and roughly radial coverage; grid quantization and hysteresis preclude an exact circle assertion. |

S1-S16 are required before removing the old planner. T1-T5 are required before off-thread publication is
enabled. L1-L3 are required before changing the laboratory root configuration. Allocation checks belong
in the existing ofg-terrain-allocation-test executable. Real fence completion belongs in GPU integration
checks; a fake monotonic serial proves the protocol alone, not backend completion.

Threaded tests use condition variables/latches and explicit handshakes, never sleeps to infer completion.
A timeout is a failure guard only. Ensure TerrainWorkers' service path publishes exactly one terminal
result per submitted ID; its current cancel-of-unknown path can synthesize a Cancelled result, so the
service must not mistake that for a second resource release. Verify or tighten that behavior during
integration rather than assuming the old transport contract already has the new semantics.

Add seeded randomized schedules (at least seeds 1..32, 1,000 bounded input/result steps each) covering
movement, completion, failure, cancellation and budget pressure; run the oracle after every publication.
On failure print seed and replayable event list. Finally hold inputs fixed, remove injected failures,
allow sufficient capacity, drain all work and require convergence; then withdraw everything and require
zero jobs/charges/render records. This distinguishes safety under pressure from liveness after it ends.

The native C++ suite remains the primary state-machine oracle; browser smoke proves real threads,
shared heap and rendering. No new
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
22.1.3), JS syntax checks and git diff --check pass. Renderer/profiling changes were preserved and
subsequently committed together with the migration in `eadecbf`.

Plan refinement review (2026-10-07): retained one owner per mutable domain, replaced ambiguous budget
closure with a terminating algorithm, specified skipped-snapshot/fence retirement, and separated target
and readiness balance. Runtime implementation remains proposed. The next implementation must delete
old plan scaffolding as it replaces it; these new tests are requirements, not claimed passing tests.
Documentation checks passed: Markdown code fences pair, local links and named existing implementation/test
paths resolve, and git diff --check passes. No runtime tests were rerun for this documentation-only change.

## Idempotence and Recovery

Reuse configured build trees and pinned dependencies. The starting renderer/threading state is backed
up in pushed commit `eadecbf`; preserve any subsequent unrelated user changes.
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
