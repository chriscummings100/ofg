# Terrain streaming

The noise terrain prototype is selected with `--terrain` or `?demo=terrain`. Generation supplies immutable values;
`TerrainStream` owns demand, node groups, asynchronous identities and atomic display cuts on the application thread.
`TerrainLaboratory` owns worker transport, validation, bounded uploads and retaining render snapshots. The scene owns
one inspection camera rather than one entity per tile. Hydrology, editing, caves, foliage and variable sample counts
per node are future generation work; this prototype uses 32 sample intervals at every depth.

## Addressing and surface boundaries

Roots are 1024-metre cells addressed by three signed 64-bit integers. Integer octree coordinates identify descendants,
through depth six (16 metres). The seven cell widths are 1024, 512, 256, 128, 64, 32 and 16 metres.
Octree depth increases toward finer cells; conventional LOD numbers run the other way (LOD 6 to LOD 0). Positions combine cell identity with double local metres. Root subtraction happens
before float conversion. Rendering uses node-local vertices plus camera-relative translations; terrain and shadow
casters share these transforms. Atmosphere altitude remains separate from the rebased render origin.

Four deterministic noise octaves define a height surface. A conservative global height bound omits root layers that
cannot contain a surface; local per-octave extrema also certify empty nodes within that global range; every admitted nonempty root still uses a full 3D octree. Meshes stay inside all six node
faces. Transvoxel tables join different sample spacings on the fine side, including multiple simultaneous faces.
This version uses zero-width transition strips on the common face, without secondary-vertex displacement or skirts.
Geometric popping is expected. Neighbor depth differs by at most one. CPU tests independently weld shared positions
within 1e-5 of sample spacing and check edge incidence, including six faces and corner junctions.

## State and publication

A nonexistent child group is unloaded. Each node has one build slot: Pending, Loading, Loaded or Failed. Loading includes CPU
work and incremental GPU preparation. Visibility is membership in the published cut, not another mutable node flag.
A loaded node can be hidden by descendants, waiting for sibling preparation, or retained until retirement.
Empty output is a successful payload. Only a conservative emptiness certificate suppresses further subdivision.

| Event | Build state and observable result |
| --- | --- |
| Create root or complete child group | New node slots are Pending; the current cut is unchanged. |
| Reserve a missing sibling group | Pending slots become Loading with fresh request identities; dispatch follows reservation. |
| Accept validated CPU geometry | Remains Loading while bounded GPU preparation advances. |
| Accept prepared geometry or explicit empty result | Becomes Loaded; publication still waits for the complete compatible replacement. |
| Generation or upload fails | Becomes Failed; existing displayed coverage remains. Explicit retry returns the slot to Pending. |
| Demand becomes obsolete | Invalidate the request owner; keep its job charged until terminal acknowledgement and retain submitted data until completion. |
| Candidate is complete and still required | Atomically replace the display cut; build states remain Loaded. |
| Prune unused content | Remove complete unused groups; payload ownership transfers to retirement where necessary. |

Loaded and visible are therefore separate facts. An empty selected payload participates in coverage without issuing a
draw. A parent can remain Loaded throughout child generation, publication and later coarsening.

```cpp
// Handle each worker result once. CPU completion alone does not admit geometry.
if (stream.acceptGenerated(request.id, geometry.allocatedBytes()))
{
    uploads.push_back({request, payload});
}

// Advance this queue on subsequent frames, without accepting the CPU result again.
while (!uploads.empty() && uploadAllowance >= 4)
{
    auto& upload = uploads.front();
    const auto before = uploadAllowance;
    const bool ready = Graphics::prepareStreamingMesh(*upload.payload->mesh, uploadAllowance);
    if (before != uploadAllowance)
    {
        stream.submitted({{upload.request.address, upload.payload}}, completion.mark());
    }
    if (!ready) break;
    stream.complete(upload.request.id, upload.payload);
    uploads.pop_front();
}
```

The full adapter implementation handles cancellation, errors and partial-buffer retirement around this basic sequence.
Every request has an epoch and monotonically increasing sequence. Old results cannot overwrite a recreated node or a
retry. Cancellation invalidates the owner immediately but retains its reservation until a terminal acknowledgement.
Failure is terminal until explicit retry. Worker callbacks enqueue values and never access scene/node/GPU pointers.

```text
for a proposed split, merge, root admission or withdrawal:
    reconstruct a candidate from the current published cut
    balance coarse neighbors to at most a 2:1 spacing ratio
    require missing candidate nodes and their retained ancestors
    wait for each node's parent to become Loaded before dispatching the node
    reserve each missing sibling group together; build larger balanced replacements in bounded waves
    once all required nodes are Loaded, derive the active faces from the candidate topology
    allocate the next snapshot before changing admission flags
    swap nodes and their face masks together, then prune obsolete content
```

All eight siblings are created and reserved together; their parent is prepared before dispatch. After publication, one sibling may refine independently.
Merges proceed bottom-up and reuse retained parents. Every generated payload contains regular triangles followed by
all six transition faces in a shared vertex/index buffer. Eight offsets delimit seven contiguous ranges, including empty
ranges. Nonempty ranges map to existing Mesh submeshes; no shader or graphics wrapper is needed. The request identity
contains only the node address and epoch/sequence, never a neighbor mask. CPU/GPU readiness covers the entire payload.

The cut stores a six-bit face mask separately from payload ownership. A change of neighbors updates that mask at atomic
publication and reuses the same payload; it creates no mesh variant, generation job or upload. Color and shadow draws
select the same regular/active-face submeshes. All faces are generated even if currently unused; this deliberately trades
some upfront geometry/storage for simpler switching. An entirely empty result has seven empty ranges and no Mesh.
A newly admitted root still waits for balanced neighboring node coverage before appearing. Culling affects draw extraction only, never spatial coverage or demand.

