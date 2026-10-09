# Progressive erosion experiments

FastScape is an optional dependency of the Python generator, never the C++ application. One spawned
process owns one live Fortran context until completion or cancellation. HTTP continues to serve immutable
terrain while the worker runs. Both native D3D12 and browser WebGPU consume the existing terrain format.

## Reproducible environment

The supported environment is Linux x86-64, verified in Ubuntu 22.04 under WSL2 on Windows.
`services/terrain/conda-linux-64.lock` fixes the compiler/runtime; `requirements-erosion.lock` fixes the
HTTP/test packages. Python 3.11.17, NumPy 1.26.4 and GNU Fortran 12.4.0 compile
[FastScape Fortran v2.8.4](https://github.com/fastscape-lem/fastscapelib-fortran/tree/v2.8.4), commit
`c21b5c038bde663a61b6226b0cc3d16ce17991ec`. Its GPLv3 license remains with the separate optional service
dependency. No upstream solver source was copied into OFG or patched.

From Windows, with Miniconda installed inside Ubuntu:

```powershell
wsl -d Ubuntu -- bash /mnt/c/dev/ofg/tools/setup-terrain-erosion.sh
wsl -d Ubuntu -- /home/chris/.local/share/ofg/fastscape/bin/python -m terrain_service generate --data-dir /home/chris/.local/share/ofg/terrain-data
wsl -d Ubuntu -- /home/chris/.local/share/ofg/fastscape/bin/python -m terrain_service serve --port 8768 --data-dir /home/chris/.local/share/ofg/terrain-data
build/native-release/ofg.exe --terrain-service http://127.0.0.1:8768 --island demo
```

The setup script accepts `CONDA_EXE` and `OFG_EROSION_PREFIX` for other Linux installations. It records
the actual compiler and source revision in the environment's `share/ofg-fastscape-build.json`; previews
include that record, Python/NumPy/library versions and the complete recipe. An independently installed
binding without this record is explicitly marked as having unrecorded provenance.

Keep live WSL content on the Linux filesystem. A tested `/mnt/c/...` publication failed with PermissionError
while renaming a completed directory. The old revision remained intact. The Windows viewer successfully
streamed the service using Linux-local content. Copy completed diagnostics to `artifacts` for inspection;
the client does not need filesystem access to the generator.

## Editor and API

Open **Terrain streaming → FastScape erosion**. **Start erosion (paused)** uses the explicitly displayed
revision and its matching source recipe. This starts a new experiment, not a restart of an earlier solver
from height alone. **Play**, **Pause** and **Step** control the existing worker. **Cancel generation** destroys
that worker while retaining its last publication. Editing the next run is disabled during an experiment.
Neither stepping nor publication moves the camera.

`POST /v1/jobs` adds an optional `erosion` object to `{island, operation_id, parameters}`:

```json
{
  "initial_revision": "0123456789abcdef0123456789abcdef",
  "timestep_years": 1000,
  "steps": 100,
  "uplift_m_per_year": 0.001,
  "incision": 0.00002,
  "sediment_incision": 0.00004,
  "diffusivity": 0.1,
  "deposition": 1,
  "marine_diffusivity": 0,
  "precipitation": 1,
  "perturbation_m": 1,
  "preview_steps": 10,
  "preview_seconds": 1,
  "preview_stride": 1,
  "start_paused": true
}
```

The revision illustrates syntax; supply an existing revision of the selected island. Geometry `parameters`
must equal its saved recipe. Repeating an operation ID and body returns the same job.
`POST /v1/jobs/{id}/pause`, `/resume`, `/step` and `/cancel` are explicit controls. Step requires acknowledged
`paused` status. Pause is acknowledged between steps, not while Fortran executes. Status reports step,
simulated years, solver wall time, process RSS, export time and latest published revision.
After an ambiguous control response, the viewer retries by reading status; it never blindly repeats Step.

The worker publishes its initial condition, then completed states at the requested step cadence and minimum
wall interval. A paused Step always publishes, as does the final step. One small publication message must be
adopted by the parent before another export starts; arrays never cross the pipe. Each export checks disk
headroom. The viewer independently finishes its current atomic handoff and retains at most one newer pending
revision. It may skip previews when simulation outruns rendering.

Preview strides 2/4/8 subsample export only, with explicitly larger source spacing. The source origin must
align and at least 256 preview intervals must remain. Bounds still reduce the **full simulation grid**, so
a skipped peak cannot falsely certify an empty branch. Snapshots are viewable content, not restart checkpoints.

## Numerical meaning and limits

Application X/Z maps to solver X/Y; arrays are `[z,x]`, flattened with X varying fastest. Lengths are metres
and timesteps years. The model combines routed stream-power incision (`K A^m S^n`), upstream sediment
deposition and hillslope diffusion. Uplift covers the initial land footprint, with zero outer-edge uplift.
The outer boundary is fixed and the island has a constant seabed margin. The marine routine copies corners
from horizontal neighbours; incompatible initial corners are rejected rather than altered after each step.
The initial seeded ±1 m perturbation breaks the flat plateau's drainage symmetry.

This is landscape evolution with routed drainage, not a moving-fluid solver. Precipitation scales routed
contributing area/discharge. `drainage.npy` is an experimental diagnostic, never water depth. Terrestrial
water is unknown; submerged water depth uses the static-sea convention. Material ID 1 means bare rock,
ID 2 sediment (surface minus bedrock above 1 mm). Simulation arrays are float64, published channels float32.

The adapter deliberately selects **single-flow direction**. Paused/exported versus uninterrupted runs
exposed heap-dependent results in v2.8.4's multiple-flow path. Valgrind traced uninitialized values to
`StreamPowerLaw` lake-water scratch. The supported single-flow API makes final exports bit-identical.
Valgrind still reports uninitialized values in Marine's unused compaction calculation; its height update
is commented out upstream. We do not claim a clean upstream memory audit. The unmodified single-flow API
also doubles outlet accumulation when a node receives itself; interpret outlet drainage diagnostics with
that convention, not as an independently balanced rainfall budget.

Marine diffusion defaults to **zero**. The tested nonzero model holds continental heights during submarine
diffusion and can supply a large unbalanced sediment volume at the coast. It remains explicitly experimental.
Marine deposition of continental sediment still runs with diffusion zero. `net_volume_change_m3` subtracts
applied uplift; `sediment_volume_m3` integrates surface minus bedrock. Neither asserts conservation in a domain
with open/fixed boundaries.

The initial 100,000-year experiment at 1000/500/250-year steps failed the 1%-of-relief convergence criterion.
Reducing timestep and horizon is a numerical experiment, not a relaxation of that criterion. With marine
diffusion zero, the 10,000-year comparison at 100/50/25-year steps has RMS differences 0.6571 m then 0.3273 m,
versus 121.0 m initial relief: improving and below 1% for the finer pair. These are fixture results, not proof
that every editor setting is calibrated or that a metre-resolution island is ready.

## Evidence and costs

`tools/terrain-erosion-experiment.py` saves recipes, arrays, terrain publications, drainage and hillshades.
Uplift on/off, doubled precipitation, fluvial deposition on/off and timestep refinement are separate runs.
The selected short comparison is in `artifacts/terrain-service/stage-4/continental-comparisons`.
Earlier failed long comparisons remain available.

Initial scaling measurements used an i7-12700H, Ubuntu WSL2, the pinned Fortran build, one solver process,
32 m spacing, a small island plus increasing seabed extent, 5 steps of 100 years and marine diffusion 10.
Other development work was running, so these are preliminary workload costs, not isolated CPU benchmarks.
Export through `/mnt/c` includes arrays, bounds, metadata/hash and diagnostic PNG. Peak RSS is the process
high-water mark across sequential cases, including export; no GPU was used.

| Grid samples | Total for 5 solver steps | Peak RSS | One export |
| --- | ---: | ---: | ---: |
| 257² | 0.37 s | 88 MiB | 1.18 s |
| 513² | 1.64 s | 183 MiB | 1.28 s |
| 1025² | 7.09 s | 531 MiB | 3.46 s |
| 2049² | 33.18 s | 1869 MiB | 6.51 s |

This grows domain size, not island resolution. It does not justify a 128 km/metre-scale runtime or CPU/GPU
price extrapolation. Profile the intended terrain and interval before renting hardware.

A separate delivery measurement used 20 sequential immutable tile GETs over one reused httpx connection,
from Windows to the WSL loopback service (513² source, 32 m spacing). Each response was 1,006,407 bytes;
median latency was 23.14 ms and p95 35.33 ms, including source sampling/encoding and full body transfer.
Concurrent development work was running. Raw measurements and the revision are in
`artifacts/terrain-service/stage-4/http-cost.json`; these are not remote-network estimates.

Verification commands, from the repository root:

```powershell
wsl -d Ubuntu -- /home/chris/.local/share/ofg/fastscape/bin/python -m pytest /mnt/c/dev/ofg/services/terrain/tests -q
wsl -d Ubuntu -- /home/chris/.local/share/ofg/fastscape/bin/python /mnt/c/dev/ofg/tools/terrain-erosion-experiment.py
wsl -d Ubuntu -- /home/chris/.local/share/ofg/fastscape/bin/python /mnt/c/dev/ofg/tools/terrain-erosion-experiment.py --benchmark
$env:OFG_TERRAIN_SERVICE='http://127.0.0.1:8768'
$env:OFG_TERRAIN_EROSION='1'
build/native-release/ofg-render-test.exe '--test-case=Terrain laboratory renders asynchronously generated native geometry' --no-colors
Remove-Item Env:OFG_TERRAIN_EROSION
Remove-Item Env:OFG_TERRAIN_SERVICE
node tools/terrain-erosion-smoke.mjs
.venv-terrain/Scripts/python tools/terrain-client-test.py --soak --executable build/native-release/ofg-render-test.exe
```

Native/browser integration captures three paused steps and checks revision adoption, unchanged camera
(native), budget bounds and cancellation. Evidence is under `artifacts/terrain-service/stage-4`.
The smoke expects the separately started server on 8768; `OFG_EROSION_PORT` changes its proxy target.
The ten-minute test owns its temporary service, moves around the island and to distant coordinates,
regenerates repeatedly and drains allocations.

Final native regression passes all nine CTest groups in 228.83 seconds, including 2200-material descriptor
pressure, cross-material snapshots and within-command parameter edits. This run used Intel Iris Xe D3D12
with validation and concurrent builds/stress work. The complete graphics-suite allowance is now 300 seconds;
its individual asynchronous deadlines and correctness tolerances are unchanged. The native renderer's
[descriptor adaptation](architecture.md#native-parameter-block-descriptor-reuse) addresses the exhaustion
found by aerial erosion replay. Exact-value mipmap tests also caught an Intel half-float rounding issue,
fixed by averaging differences from a reference sample instead of rounded absolute totals.

The final Linux Python suite passes 64 tests. Coverage includes spawned workers and the collector thread:
94.7% of project service lines and 88.3% of branches, with no project exclusions. The Windows environment
runs the service tests while skipping the optional Fortran suite. To reproduce coverage, keep its temporary
data on the Linux filesystem (Windows-mounted coverage files encountered permission errors):

```powershell
wsl -d Ubuntu -- env COVERAGE_FILE=/tmp/ofg-terrain-coverage/.coverage /home/chris/.local/share/ofg/fastscape/bin/python -m coverage run --rcfile=/mnt/c/dev/ofg/services/terrain/pyproject.toml -m pytest /mnt/c/dev/ofg/services/terrain/tests -q
wsl -d Ubuntu -- env COVERAGE_FILE=/tmp/ofg-terrain-coverage/.coverage /home/chris/.local/share/ofg/fastscape/bin/python -m coverage combine --keep --rcfile=/mnt/c/dev/ofg/services/terrain/pyproject.toml /tmp/ofg-terrain-coverage
wsl -d Ubuntu -- env COVERAGE_FILE=/tmp/ofg-terrain-coverage/.coverage /home/chris/.local/share/ofg/fastscape/bin/python -m coverage report --rcfile=/mnt/c/dev/ofg/services/terrain/pyproject.toml
```

Create `/tmp/ofg-terrain-coverage` first. The final ten-minute D3D12 test completed 90,555 frames,
five regenerations and 452,788 assertions with tracked allocations draining to zero (Intel Iris Xe,
960x640, validation enabled). Concurrent development work was running; its frame count is a stability
result, not a performance benchmark. Browser erosion playback passed three paused states at 0/1000/2000
years with no console errors; texture/checkerboard and PBR browser regression also pass.
