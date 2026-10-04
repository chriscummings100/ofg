# Animate the superhero through portable compute skinning

This completed ExecPlan follows [PLANS.md](../../PLANS.md). The user authorized implementation in a dedicated worktree on 2026-10-04, then continuation through the remaining milestones. Sampling, transport, direct character mapping, compute deformation and controls are implemented and verified on both targets. The plan was archived after final acceptance on 2026-10-04.

## Purpose / Big Picture

Load the Quaternius superhero male and Universal Animation Library 1 (UAL1), then inspect all 45 clips with an Animation workspace panel on native D3D12 and browser WebGPU. The fixture explicitly starts Idle_Loop. Ordinary model instances remain paused. Both hosts keep the PBR default and generic model mode; character entry points are `--character` and `?demo=character`.

Direct mapping by joint name reproduces the old demonstration. Matching names and parent relationships do not imply matching rest transforms or proportions. This milestone deliberately does not retarget, blend, solve IK, extract root motion, or add a character controller/follow camera. Authored root motion stays under a synthetic placement root. Use the existing fly camera and PBR lighting, with reset framing the character.

## Progress

- [x] (2026-10-04) Created managed worktree `C:/Users/Chris/.codex/worktrees/gltf-animation/ofg`, branch `gltf-animation`, based on committed `c950f6c`. Preserved uncommitted outdoor-lighting work in `C:/dev/ofg`.
- [x] (2026-10-04) Read repository/build instructions and inspected importer, passive Animator, model instantiation, mesh layout and upstream RHI compute interfaces.
- [x] (2026-10-04 12:22Z) Preserved both GLBs byte-for-byte with original provenance/CC0 notes; importer tests verify 45 TRS clips. Node-name propagation and instance rename isolation pass CPU regression tests.
- [x] (2026-10-04 12:38Z) Milestone 1: command-ordered uploads, 64-thread compute, output transition, vertex consumption and native readback pass. The shared RHI/Emscripten browser proof visibly renders left/right/left-again with no errors or validation warnings; captures inspected.
- [x] (2026-10-04) Milestone 2: local TRS poses, glTF interpolation, double-precision transport, rest-pose reset and state/animation/world update ordering implemented; focused CPU tests pass, native hidden-target ordering passes.
- [x] (2026-10-04) Milestone 3: validated mapping, mesh-local palette snapshots, all-influence compute skinning and asynchronous character loading render the superhero on D3D12 and WebGPU. Native actual-clip and numerical GPU cases pass; browser idle motion and stable pause checks pass.
- [x] (2026-10-04) Milestone 4: Animation controls and both launch modes complete. Full native/browser acceptance and model/UI/PBR/texture regressions pass; screenshots, motion video and independent-instance evidence are saved. Final web rebuild, character startup and UI smoke pass after scene-replacement/layout fixes.

## Surprises & Discoveries

2026-10-04 continuation: native weighted skinning tests pass (six influences, mirrored nonuniform scale, exact UV/color preservation, singular normals, separate outputs, outside-rest-bounds visibility and graphics reinitialization). Actual superhero captures cover idle/walk/sprint/punch and independently paused instances. The first full native run hit the CPU suite's 30-second timeout during concurrent WASM optimization, and one stale layout-header expectation failed after adding Animation visibility. After correcting the expectation and removing link contention, CPU CTest passes in 12.84 seconds and all four native CTest targets pass. The web build completed; motion recording required the pinned Playwright CLI's ffmpeg v1011 helper.

Browser automation needed to drain ImGui's trickled input events between numeric characters. Sending long strings at once left actions queued; holding arrow keys across slow submitted frames triggered repeat. Short presses followed by frame-based draining fixed the smoke without changing playback behavior. The legacy docking smoke hides Animation while testing the original three-panel geometry, because detaching Render Settings now legitimately leaves Animation in the right dock. Final review caught and fixed the full-canvas character fly-input gate, stale fixture readiness after Scene replacement, and default Animation visibility when re-reading an older layout.

The worktree tool initially used remote `main` at e811a3d, which predates model loading. The clean worktree was switched to local c950f6c before edits. No dirty changes from the primary checkout were copied.

Initial inspection found that `Scene::instantiateModel` transferred transforms but omitted node display names; the first checkpoint fixed this. Animator was initially passive; milestone 2 added playback through local poses. `Vertex` uses 18 scalar floats (72 bytes); the proof and deformation implementation assert all offsets and use byte-address storage instead of relying on storage-buffer float3 alignment.

