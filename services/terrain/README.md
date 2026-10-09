# OFG terrain service

Independent Python service for the flat-island generation milestone. Generation runs in one spawned process;
the HTTP host serves immutable revisions and remains responsive while it runs. FastScape is not integrated yet.
The viewer connection and editor controls are subsequent stages in the [active plan](../../docs/plans/terrain-generation-service.md).

From the repository root on Windows:

```powershell
python -m venv .venv-terrain
.venv-terrain/Scripts/python -m pip install -r services/terrain/requirements.lock
.venv-terrain/Scripts/python -m pip install --no-deps -e services/terrain
.venv-terrain/Scripts/python -m terrain_service generate
.venv-terrain/Scripts/python -m terrain_service serve --data-dir artifacts/terrain-service/data --port 8765
```

`generate` makes a default island and prints the revision directory, including `diagnostic.png`. Use
`--recipe recipe.json` to supply an `IslandParameters` JSON object. The defaults are a 513x513 source raster
at 32 m spacing, 4096 m seed spacing and a flat 40 m plateau above an -80 m seabed. All generation inputs are
saved in the manifest. The source origin is aligned to the world sample lattice, even at distant coordinates.

Submit interactive jobs with JSON to `POST /v1/jobs`, for example:

```json
{"island":"demo","operation_id":"first-island","parameters":{"seed":"1"}}
```

Poll `GET /v1/jobs/{id}` for completion, then resolve `/v1/islands/demo/manifest`. A conflicting job returns 409.
Cancellation preserves the previous publication. A service restart records interrupted jobs as failed;
retry a failed experiment using a new operation ID. Repeating the original ID returns its original outcome.

Saved-content serving requires the data root containing `demo/revisions`, not an individual revision directory:

```powershell
.venv-terrain/Scripts/python -m terrain_service serve --read-only artifacts/terrain-service/data --port 8766
.venv-terrain/Scripts/python -m pytest services/terrain/tests --cov=terrain_service --cov-branch --cov-report=term-missing
```

The locked environment was installed with Python 3.10.5 on Windows. No GPU or client build is needed to run it.
The HTTP server binds loopback by default, with one active generation job and a 4096 MiB output budget. Use
`--output-limit-mib` to change that explicit cap. Old published revisions are retained; stop the service before
manually removing obsolete revisions or interrupted `.tmp` directories inside its data directory.

The [wire contract](../../docs/terrain-content.md) defines the 256-cell tiles, apron, channel layout, CRC and
full-source conservative bounds. The initial water field is static sea depth, not a hydraulic simulation.
