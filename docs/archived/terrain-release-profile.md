# Measure Release terrain function costs

This ExecPlan follows [PLANS.md](../../PLANS.md). Authorized profiling work, completed 2026-10-07.

## Purpose and scope

Measure the current terrain workload in actual MSVC Release, separating streaming, generation, draw preparation,
render submission and GPU wait. Do not optimize behavior in this task. Preserve workload and correctness assertions.

## Progress

- [x] (2026-10-07) Inspected build and existing sixty-second profile. Working tree started clean.
- [x] (2026-10-07) Added Release presets and opt-in nested function timers, with profiling-only validation control.
- [x] (2026-10-07) Built Release, verified device startup and all nine CTests (50.18 seconds).
- [x] (2026-10-07) Instrumented and inactive-timer Release runs passed convergence and all workload assertions.
- [x] (2026-10-07) Validation-on run passed; formatting and whitespace checks passed.
- [x] (2026-10-07) Analyzed function costs, preserved evidence and documented commands and limitations.

## Surprises & Discoveries

Windows WPR CPU recording failed with 0xc5585011 (performance profiling policy unavailable). No recording was active
before the attempt. Use explicit wall-clock scopes rather than claiming sampled CPU or GPU execution times.
The prior offscreen profile always enabled D3D12 validation regardless of compiler optimization.

## Decision Log

Compile scopes only with OFG_TERRAIN_FUNCTION_PROFILE; runtime activation is specific to the skipped terrain profile.
Measure inclusive and exclusive time (excluding instrumented children only), count calls, and separate the first
45 seconds from the final 15 seconds. Workers report their own scopes and overlap the main thread. Release defaults
remain normal MSVC flags; normal graphics tests still use validation. Compare instrumentation enabled/disabled to
bound perturbation, then validation enabled/disabled. No browser rebuild is required for disabled diagnostic scopes.

## Validation and concrete steps

Use the build-native skill and initialize the x64 VS environment. Configure native-release with
OFG_TERRAIN_FUNCTION_PROFILE=ON, build its targets, run CTest and device startup. Run the existing skipped stationary
profile with and without OFG_TERRAIN_PROFILE_FUNCTIONS and OFG_TERRAIN_PROFILE_VALIDATION. Preserve CSVs and logs under
artifacts/terrain/release-profile. Run profiles serially with no concurrent build/GPU workloads launched by this task.
Record adapter, CPU, viewport, compiler flags, draw/triangle counts, convergence and instrumentation overhead.

## Outcomes & Retrospective

Measurements: inactive-timer median frame 29.4123 ms, active-timer median 29.4712 ms; respective means
31.2062 and 31.5338 ms. The difference is within the observed run variability, not an isolated overhead estimate.
The first instrumented run converged in 8.67491 seconds. Steady Graphics::render averaged 24.8571 ms,
including shadows 6.0850, bindMaterial 5.3787, bindOutdoor 5.2276 and bindFrame 4.4282 ms per frame.
TerrainLaboratory::update averaged 0.1275 ms; remaining queue wait 6.0410 ms. All runs retained 1529 selected nodes,
219 visible draw items and 295066 visible triangles. Evidence is under artifacts/terrain/release-profile.

Timer scopes report elapsed host time, which can include preemption and driver waits. GPU queue
wait is remaining latency after submission, not total GPU execution time. Do not add inclusive parent/child totals.
Worker checkpoint totals are approximate at boundaries because completed calls are recorded on their finishing side.

## Results and interpretation

Hardware: Intel Core i7-12700H, NVIDIA GeForce RTX 3050 Ti Laptop GPU, driver 596.08. MSVC 19.51.36248.0,
Release `/O2 /Ob2 /DNDEBUG`, D3D12, 1440x1200 offscreen target. The profile drains the queue every frame and excludes
ImGui and swapchain presentation. No concurrent compilation or other GPU test was launched during measurements.
The final fifteen seconds of each sixty-second run form the steady window; terrain was idle and fully converged.