The copied GLBs load through the real importer. A separate JSON audit found 65 unique animated source nodes, no missing target names, no duplicate target names and no corresponding-parent mismatches. Runtime validation now checks these contracts; tests also bind and sample all 45 imported clips. The first native compile exposed use of an unavailable doctest Approx::margin API; replacing it with an explicit absolute-error comparison preserves the requested 1e-4 tolerance. The first formatter command used the wrong LLVM directory; the documented x64 path succeeded with clang-format 22.1.3.

The first WebGPU proof failed inside emdawnwebgpu bind-group construction. A shared source module kept compute globals in the draw program's reflected layout, leaving unfilled buffer bindings during rasterization. Separate preprocessor-selected compute/draw modules remove those globals from the draw layout. Slang also needs different source paths for those different module contents; reusing the source path triggered its dictionary assertion natively. With distinct paths, both native and browser proofs pass. The initial failure stack is retained in `artifacts/animation/browser/compute-startup-failure.json`. An obsolete in-progress proof link was explicitly stopped after that source-path bug was found; the subsequent final build completed normally. No SDK/cache or upstream files were changed.

## Decision Log

2026-10-04: Instance storage owns output and palette buffers; the shared Mesh owns immutable source vertices and packed influences. A focused DeformationRenderer submits upload/compute/transition commands on the graphics queue before render passes. The native oracle checks analytic affine results rather than duplicating the shader algorithm. Four storage buffers and 64-thread groups stay within the portable profile; use a conservative 128 MiB storage binding cap because pinned RHI exposes maxBufferSize but not maxStorageBufferBindingSize.

2026-10-04: Add `--character-pair` and `?demo=character&instances=2` as a small independence diagnostic. It uses the same fixture with two placements and shared clip/mesh assets; the second starts halfway through idle at 0.6 speed. No separate scene system or test-only runtime animation control API is introduced. Browser smoke manipulates the rendered panel and only reads transport diagnostics.

2026-10-04: Extend current immutable Animation/Skin/Model descriptions and instance observers. Do not import the old animation implementation. Preserve the historical directory unchanged.

2026-10-04: Begin with an intentionally small translation compute proof before implementing weighted skinning. Its test-only shader is not a runtime deformation fallback. Share one RHI helper between native GPU tests and a separate minimal Emscripten proof host, so the experiment exercises the real backend without adding temporary modes to the normal application.

2026-10-04: Retain the existing CPU test workflow and behavioral coverage policy. No arbitrary numerical coverage threshold or performance claim is introduced. Numerical tests must independently calculate expected results.

## Outcomes & Retrospective

Merge integration on 2026-10-04 preserves the newer outdoor-lighting renderer and both host launch modes. Scene
and shadow draws now share each deformation job/output, and cascade culling also bypasses skinned rest bounds.
The native merge regression translates a cube from rest coordinates 10,000 metres away into the visible shadow
region, checks exact agreement with an independently translated static caster, and checks disagreement with the
unanimated caster. The first test draft incorrectly assumed a cube had one submesh; it now covers all six faces.
Merged CPU-only and native-debug configure/build/CTest pass (110 CPU cases/11,218 assertions; 29 scene/GPU
cases/2,598 assertions plus checkerboard/startup). Browser build, full animation, outdoor, UI, PBR and model smokes pass.
Fresh native/browser character and outdoor captures were inspected. Evidence is under `C:/dev/ofg/artifacts/animation`, including `merge-cpu-tests.log` and
`merge-native-tests.log`; original worktree evidence remains preserved.

The preserved character now renders with compute skinning on both backends. CPU sampling, transport, binding and palette tests pass (104 cases, 11,135 assertions). Native actual-clip captures show idle, walk, sprint, punch and a two-instance demonstration; paused frames compare exactly and resumed poses differ. All four native CTest targets passed, followed by focused lifetime/layout checks and the final scene-replacement/layout regression (2 cases, 97 assertions). Full browser acceptance covers clip controls, scrubbing, pause/resume, independent instances, hidden viewport advancement, resize/reload, full-canvas fly/reset and loading cancellation. Model, UI, PBR and texture/checkerboard smokes pass. After the last lifecycle/layout fixes, the final web rebuild, character startup and UI smoke also pass. The first checkpoint's separate compute proof remains as a reproducible backend regression. Direct mapping still has the documented rest-transform/proportion limitations; no retargeting or performance improvement is claimed.

## Contract and Quality Baseline