Distance uses the observer-to-AABB metric: below two node widths requests children; above 2.5 widths requests the
parent. Equality and the intervening band preserve demand. Root demand is 4096 metres with 5120-metre retention.
Observer freeze separates demand from the moving render camera. Coarse coverage loads outward first, followed by
refinement in distance order. Runtime planning considers two candidates per update; a held or blocked candidate yields
to the next nearest candidate until demand, readiness, topology or capacity changes. Idle and blocked passes do no
planning work. Pending dependencies remain owned, but publication always reconstructs the candidate against the current
cut. Balancing visits the cut once and follows new children rather than repeatedly scanning the complete cut; unchanged
cut entries reuse their prepared payloads. The frame adapter reconciles twice, before results and after uploads.

Cell boundaries quantize the distance bands; they are approximately circular on a flat surface, not smooth circles.
The terrain-bands test checks 64 directions and twelve radii against an independent distance calculation, all seven
cell widths and parent-before-child dispatch. It exports the actual cut for `tools/terrain-band-map.py`. The default
camera starts eight metres above the local surface so the finest 16-metre cells can be inspected; R selects the high
overview and F returns near the surface.

Mesh sample count remains 32 intervals per node in this streaming proof: spacing ranges from 32 m to 0.5 m. A higher
vertex count per fine node is still future meshing work. Changing that count requires balancing by sample spacing and
supporting the resulting boundary ratios; the current Transvoxel path supports a 2:1 spacing change.

## Residency and renderer ownership

The runtime reserves up to 4 MiB for each of at most 32 outstanding payloads. CPU payloads have a 256 MiB budget;
another 128 MiB is an allowance for worker storage, scratch, transport and metadata. These counters are not process RSS
or exact allocator/driver residency. Each job's scratch allocator has a hard 16 MiB limit, and each browser worker heap
has a 48 MiB maximum. GPU payloads have a 256 MiB budget. Upload submission is limited to 4 MiB per frame
and 16 MiB of incomplete staging. Counts include retained ancestors, cancelled requests and retired payloads.
Driver allocation alignment and the preexisting renderer's lighting resources are outside the terrain payload ledger.

`Graphics::prepareStreamingMesh` creates immutable destination buffers and submits aligned chunks. Rendering rejects
partially prepared meshes. Prepared means uploads are ordered before draws on the same graphics queue; it does not
mean the CPU waited for the GPU. Terrain snapshots retain their meshes independently of topology.

The last upload or draw serial protects each payload. Native D3D12 uses a real queue fence. The pinned WebGPU RHI
fence signals submission, so the browser path uses `wgpuQueueOnSubmittedWorkDone` instead. Callback state outlives the
laboratory when needed and contains no terrain pointer. Retirement releases a payload only after real completion and
after external snapshots have dropped ownership. Native shutdown drains the queue before destroying terrain ownership;
browser command buffers retain submitted resources and worker termination prevents further application messages.

## Verification

Fast doctest cases exercise the state machine, boundaries, cancellation, retries, budgets and native workers. Separate
executables sweep allocation failures and verify settled updates allocate nothing. Exhaustive checks cover all 256
readiness subsets and 40320 child completion permutations. Generated schedules use 32 seeds and 5000 operations each,
checking independent finest-grid coverage plus ownership, balance and byte-accounting invariants after each update.
Eight isolated mutations demonstrate that the assertions detect the intended defects. Native GPU tests check actual
partial uploads, complete rendering, completion markers and culling separation. Browser smoke exercises real dedicated
workers and WebGPU; long native/browser routes check bounded payload residency and eventual drainage.

| Contract | Executable specification |
| --- | --- |
| Readiness, identity, atomic cuts, balance and accounting | [Controller tests](../tests/terrain-stream-test.cpp); S01-S40 scenario IDs map to the plan. |
| Allocation failure and allocation-free settled updates | [Allocation tests](../tests/terrain-allocation-test.cpp), with a separate container ABI. |
| Seven radial detail bands and parent-before-child dispatch | [Band tests](../tests/terrain-band-test.cpp); outward root/refinement priority is in the controller tests. |
| Noise samples, bounds, empty certificates and six-face/corner joins | [Generator tests](../tests/terrain-generator-test.cpp). |
| Native cancellation, held work and teardown | [Worker tests](../tests/terrain-workers-test.cpp). |
| Uploads, GPU completion, culling and long traversal | Terrain cases in [graphics tests](../tests/graphics-test.cpp). |
| Browser presentation and real worker isolation | [Terrain smoke](../tools/terrain-smoke.mjs) and [worker smoke](../tools/terrain-workers-smoke.mjs). |
| Sensitivity to the eight specified faults | [Mutation runner](../tools/terrain-mutation-check.py). |

Commands and evidence locations are in [DEVELOPING.md](../DEVELOPING.md). The [original completed plan](archived/terrain-streaming.md)
records the initial implementation checks; the repair plan below records subsequent checks. Documentation of a test is not a passing result.
The implementation is a terrain laboratory, and the explicit metadata/worker allowance is not a claim of exact total
process memory accounting. Shared-edge tests establish the defined geometric tolerance, not pixel identity across GPUs.

The [streaming repair plan](archived/terrain-streaming-repair.md) records the subsequent priority, LOD-band and frame-cost fixes.

The [completed prebuilt transition plan](archived/terrain-prebuilt-transitions.md) records the node-owned seven-part mesh simplification.
