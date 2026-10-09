# Terrain streaming

Run `--terrain` natively or `?demo=terrain` in the browser. `TerrainStreamingService` owns one coordinator
thread and two generation threads on both hosts. `TerrainStream` is the synchronous, GPU-independent
controller owned by that coordinator. `TerrainLaboratory` owns meshes, uploads, rendering and GPU retirement
on the application thread. The scene contains an inspection camera rather than an entity per terrain node.

The rule is **what do we need, and what do we have?** Parents remain loaded while their branches are needed,
including while their children are displayed. Completion adds available content; it never discards a parent.
The next selection is computed from desired detail and current readiness, independently of completion history.

## Addressing and distance

Laboratory roots are 131,072 m cubes addressed by three signed 64-bit integers. Integer local octree coordinates
identify descendants through depth 13, reaching 16 m cells. Widths halve across fourteen levels. The controller
supports depth 16; it allocates demanded branches, not a dense 8^13 grid. A non-owning address hash index serves
lookup, while roots and complete eight-child groups own nodes. Negative-root crossings use checked integer arithmetic.

Positions combine root identity with double local metres. Render transforms subtract the live camera before
conversion to float; snapshots never freeze a camera-relative transform. Shadow casters use the same transforms.
Atmosphere altitude remains independent of the rebased rendering origin.

Distance is observer-to-AABB distance. Split below two widths, coarsen above 2.5 widths, and preserve history at
equality/in between. Detailed demand is limited to 4,096 m. Root admission uses that radius; retention extends
one root width beyond it. Root coverage is whole-root coverage, so the admission radius is not a hard draw cutoff
inside a 131 km root. The camera far plane remains 16,000 m. Frustum/shadow culling filters draws, not ownership.
Observer freeze changes demand independently of the moving camera.

The height prototype still uses four noise octaves whose wavelengths scale with root width. Increasing root size
therefore makes this fixture locally flatter. This is not an island/hydrology generator or a promise of added fine
surface features. Meshing retains 32 intervals per node (4,096 m to 0.5 m sample spacing across these levels).
Independent fine-node tessellation, caves, editing and foliage remain generation work.

## State and complete selection

Absent metadata is unloaded. Each existing node is Pending, Loading, Loaded or Failed. Visibility is membership
in a published cut: a non-overlapping set of leaves covering each admitted root, including empty leaves.
Loading includes queued generation, CPU generation and renderer preparation. A successful empty mesh is ready
without GPU work; only a conservative certificate suppresses further distance subdivision.

| Event | Result |
| --- | --- |
| Create a child group | All eight Pending nodes are attached/indexed together, or none are. |
| Reserve an eligible group | All its Pending members become Loading with fresh epoch/sequence IDs. |
| Receive valid nonempty geometry | Transfer owned CPU vectors to the renderer; remain Loading. |
| Receive upload-ready acknowledgement | Become Loaded; recompute availability selection. |
| Complete children | Keep the parent Loaded; selection may descend when all eight children are ready. |
| Fail generation/upload | Become Failed until explicit retry; partial upload charges survive renderer retirement. |
| Lose demand | Detach obsolete request ownership; release charges only after the responsible owner acknowledges. |
| Change neighbours | Select prebuilt transition ranges; no regeneration or upload. |

A reconciliation has three spatial passes, followed by bounded scheduling/cleanup:

```text
1. Discover desired detail breadth first, marking the complete target and ancestor dependencies.
2. Balance desired face neighbours to at most one depth difference.
3. Starting at ready roots, descend only through complete ready child groups wanted by the target.
   Balance this available cut by selecting retained coarser ancestors where necessary.
   Derive transition masks and publish the complete immutable selection if it changed.
4. Cancel/prune content required by neither target nor selection; keep their ancestors.
   Reserve and submit eligible groups within job and memory limits.
```

Face lookup steps to the same-depth adjacent address and checks its ancestors in a leaf hash set. Every fine leaf
checks its coarse neighbours; this handles one-to-many faces without pairwise production scans. Target closure
refines coarse leaves where metadata fits. After a refusal, that closure only coarsens, so it terminates. Display
closure always coarsens to loaded ancestors, replacing all their selected descendants. Failed/held jobs leave valid
fallback coverage. A ready adjacent root can cause existing coverage to coarsen at its boundary.

Dispatch is coarse depth first, then distance to the sibling group's parent, then exact address. A parent must be
ready before dispatching its children. Reserve every Pending member of an eligible group together; skip groups that
do not fit without stopping demand traversal. Two workers may finish in any order. Failures await explicit retry.
Unchanged settled/blocked inputs perform no traversal. Metadata cleanup permits one additional complete retry;
otherwise capacity changes or new input wake blocked work.

## Prebuilt faces