Animation sampling is GPU independent. `AnimationPose` contains local TRS in source-node order. Binding captures target rest transforms. Every evaluation copies that rest pose and replaces only authored properties, preventing prior-clip state from leaking. Sampling does not mutate entities or build matrices; ordinary pose application changes entities afterward. The sequence is playback time, local pose sample, future blending, future IK, entity application, world transforms, palettes, GPU deformation, draw.

Animator adds `selectAnimation(size_t)`, `play()`, `pause()`, `stop()`, `seek(double)`, `setLooping(bool)` and `setPlaybackSpeed(float)`, plus selected clip, time, duration, playing, looping and speed getters. Selection rewinds/samples and preserves play state; binding resets playback. Play restarts an ended non-looping clip; stop pauses and samples zero. Seek clamps and samples immediately. Speed is finite/nonnegative; zero freezes advancement. Double-precision elapsed time handles arbitrary large loop deltas; non-looping clips hold their final pose, and zero-duration clips stay at zero. Weight-track selection fails explicitly.

STEP, LINEAR and CUBICSPLINE follow the [glTF animation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations) and its interpolation appendix. Clamp each track independently outside its keys. Linear rotations take the shortest quaternion path; cubic tangents scale with the key interval, then normalize rotations. Reject invalid evaluated quaternions before publishing a pose. `Scene::updateAnimations(deltaSeconds)` runs after states and before world transforms, even without a visible viewport.

Cross-rig binding uses source node indices mapped to superhero entities. Validate unique names, every animated target and corresponding parents with useful diagnostics. Reuse UAL1 clip handles, never keyframe copies. Only the superhero is instantiated; both resources load asynchronously with the existing cancellation/publication contract.

DrawList stores one deformation job per affected MeshRenderer; all its submeshes reference that job. Snapshot `inverse(meshWorld) * jointWorld * inverseBindMatrix` during extraction. Retain assets and opaque instance storage, never entity pointers. Fail on expired joints or singular mesh transforms. Bypass static AABB culling for skinned geometry until animated bounds exist.

Upload immutable vertices and all influence sets once per shared mesh. Each MeshRenderer owns a distinct output allocation via an opaque handle, reset when its mesh changes. Compute uses 64 threads per group, all imported influence sets, and total-weight normalization. Positions/tangents use the blended affine matrix; normals use its inverse transpose. Correct tangent handedness by determinant sign; preserve zero authored-tangent markers and all UV/color data. Degenerate normals become zero for the geometric-normal path. Define/assert every byte offset. Validate device buffer/dispatch limits and fail unsupported configurations explicitly.

Encode palette uploads, compute and output transitions on the existing graphics queue before rendering. Use command-ordered uploads, not in-flight CPU memory overwrites; no per-frame queue waits. Submitted RHI commands must retain allocations through completion. A CPU oracle is test-only, never an automatic fallback.

## Context and Orientation

`src/resources/animation.*`, `skin.*`, `model.*` and `gltf-*` import immutable clips, inverse binds and all influence sets. `src/scene/animator.*` owns transport; `animation-pose.*` samples/applies local poses and `animation-binding.*` validates cross-rig mapping. `model-instance.cpp` stages hierarchies before publishing them. `mesh-renderer.*` owns joint observers and opaque instance storage. `src/render/draw-list.*` snapshots draws/palettes; `deformation.*` owns compute submission and `graphics.cpp` manages scene passes. `src/game.cpp` orders updates. `src/ui/animation-panel.cpp` implements transport UI. Native/web hosts live in `src/main.cpp` and `src/web-main.cpp`. Contracts are [resources](../resources.md), [state/scene](../state-and-scene.md), and [architecture](../architecture.md).

Assets originate in `C:/dev/ofg-old2/assets/models/player`: `quaternius-superhero-male.glb`, `quaternius-ual1-standard.glb`, and `SOURCE.md`. Preserve original provenance and CC0 notes alongside hashes of the copied files. The old directory is read-only historical input.

## Plan of Work

Milestone 1 proves the risky backend boundary with a known mesh, explicit packing, group-tail bounds checking, native readback and visible rendering on both backends. Test multiple ordered uploads before waiting. Keep experimental proof code clearly separate from finished skinning claims. Verify shared RHI WebGPU before closing this milestone.

Milestone 2 introduces a small pose value and ordinary sampling/application functions. Add Animator transport and Scene update ordering. Tests establish interpolation and transport semantics before GPU integration.

Milestone 3 adds validated name mapping and asynchronous character fixture, then palette extraction and focused deformation implementation/shader. Prove per-instance lifetime and all-influence weighting against CPU expectations. Use the existing material passes and one output per mesh instance.

