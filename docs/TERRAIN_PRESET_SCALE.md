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

The current presets were retuned on 2026-06-14 after adding player-bounded
vertical terrain bands and coarse far coverage. The key change is that macro
feature wavelengths are now kilometer-scale. The initial proof pass used LOD6
and an 18 km generated span, but the current playable default trims that to
LOD5 with about a 7 km generated span, a 3500 m camera far plane, and a
200-3000 m skybox-matched fog ramp. That means presets are now plausible shape
baselines rather than finished authored vistas; human tuning should decide
which kilometer-scale features read well inside the fogged horizon.

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
| `seed` | Lowland Plain. Neutral low-relief baseline for compatibility and quick smoke checks. | Mild | About 7.1 km macro form with 167 m detail. It should read as broad lowland undulation rather than chunk-scale bumps. |
| `rollingHills` | Low, broad, grassy terrain with gentle horizon-scale variation. | Low to moderate | About 4 km macro form and 1.4 km ridge hint, matching low-relief kilometer-scale hill/drumlin references without making the default terrain mountainous. |
| `mountainValley` | Mountain-valley massing with broad ranges and large valley walls. | Highest built-in relief | About 9.1 km macro form, 3.1 km ridges, and 6.25 km warp. This is still conservative compared with true alpine terrain, but now uses real landform scale. |
| `rockyHighland` | Rugged highland with broad massing plus smaller erosional cuts. | Moderate to high | About 5.9 km macro form, 1.6 km ridges, 909 m cellular breakup, and 118 m detail so it keeps rocky texture on top of large upland massing. |

## Current Parameter Table

| Preset | Base | Large Freq / Wavelength / Scale | Ridge Freq / Wavelength / Scale | Warp Freq / Wavelength / Amp | Cellular Freq / Wavelength / Scale | Detail Freq / Wavelength / Amp |
| --- | ---: | --- | --- | --- | --- | --- |
| `seed` | 4 m | `0.00014` / 7143 m / 16 m | `0.00035` / 2857 m / 0 m | `0.00012` / 8333 m / 60 m | `0.00050` / 2000 m / 0 m | `0.0060` / 167 m / 1.5 m |
| `rollingHills` | 12 m | `0.00025` / 4000 m / 42 m | `0.00070` / 1429 m / 8 m | `0.00022` / 4545 m / 140 m | `0.00080` / 1250 m / 2 m | `0.0065` / 154 m / 3 m |
| `mountainValley` | 24 m | `0.00011` / 9091 m / 110 m | `0.00032` / 3125 m / 105 m | `0.00016` / 6250 m / 240 m | `0.00055` / 1818 m / 14 m | `0.0045` / 222 m / 6 m |
| `rockyHighland` | 18 m | `0.00017` / 5882 m / 78 m | `0.00062` / 1613 m / 72 m | `0.00028` / 3571 m / 190 m | `0.00110` / 909 m / 22 m | `0.0085` / 118 m / 8 m |

## Remaining Terrain-Band Constraints

The old fixed absolute height search has been removed. `height_at_with_shape`
now brackets the surface around the sampled macro terrain elevation, using a
shape-relative detail bracket, so high-base terrain is no longer implicitly
clamped to the old `-96m..96m` band for player grounding and surface probes.

The vertical band resolver can stream taller columns, and the default far LOD
set can show longer wavelengths. However, taller authored ranges still multiply
terrain streaming, surface-query, water, and player-grounding costs. A preset or
editor change that creates regular large frame spikes is not complete: OFG is
targeting 60fps play, and 500ms-class terrain/frame spikes must be fixed or kept
behind a non-default diagnostic setup before the change is accepted.

The other practical limits are:

- Terrain variant validation now allows much larger authoring experiments:
  base height `-4096..4096`, macro relief `-2048..2048`, ridge relief
  `0..2048`, cellular relief `0..1024`, detail amplitude `0..512`, and domain
  warp amplitude `0..8192`.
- Smoke and browser startup still assume terrain can be found and inspected
  near the initial playable height without a terrain-aware vertical shell.
- Current terrain has no hydrology, erosion solver, water, talus, cliffs as
  separate features, or biome/climate layer, so increasing vertical scale alone
  will not create convincing mountains.

## Next Targets

The next fix should still be architectural before it is artistic:

1. Add authored terrain-interest bounds for future caves, lakes, strata, and
   hydrology rather than relying only on sampled height plus padding.
2. Add preset smoke captures that explicitly sample higher mountain relief and
   low valley floors before raising the built-in defaults.
3. Add movement/perf acceptance that catches terrain-streaming or height-query
   spikes before larger terrain values become the default.