| Run | Median frame (ms) | Mean frame (ms) | p95 frame (ms) |
| --- | ---: | ---: | ---: |
| Release, validation off, timers inactive | 29.4123 | 31.2062 | 39.2321 |
| Release, validation off, timers active | 29.4712 | 31.5338 | 39.7425 |
| Release, validation on, timers active | 31.5614 | 33.1065 | 41.2121 |

These are sums of the existing update/extraction/render/wait phases. Diagnostic counters, CSV output and loop assertions
outside those phases add some harness overhead; diagnostics alone averaged 0.2602 ms per steady frame in the timed run.
The timer-active versus inactive mean differs by 0.3275 ms (1.05%) and median by 0.0589 ms (0.20%). This comparison
checks gross perturbation but does not distinguish timer overhead from system variability. Validation adds roughly
2.09 ms to the median in these runs. The previous 70.98 ms RelWithDebInfo recording was not reproduced; these results
do not establish the cause of that historical difference.

Average host function costs per steady frame, validation off:

| Function or timed call | Calls/frame | Inclusive ms/frame | Exclusive ms/frame |
| --- | ---: | ---: | ---: |
| Graphics::render | 1 | 24.8571 | 2.2397 |
| ShadowRenderer::render | 1 | 6.0850 | 6.0850 |
| bindMaterial | 219 | 5.3787 | 5.3787 |
| bindOutdoor | 219 | 5.2276 | 5.2276 |
| bindFrame | 219 | 4.4282 | 4.4282 |
| Graphics createRootShaderObject call | 219 | 0.3955 | 0.3955 |
| Graphics scene encoder finish | 3 | 0.3241 | 0.3241 |
| bindResources | 219 | 0.2545 | 0.2545 |
| Graphics scene queue submit | 3 | 0.2261 | 0.2261 |
| outputPass | 2 | 0.1584 | 0.1578 |
| TerrainLaboratory::append | 1 | 0.4059 | 0.4059 |
| TerrainLaboratory::update | 1 | 0.1275 | 0.0451 |
| queue.waitOnHost | 1 | 6.0410 | 6.0410 |

Graphics::render includes the shadow and binding rows; do not add its inclusive time to theirs. The three binding
functions total 15.0345 ms, about 60.5% of Graphics::render. Inspection shows material field lookup/type validation
and uniform writes repeated for every draw; bindOutdoor and bindFrame also repeat mostly frame-wide lighting data.
These are measured optimization targets. Caching validated layouts and reusing material/frame binding data should
be investigated before changing the streaming architecture. No optimization is included in this profiling task.
The scopes do not isolate individual reflection API calls, shader execution, or driver CPU instructions.

During the first 8.67491 seconds (333 frames) before convergence, TerrainLaboratory::update averaged 5.2549 ms/frame,
including TerrainStream::reconcile 3.3656 ms/frame and upload 1.6787 ms/frame. Inside reconcile, makePlan averaged
0.9533 ms/frame, prune 0.2815 and publish 0.1451. Worker TerrainMesher::step accumulated 1448.8 ms and constructor
sampling accumulated 274.091 ms across 1729 nodes. These worker totals overlap the main thread. In the steady window,
reconcile, meshing and mesh uploads had no calls. Startup also includes first-use shader/pipeline compilation.

All nine Release CTests passed, including the nested-timer accounting regression. The three profile runs respectively
passed 3952 (inactive timers), 3892 (active timers) and 3728 (validation) assertions, preserving coverage/depth/failure
checks. Function scopes compile out by default and can be activated only in the explicit profiling build/run.
Review confirmed no terrain scheduling, geometry, shader, ownership or rendering behavior changes. Normal graphics
tests and the interactive application retain validation; only the skipped profile permits it to be disabled.

Evidence: `artifacts/terrain/release-profile/{baseline,instrumented,validation}/` contains each run's frame CSV,
function CSV and log. `summary.json`, `metadata.json` and `ctest.log` preserve analysis and configuration. Reproduce
with the Release function-profiling section in DEVELOPING.md and `tools/terrain-profile-summary.py`.
