# Terrain content version 1

The Python service publishes immutable heightfield revisions. Geometry and shading use the same canonical
samples. This contract is implemented by `services/terrain/terrain_service/protocol.py` and both application
hosts; implementation evidence is recorded in the [plan](archived/terrain-generation-service.md).

## Coordinates and resolution

Y is up; heights are metres above world elevation zero. Roots are 131072 m wide. Root X/Z addresses are signed
64-bit integers, serialized as decimal strings in JSON. Tile depth `d` divides a root into `2^d` tiles per axis;
tile X/Z indices are unsigned integers in `[0, 2^d)`. A tile has 256 intervals across its footprint, 257 edge-inclusive
samples, and a one-sample apron on each side: 259x259 stored samples, X varying fastest. Stored index 1 is the
low core edge; index 257 is the high core edge. Mesh intervals currently number 32, giving eight data intervals
per mesh interval. GPU UVs must include apron and texel-centre offsets.

The published source grid has integer power-of-two spacing from 1 to 512 m and a world-aligned integer origin.
`terminal_depth = log2(131072 / (256 * source_spacing))`. Finer mesh descendants inherit their terminal ancestor's
data; the service rejects requests below this depth. Each coarser node requests its own footprint. Identical
world locations are sampled identically across roots and levels. Outside the finite source, the field is seabed.

## Binary layout

All numbers are little endian, with no implicit alignment. The header is exactly 192 bytes. The body contains
five planar arrays with 67081 samples each, occupying exactly 1,006,215 bytes. A response totals 1,006,407 bytes;
the transport must enforce a 2 MiB upper bound before decoding. Additional or missing bytes are errors.

| Offset | Type / size | Value |
| --- | --- | --- |
| 0 | 8 bytes | `OFGTRN1` followed by zero |
| 8 | uint32 | Format version, 1 |
| 12 | uint32 | Header bytes, 192 |
| 16 | uint32 | Body bytes, 1006215 |
| 20 | uint32 | Intervals, 256 |
| 24 | uint32 | Apron samples per side, 1 |
| 28 | uint32 | Data depth |
| 32, 40 | int64 each | Root X, Z |
| 48, 52 | uint32 each | Tile X, Z |
| 56 | 16 bytes | Revision UUID bytes in the order of its 32-character lowercase hex string |
| 72 | float64 | Root width, 131072 m |
| 80 | float64 | Source spacing in metres |
| 88 | 6 float64 | AABB min X/Y/Z, max X/Y/Z; X/Z root-relative, Y world elevation |
| 136 | uint32 | IEEE CRC-32 of the complete response with these four bytes zero |
| 140 | 52 bytes | Reserved, must all be zero |
| 192 | 67081 float32 | Ground surface elevation |
| 268516 | 67081 float32 | Bedrock elevation |
| 536840 | 67081 float32 | Water depth above ground |
| 805164 | 67081 uint16 | Material ID (1 = bare rock, 2 = sediment) |
| 939326 | 67081 uint8 | Validity bits: 1 bedrock, 2 water, 4 material; other bits zero |

CRC detects corruption, not malicious alteration. Compare revision, root and tile address against the captured
request independently. Reject unsupported dimensions, nonfinite numbers, malformed bounds, invalid materials,
bedrock above surface, negative known water depth, and nonzero reserved bits. Unknown attributes have zero
payload values; unknown water is different from known dry ground. Surface is always valid.

The ground AABB contains every fine source bilinear cell intersecting the requested core footprint, with
outward-rounded elevations. It can be much taller than the coarse returned heights. A min/max pyramid computes
these bounds independently of tile sample resolution. A node strictly outside this height range can be certified
surface-empty; touching cannot. An empty coarse mesh alone never certifies a subtree. Bounds describe ground,
not future water or vegetation renderables.

## HTTP and publication

`GET /v1/health` reports capability and read-only mode. `/v1/islands/{island}/manifest` resolves latest;
`/v1/islands/{island}/revisions/{revision}/manifest` pins all metadata. The tile template is
`/v1/islands/{island}/revisions/{revision}/terrain/{rootX}/{rootZ}/{depth}/{tileX}/{tileZ}.bin`.
Immutable reads are cacheable; latest and controls use `Cache-Control: no-store`.

`POST /v1/jobs` accepts `{island, operation_id, parameters}` and returns 202 with an ID. An identical operation
is idempotent across restarts; conflicting input or another active job returns 409. `GET /v1/jobs/{id}` reads
state; `POST /v1/jobs/{id}/cancel` requests cancellation. Invalid input returns 422, unknown content 404, and
control bodies above 256 KiB return 413. Content reads do not start generation. Read-only mode has no controls.

A worker writes source arrays, bounds, metadata and a diagnostic to a temporary directory, then renames it
to its UUID. The parent job owner atomically updates latest after each completed, uncancelled publication.
Flat generation publishes once; erosion can publish completed previews while its solver remains active.
Old revisions never change. Interrupted temporary directories are unserved and count against the configured
storage budget until explicit maintenance. Saved serving opens the same directory hierarchy without a generator.

## Viewer integration

The native viewer accepts `--terrain-service http://127.0.0.1:8765 --island demo`; the browser accepts
`?demo=terrain&terrainService=/v1&island=demo` through the development host's fixed loopback proxy.
Omitting the service retains the noise diagnostic. The viewer pins an immutable manifest before building.
Builds acquire through two asynchronous cache I/O threads and resume on the existing two mesh threads.
The coordinator and render thread do not wait for HTTP or persistent storage. Native uses WinHTTP and
atomic files under `artifacts/terrain-cache`; web uses Fetch and IndexedDB `ofg-terrain-v1`.
`--skip-terrain-cache` / `skipCache=1` bypass both local reads and writes and HTTP caching.
Changing the UI checkbox affects subsequent acquisitions, not already loaded nodes.

Each node acquires its own horizontal tile until source spacing is reached. Descendants then retain that
ancestor's input and texture allocations and map their smaller footprint into it. Certified-empty branches
discard input arrays; a merely empty coarse mesh retains its source so it can refine.

Shading uses an RGBA16F texture plus point-sampled R8 validity. Height is stored as a half-float high part
and residual, in units of 1024 m; tests bound reconstruction error below 1 mm over the supported elevation
range. The other channels store sediment thickness and water depth in the same units. Unknown channels
remain distinct from zero. Continuous attributes require all contributing samples valid; material uses
nearest sampling. Palette ID 1 is bare rock and ID 2 is sediment; the R8 texture packs validity in bits 0..2
and sediment classification in bit 3. Base-level sampling uses the apron;
mipmapped minification is not implemented. Intermediate source samples affect shading normals without
adding geometry. The optional flat sea-level grid is a visual reference, not simulated water.

Service terrain has a 512 MiB combined CPU residency budget and 256 MiB GPU budget. Shared decoded inputs
and CPU/GPU textures are charged once across active and retired descendants. The complete 16 m fixture
measured about 419 MiB CPU and 190 MiB GPU, including 176.6 MiB decoded sources and 105.9 MiB texture
images on each side. Two meshers have separate scratch allowances; uploads have a 16 MiB staging cap.
Texture conversion adds under 1 MiB transient scratch. Browser-managed transport buffers are outside the
application's exact memory accounting. GPU resources retire only after selection changes and completion.