Milestone 4 adds animator selection, indexed labels for unnamed/duplicate clips, clip dropdown, play/pause, stop, looping, speed and time scrubber; scrubbing pauses. Finish launch/reset integration and native/browser motion acceptance. Update relevant contracts and DEVELOPING with working commands.

## Concrete Steps

Run commands from this worktree, never the dirty primary checkout. Follow [DEVELOPING.md](../../DEVELOPING.md) for Visual Studio x64 setup in every fresh shell, then `cmake --preset cpu-tests`, `cmake --build --preset cpu-tests --parallel 8`, `ctest --preset cpu-tests`. Initialize pinned submodules with `git submodule update --init --recursive`. Use `cmake --preset native-debug`, its build preset and `ctest --preset native-debug` for native integration. Use Emscripten 6.0.0 and the `web` configure/build presets, then `npm.cmd ci`, `npm.cmd run smoke:web`, `node tools/model-smoke.mjs`, `npm.cmd run smoke:ui`, and `npm.cmd run smoke:pbr` for browser regressions. These are existing workflows; new worktree results must be recorded separately from historical passes.

Character launch flags and `node tools/animation-smoke.mjs` are implemented; see [DEVELOPING.md](../../DEVELOPING.md#animation-bring-up). The browser tool uses actual UI input and read-only transport diagnostics, with screenshots, reports and motion video under `artifacts/animation`.

## Milestone Review

Milestone 4 reviewed on 2026-10-04: actual controls drive transport, scrubbing pauses, indexed clip labels remain unambiguous, and both hosts preserve the PBR/model launch modes. Browser diagnostics query the actual RHI device and read transport state without a test-only playback API. Final review fixed fixture readiness after scene replacement, old-layout Animation visibility and full-canvas character camera input. Native screenshots, browser clips and recorded sprint transitions were inspected. All changed C++/shader files pass clang-format 22.1.3, smoke scripts pass syntax checks, and whitespace checks pass. Resource/scene/render/UI contracts and launch commands match implemented behavior. No character controller, retargeter, crossfade, solver, global registry or runtime CPU fallback was introduced.

Milestones 2/3 reviewed on 2026-10-04: binding and clip-selection failures preserve existing transport/pose; local sampling contains no entity writes or GPU matrices. Rest-property restoration and shared-clip independent playback are tested. DrawList contains no entity references. Mesh replacement discards its opaque storage; pending command buffers retain allocations after owners/Graphics release them. Pinned RHI `src/command-buffer.cpp::uploadBufferData` retains an upload-heap handle and emits a retained source/destination copy, and render command recording retains vertex/index buffers. Native tests submit changing palettes to the same instance without intermediate waits and release storage immediately after submission. Native analytic position/direction tolerance is 1e-4; UV/color/zero-tangent preservation is exact. No runtime CPU fallback, callback registry, animation graph or generic component lifecycle was introduced.

Milestone 1 reviewed on 2026-10-04: explicit packing matches Vertex offsets, source geometry remains immutable, uploads and compute/draw share queue order, and test waits occur only before readback. The helper owns RHI handles; no runtime deformation fallback or renderer abstraction was added. Proof code stays in tests and the separate proof executable. Name propagation preserves per-instance identity/ownership and staging atomicity. Function comments, clang-format 22.1.3 checks, local documentation links and git diff whitespace checks pass. The browser failure was fixed at the shader program boundary, with its diagnostic preserved. Native offscreen rendering and browser canvas presentation are verified; this does not prove character playback, weighted skinning, animated bounds or the future instance-retirement implementation.

## Validation and Acceptance

CPU cases cover all interpolation modes, quaternion signs, unequal track ranges, one key, zero duration, loop boundaries, large deltas, seek/pause/stop, clip switching and independent instances. Binding failures cover missing/duplicate names, mismatched hierarchy, unsupported weights and invalid targets. Palette fixtures include nonidentity mesh and parent transforms, inverse binds and independent placements.

Native GPU fixtures independently calculate one-joint/blended outputs, more than four influences, nonuniform scale, normals/tangents and unchanged attributes. Position tolerance is 1e-4 for metre-scale fixtures; direction tolerance is 1e-4; preserved attributes compare exactly. Exercise shared meshes at different times, multiple submeshes, repeated frames, scene replacement and Graphics shutdown/reinitialization. Animated geometry outside rest bounds must remain visible.

Inspect idle, walk, sprint, a one-shot, clip switches and scrubbed poses on both backends. Paused geometry stays stable and resume changes it. Check loading/cancellation, resize/reload, hidden viewport updates and validation diagnostics. Save a two-instance independence demonstration. Builds alone do not satisfy acceptance; report unavailable checks explicitly. No performance improvement is asserted without named hardware, viewport, workload and measurement method.

## Idempotence and Recovery

Reconfigure/rebuild existing per-toolchain trees without deleting caches. Preserve submodule pins and the historical backup. On interrupted asset copies, compare hashes before replacing only the intended destination. Keep pending implementation marked pending. Commit cohesive checkpoints on the worktree branch when appropriate; do not merge into or overwrite the dirty primary checkout.

## Artifacts and Notes

Pinned RHI: 16324a68af477baaede620e713644f5e9613b1a2. ImGui: b48d1afbe8ee8b238e2961dc363a949dd7304e23. glTF specification inspected on 2026-10-04 (page identifies specification revision 8e798b02d254cea97659a333cfcb20875b62bdd4). Add actual validation logs and asset hashes as work proceeds.

Actual checkpoint commands: `cmake --preset cpu-tests`; `cmake --build --preset cpu-tests --parallel 8`; `ctest --preset cpu-tests`; `cmake --preset native-debug`; `cmake --build --preset native-debug --parallel 8`; `ctest --preset native-debug`; `./build/native/ofg-render-test.exe '--test-case=Compute vertex*' --no-colors`; `cmake --preset web`; `cmake --build build/web --target ofg-web ofg-compute-proof-web --parallel 6`; final proof rebuild with `cmake --build build/web --target ofg-compute-proof-web --parallel 6`; `npm.cmd ci`; `node tools/compute-vertex-smoke.mjs`; `node tools/model-smoke.mjs`; `npm.cmd run smoke:ui`; `npm.cmd run smoke:pbr`; `npm.cmd run smoke:web`. All final invocations passed. Native used MSVC 19.51 and NVIDIA GeForce RTX 3050 Ti Laptop GPU; browser used Emscripten 6.0.0 and Chrome 154.0.8037.95. An independent browser adapter query reported Intel gen-12lp. The existing Asyncify/WASM-exception build warning and Windows powerPreference browser warning remain; no performance claim is made.

Evidence: `artifacts/animation/cpu-tests.log`, `native-tests.log`, `native-compute-tests.txt`, `native/compute-0.ppm`, `native/compute-1.ppm` (PNG viewing copies beside them), `browser/compute-report.json`, `browser/compute-left.png`, `browser/compute-right.png`, `browser/compute-left-again.png`. Final browser report has no errors and only the known powerPreference warning. Generated WGSL probes under `artifacts/animation` show three compute buffers, 64-thread groups and resource-free draw inputs at locations 0/1. Other regression reports remain in `artifacts/models/browser`, `artifacts/imgui/browser`, `artifacts/pbr/browser` and `artifacts/textures/browser`. Asset hashes are recorded in `assets/models/character/README.md` and were compared against the unchanged historical originals.

Final character commands: `cmake --build --preset web --parallel 6`; `./build/native/ofg-render-test.exe '--test-case=Animated superhero*,workspace layouts*' --no-colors`; `node node_modules/playwright-core/cli.js install ffmpeg`; `node tools/animation-smoke.mjs`; `node tools/animation-smoke.mjs --initial-only`; `npm.cmd run smoke:ui`. All passed. The initial-only check writes a separate startup report, preserving the full acceptance report/video.

Character evidence includes `native/pair-animation-panel.png`, native Idle_Loop/Walk_Loop/Sprint_Loop/Punch_Cross pose sequences and independent-pair captures, `browser/animation-report.json`, `browser/animation-startup-report.json`, `browser/independent-pair-scrubbed.png`, and `browser/page@79391143b221d5df365e98bc765aba0e.webm` (3 minutes 39 seconds). Viewing frames at 65 and 65.5 seconds shows the sprint transition. The actual RHI WebGPU adapter is Intel gen-12lp, with 16 storage buffers per stage and 65,535 workgroups per dimension; the implementation keeps the portable four-buffer/64-thread/128 MiB profile. These are correctness observations, not a performance benchmark.

## Interfaces and Dependencies

Reuse shared immutable resources and existing observers/RAII. Add no animation graph, solver interface, callback registry or renderer abstraction. Native stays D3D12; browser stays WebGPU; CPU tests stay free of graphics dependencies. The AnimationPose boundary is the future blending/IK extension point, without implementing those systems now.
