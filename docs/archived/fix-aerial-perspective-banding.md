# Remove surface radiance from aerial perspective

This ExecPlan follows [PLANS.md](../../PLANS.md). The reported foreground haze defect is fixed and validated on native D3D12 and browser WebGPU.

## Purpose / Big Picture

Make haze add atmospheric scattering without projecting a second lit ground surface onto scene geometry. Provide
an existing-panel haze toggle for comparisons, and retain the day/night exposure and incremental IBL contracts.

## Progress

- [x] (2026-10-04) Inspected shader generation, sampling, samplers and current validation.
- [x] (2026-10-04) Reproduced: vacuum slices contained 744 to 1536 nonzero channels; saved native before/after captures.
- [x] (2026-10-04) Corrected ground term, physical-distance weights and toggle. Both focused GPU tests pass (1,073 assertions).
- [x] (2026-10-04 18:05Z) Validated native/WebGPU, inspected captures, reopened the corrected native demo and updated documentation.

## Surprises & Discoveries

`integrateAtmosphere` adds a Lambertian ground reflection when a ray hits the planet. The aerial-volume pass calls
the same function as the sky and therefore stores that surface reflection as in-scattered light. Interpolating across
the intersection makes visible bands. A vacuum must have zero in-scattering, making this independently testable.
The volume uses quadratically spaced distances but interpolates by fractional slice index; this overstates near
scattering. Hardware bilinear filtering and explicit inter-layer interpolation already exist, so missing filtering
is not the primary issue.

## Decision Log

Keep the current textures, bindings and resolution. Exclude ground surface radiance only from aerial perspective;
retain it for sky and multiple-scattering closure. Interpolate using physical endpoint distances. Add a diagnostic
haze toggle without disabling exposure, direct lights, sky or IBL. Do not mask the bug by increasing LUT resolution.

## Outcomes & Retrospective

The native vacuum regression failed on the original shader and passes after excluding the ground boundary term.
The original sampling produced a 5.20234-unit error on a linear-distance ramp; the corrected production shader passes
with a 0.005-unit tolerance at output values up to 20,000. Native before/after images confirm the broad foreground
bands are gone. A separate low-sun sphere-shading artifact remains visibly present and is not claimed fixed.
Full native CTest passes 4/4 in 58.61 seconds; the focused GPU tests pass 1,073 assertions. PBR WGSL ABI and actual
browser shader checks pass. The WebGPU build succeeds with the existing Asyncify/exceptions warning; final browser
outdoor runtime and UI checks pass on Chrome WebGPU, Intel Gen12LP with a requested 16-texture limit and no errors.
The inspected checkbox capture confirms haze is disabled; near-foreground mean haze on/off difference is 0.01 of an
8-bit channel level. Native presentation passed resize/maximize/minimize/restore/clean close; the corrected demo was
reopened for the user. The ordinary PBR browser smoke also passes. Formatting, JavaScript syntax and whitespace checks pass. Scalar extinction, coarse angular lookup resolution and atmospheric numerical
precision remain separate approximations; this work does not promise to eliminate every possible lighting artifact.

## Context and Interfaces

`shaders/sky/atmosphere.slang` owns the shared integral; `aerial-perspective.slang` generates the 32 distance slices.
`shaders/shadows/sampling.slang` samples/composes the volume. `scene/outdoor-lighting.h`, `render/graphics.cpp` and
`ui/render-settings-panel.cpp` expose the haze checkbox. Tests live in `tests/outdoor-rendering-test.cpp` and the
existing browser smoke. Slang RHI remains pinned to 16324a68af477baaede620e713644f5e9613b1a2.

## Plan of Work and Concrete Steps

Add a real GPU vacuum test and render noon/sunset before changing shader behavior. Use the documented x64 Visual
Studio environment to build native targets, run the focused outdoor tests, then apply the fix and repeat. Run full
native CTest, the PBR shader check, the WebGPU build and outdoor smoke according to DEVELOPING.md and build skills.
Record actual commands and results, rather than treating compilation as visual proof.

## Validation and Acceptance

Vacuum lookups must contain zero in-scattering and opacity, including rays ending on the planet. Check actual
sampler interpolation against a synthetic LUT linear in physical distance. Compare haze on/off at fixed lighting
and inspect native/browser screenshots, retaining evidence under artifacts/lighting/haze. No new coverage threshold.

## Milestone Review

Reviewed shader contracts: aerial volume still uses the same RGBA16F 32-layer allocation and two samples; no
texture/sampler binding was added. Ground reflection remains in sky and multiple-scattering closure. Quadratic slice
indices are clamped to a valid adjacent pair at the far boundary; the zero-distance sample stays zero. The checkbox
only bypasses haze composition and preserves HDR exposure; it does not invalidate the frozen IBL job. Regression
tests compile the production sampling source and read actual GPU results. Remaining low-sun sphere bands are kept
separate from the successfully reproduced foreground haze defect.

## Idempotence and Recovery

The working tree was clean at start. Preserve the user's active native demo until it must be replaced for linking;
close only that known process cleanly and reopen the corrected demo. Reuse native/web build trees and dependency pins.

## Artifacts and Notes

`artifacts/lighting/haze/before-test.log` records the failing vacuum regression; `before-sampling-test.log` records
the failing physical-distance reconstruction. `after-test.log` records the two focused passing GPU tests. Native
`before-noon.png` / `after-noon.png` show removal of the broad foreground bands, with matching sunset captures.
`artifacts/lighting/browser/settings.png`, `haze-off.png` and `report.json` record the real UI comparison and diagnostics.
Native presentation evidence remains in `artifacts/lighting/native/window-report.json` and `maximized.png`.
Native GPU: NVIDIA GeForce RTX 3050 Ti Laptop GPU, D3D12. Browser: Chrome 154.0.8037.95, Intel Gen12LP WebGPU.
No performance claim or texture-resolution increase was needed.