Each payload contains its regular triangles and all six Transvoxel transition faces in one vertex/index buffer.
Eight offsets delimit seven ranges, including empty ones. The cut's six-bit mask activates fine-side faces toward
selected neighbours exactly one depth coarser. Colour and shadow passes select the same parts. Neighbour changes
never create another mesh variant. Face balance and compatible 2:1 boundary sample spacing are required; corner-only
contacts do not impose a face rule. Zero-width transition strips use the common face, without skirts or secondary
vertex displacement. Geometric LOD popping is expected. Generator tests weld positions within 1e-5 sample spacing.

## Thread and graphics ownership

The coordinator alone mutates nodes, requests and cuts. Meshers own scratch/output until moving terminal results
through a protected queue. The application owns every Mesh, Material, GPU handle and last-submission serial.
`ReadyContent` is immutable CPU metadata containing the request/payload ID, byte counts and empty flags. Cut entries
share these descriptors; retaining an old descriptor never retains a graphics object or permits redrawing an old cut.

Observer values and complete snapshots coalesce. Reset/retry/stop, upload results and retirement commands do not.
Short mutex-protected swaps transfer batches; locks never surround traversal, meshing or GPU preparation. A shared
notification sequence captured before draining queues prevents a lost wake-up. A new observer does not restart a
running pass. The renderer uses its previous complete selection without waiting and rebases it against the live camera.

```text
stream: publish revision R without payload P, then send Retire(P, R)
render: adopt the latest snapshot at the frame boundary
        retain P until applied revision >= R, P is absent from the active cut,
        and its last upload/draw submission has actually completed
render: drop P's record and acknowledge PayloadReleased(P)
stream: release P's budget charge exactly once
```

Skipping revision R is safe when the renderer adopts a later revision. Once retirement is requested, that ID cannot
reappear; renewed demand gets a fresh ID. A cancelled upload remains renderer-owned even if it never became visible.
Old epoch replies cannot update a recreated node. Native uses a real queue fence; WebGPU uses work-done callbacks
because the pinned RHI WebGPU fence reports submission rather than actual completion.

Native shutdown drains the graphics queue, then coordinates CPU cancellation and retirement acknowledgements before
joining. Browser shutdown retains a small retirement state and services it on the application event loop; detached
threads retain CPU state only. No callback accesses the destroyed laboratory. The app prewarms three pthread slots;
the focused lifecycle proof uses six to permit old/replacement overlap. Browser hosting requires COOP/COEP isolation.

## Residency

CPU and GPU payload budgets are each 256 MiB, including retained ancestors, outstanding reservations and retirement.
At most 32 requests reserve up to 4 MiB each. The controller caps metadata at 32,768 nodes. Each of two meshers caps
scratch at 16 MiB. Uploads submit at most 4 MiB per frame with at most 16 MiB incomplete staging. Shared-memory vectors
move between owners without wire serialization or cross-heap copies.

These are tracked payload budgets, not process RSS or exact driver residency: metadata, container overhead, staging,
allocator alignment and other renderer resources are separate. Insufficient memory preserves coarser coverage and
reports a block. GPU-ready means uploads precede draws on the same queue, not that the CPU waits for them.

## Verification

| Contract | Executable specification |
| --- | --- |
| Availability, retained parents, boundaries, identity and budgets | [Controller tests](../tests/terrain-stream-test.cpp), including 256 subsets and all 40,320 child completion permutations. |
| Strong group/publication allocation boundaries and idle allocation behavior | [Allocation tests](../tests/terrain-allocation-test.cpp). |
| Seven-level legacy and fourteen-level large-root radial bands | [Band tests](../tests/terrain-band-test.cpp), with independent distance expectations and exported cuts. |
| Real coordinator/meshers, held traversal, coalescing, reset, shutdown and release ownership | [Service tests](../tests/terrain-streaming-service-test.cpp). |
| Actual geometry, emptiness certificates and face/corner continuity | [Generator tests](../tests/terrain-generator-test.cpp). |
| Mesher cancellation and held work | [Worker tests](../tests/terrain-workers-test.cpp). |
| Uploads, GPU completion, 16 m convergence, culling and retirement | Terrain cases in [graphics tests](../tests/graphics-test.cpp). |
| Browser shared heap, coordinator restart/drainage and real WebGPU rendering | [Worker smoke](../tools/terrain-workers-smoke.mjs) and [terrain smoke](../tools/terrain-smoke.mjs). |
| Sensitivity to eight seeded controller defects | [Mutation runner](../tools/terrain-mutation-check.py). |

Randomized tests use 32 seeds and 5,000 operations each, saving replay traces. Small fixtures independently rasterize
coverage and compare integer AABBs for overlap, balance and masks. Sparse depth-13 fixtures use exact integer volumes.
Thread tests use explicit gates/notifications, not sleeps. Browser smoke also exercises UI, resize/reload, teleports
and a moving route with final zero tracked residency. See [DEVELOPING.md](../DEVELOPING.md) for commands and the
[sparse streaming execution plan](archived/sparse-terrain-streaming.md) for current validation evidence.

The [original plan](archived/terrain-streaming.md), [repair plan](archived/terrain-streaming-repair.md) and
[prebuilt transition plan](archived/terrain-prebuilt-transitions.md) preserve earlier implementation evidence.
