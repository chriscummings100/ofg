# Terrain Preset Scale Notes

This note records the current built-in terrain shape preset numbers and why they
were chosen. The runtime source of truth is
`crates/terrain_core/src/presets.rs`; this document explains the design intent so
future tuning does not quietly shrink the terrain back to texture scale.

## How To Read The Numbers

Preset frequencies are treated as cycles per world meter. A first-order
wavelength estimate is therefore:

```text
wavelength_m ~= 1.0 / frequency
```

The estimate is only a tuning guide. The final surface combines fractal octaves,
domain warp, ridged noise, cellular edge contribution, material classification,
and 3D density detail. Height-related fields are in world meters, but
`height_scale`, `ridge_height_scale`, `cellular_height_scale`, and
`detail_amplitude` are generator knobs, not guaranteed final relief.

Current sampled relief was measured from `assets/wasm/terrain_core.wasm` on
2026-06-10, using seed `0x0F6` over a 4 km by 4 km grid from `-2048..2048` in X
and Z at 256 m spacing. This is a sanity check for the current build, not a
contract for every seed.

## Real-World Scale Anchors

The current defaults are Earth-inspired, not a simulator. They use real
landform scales as guardrails:

- Rolling/glacial lowland forms can be kilometer-scale even when relief is low.
  Britannica summarizes drumlins as commonly 1-2 km long, 400-600 m wide, and
  15-30 m high: <https://www.britannica.com/science/drumlin>.
- Hilly and low-mountain relief can reach tens to hundreds of meters. A USGS
  Piedmont report describes many hills rising about 90 m above their bases and
  Crowders Mountain with about 183 m of relief:
  <https://pubs.usgs.gov/pp/1265/report.pdf>.
- Mountain valleys are kilometer-scale landforms with much larger possible
  relief than the current game band allows. NPS describes glacial valleys as
  widened, flat-bottomed, and steep-walled:
  <https://www.nps.gov/articles/ushapedvalleysfjordshangingvalleys.htm>.
  A USGS/NPS Yosemite history describes an earlier mountain-valley stage around
  1,600 ft deep, with a 1,200 ft brow over roughly half a mile:
  <https://npshistory.com/publications/geology/pp/160/sec2e.htm>.
- Ridge-valley spacing is a real geomorphic length scale, not just visual
  roughness. Perron, Kirchner, and Dietrich compare observed valley spacing
  across field sites and discuss characteristic wavelengths:
  <https://website.whoi.edu/gfd/wp-content/uploads/sites/14/2018/10/Perron09_75803.pdf>.
- Badlands and rocky highlands need smaller erosional structure on top of broad
  massing. NPS describes Badlands National Park as sparse vegetation carved into
  spires, pinnacles, hoodoos, monuments, buttes, and mesas:
  <https://www.nps.gov/articles/nps-geodiversity-atlas-badlands-national-park-south-dakota.htm>.
  The NPS geologic resource report also records local erosional relief around
  24.5 m in one Scenic Member contact:
  <https://npshistory.com/publications/badl/nrr-2008-036.pdf>.

## Current Built-In Defaults

| Preset | Intent | Current Relief | Why This Envelope |
| --- | --- | ---: | --- |
| `seed` | Neutral varied baseline for compatibility and quick smoke checks. | 48.0 m | Broad 625 m macro form with mild 83 m detail; no ridge, warp, or cellular uplift. |
| `rollingHills` | Low, broad, grassy terrain with gentle horizon-scale variation. | 39.9 m | About 950 m macro form and 455 m ridge hint, matching low-relief kilometer-scale hill/drumlin references without making the default terrain mountainous. |
| `mountainValley` | The largest current shape preset, suggesting a valley wall or mountain shoulder inside the present terrain band. | 67.6 m | About 1.8 km macro form, 950 m ridges, and 1.5 km warp. Vertical relief is intentionally capped by current terrain-band limits. |
| `rockyHighland` | Rugged highland with broad massing plus smaller erosional cuts. | 63.4 m | About 1 km macro form, 417 m ridges, 167 m cellular breakup, and 56 m detail so it keeps rocky texture without returning to toy-scale macro shapes. |

## Current Parameter Table

| Preset | Base | Large Freq / Wavelength / Scale | Ridge Freq / Wavelength / Scale | Warp Freq / Wavelength / Amp | Cellular Freq / Wavelength / Scale | Detail Freq / Wavelength / Amp |
| --- | ---: | --- | --- | --- | --- | --- |
| `seed` | 2 m | `0.0016` / 625 m / 34 m | `0.0020` / 500 m / 0 m | `0.0015` / 667 m / 0 m | `0.0035` / 286 m / 0 m | `0.0120` / 83 m / 3 m |
| `rollingHills` | 4 m | `0.00105` / 952 m / 28 m | `0.0022` / 455 m / 1.2 m | `0.0010` / 1000 m / 110 m | `0.0030` / 333 m / 0.8 m | `0.0110` / 91 m / 2 m |
| `mountainValley` | -4 m | `0.00055` / 1818 m / 40 m | `0.00105` / 952 m / 46 m | `0.00065` / 1538 m / 220 m | `0.0018` / 556 m / 6 m | `0.0075` / 133 m / 5.5 m |
| `rockyHighland` | 8 m | `0.00095` / 1053 m / 38 m | `0.0024` / 417 m / 34 m | `0.0013` / 769 m / 180 m | `0.0060` / 167 m / 14 m | `0.0180` / 56 m / 8.5 m |

## Terrain-Band Constraints

The major current limitation is vertical, not horizontal. `terrain_core`
currently searches for the heightfield-like surface between
`SURFACE_SEARCH_MIN_Y = -96.0` and `SURFACE_SEARCH_MAX_Y = 96.0`. If a preset's
real surface regularly lives outside that absolute band, player grounding,
surface probes, and density/height consistency become unreliable.

Because of that band, `mountainValley` and `rockyHighland` deliberately use
realistic horizontal wavelengths but conservative vertical relief. Their
current relief should read as low mountains, shoulders, escarpments, or rugged
uplands, not true alpine terrain.

The other practical limits are:

- Domain warp amplitude currently validates at `0..256`, so `mountainValley`
  already sits near the upper end at 220 m.
- Smoke and browser startup still assume terrain can be found and inspected
  near the initial playable height without a terrain-aware vertical shell.
- Current terrain has no hydrology, erosion solver, water, talus, cliffs as
  separate features, or biome/climate layer, so increasing vertical scale alone
  will not create convincing mountains.

## Post-Band-Fix Targets

Once surface search and streaming are no longer tied to the fixed `-96..96`
band, update presets toward these ranges. Do this after adding tests that prove
height queries, density chunks, player grounding, terrain probes, and smoke
captures remain stable for surfaces well above and below the old band.

| Preset | Current Constraint | Target After Vertical/Far-Field Fix |
| --- | --- | --- |
| `seed` | Not heavily constrained. It is intentionally mild. | Keep macro wavelengths around 600 m to 1.2 km. Relief can stay around 40-80 m unless the baseline needs a more dramatic default. |
| `rollingHills` | Mostly art constrained, not band constrained. Current 40 m relief is appropriate for gentle terrain. | Keep 800 m to 2 km macro wavelengths. Optional hillier variants can reach 60-120 m local relief, but the default should remain navigable and low. |
| `mountainValley` | Strongly constrained. Current 68 m sampled relief is far below real mountain-valley examples because the fixed search band would make 250 m+ relief fragile. | First target 250-450 m local relief with 2-5 km macro wavelength, 1-2 km ridge wavelength, 300-800 m warp amplitude, and 100-250 m detail wavelength. Later add a distinct alpine preset at 500-1000 m relief if the vertical shell, LOD, shadows, fog, and camera controls support it. |
| `rockyHighland` | Moderately constrained. It needs more relief than today, but should remain rougher at smaller scales than `mountainValley`. | Target 120-250 m relief, 800 m to 1.5 km macro wavelength, 250-600 m ridge wavelength, 80-250 m cellular breakup, and 20-80 m detail wavelength. Add erosion/material strata rather than simply raising amplitude. |

The fix should be architectural before it is artistic:

1. Replace fixed absolute surface search with a bracket derived from the macro
   terrain estimate, or make the vertical terrain shell follow the macro
   surface.
2. Expand stream vertical coverage and smoke coverage so high/low surfaces
   render and ground the player reliably.
3. Revisit descriptor validation ranges for `height_scale`,
   `ridge_height_scale`, `cellular_height_scale`, `detail_amplitude`, and
   `warp.amplitude`.
4. Add preset smoke captures that explicitly sample higher mountain relief and
   low valley floors before raising the built-in defaults.
