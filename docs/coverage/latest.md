# Latest Coverage

Date: 2026-07-02

Commands: `npm run coverage`.

Result: passed. The C++ coverage wrapper reported that checked animation/core/gpu/runtime/math/render/resource/scene files met the 90% per-file line coverage gate, and the TypeScript coverage wrapper reported that checked TypeScript files met the 90% per-file line coverage gate.

Generated local artifacts:

- `artifacts/coverage/cpp/cpp-summary.json`
- `artifacts/coverage/ts/coverage-summary.json`

Committed summary copies:

- `docs/coverage/cpp-summary.json`
- `docs/coverage/ts-coverage-summary.json`

## C++ Line Coverage

| File | Lines |
| --- | ---: |
| `cpp/src/animation/animation_clip.cpp` | 100.00% (29/29) |
| `cpp/src/core/control_input.cpp` | 100.00% (6/6) |
| `cpp/src/core/frame_state.cpp` | 100.00% (10/10) |
| `cpp/src/core/object.cpp` | 100.00% (37/37) |
| `cpp/src/game/render_target.cpp` | 100.00% (18/18) |
| `cpp/src/gpu/common.cpp` | 96.00% (96/100) |
| `cpp/src/math/mat.cpp` | 100.00% (48/48) |
| `cpp/src/math/quat.cpp` | 100.00% (95/95) |
| `cpp/src/math/transform.cpp` | 100.00% (103/103) |
| `cpp/src/render/bootstrap_scene.cpp` | 100.00% (3/3) |
| `cpp/src/render/camera_properties.cpp` | 95.74% (45/47) |
| `cpp/src/render/demo_scene.cpp` | 92.47% (270/292) |
| `cpp/src/render/draw_list.cpp` | 96.77% (60/62) |
| `cpp/src/render/opaque_pass.cpp` | 92.22% (308/334) |
| `cpp/src/render/opaque_pbr_shader.cpp` | 100.00% (17/17) |
| `cpp/src/render/pipeline_cache.cpp` | 96.67% (116/120) |
| `cpp/src/render/renderer.cpp` | 90.22% (166/184) |
| `cpp/src/resources/material.cpp` | 90.71% (205/226) |
| `cpp/src/resources/mesh.cpp` | 93.72% (194/207) |
| `cpp/src/resources/property_bag.cpp` | 94.56% (139/147) |
| `cpp/src/resources/resource_error.cpp` | 93.75% (15/16) |
| `cpp/src/resources/resource.cpp` | 96.67% (58/60) |
| `cpp/src/resources/resources.cpp` | 92.68% (532/574) |
| `cpp/src/resources/shader.cpp` | 94.12% (128/136) |
| `cpp/src/resources/texture.cpp` | 93.00% (226/243) |
| `cpp/src/runtime/runtime_debug_status.cpp` | 100.00% (78/78) |
| `cpp/src/scene/animation_player.cpp` | 91.26% (282/309) |
| `cpp/src/scene/camera.cpp` | 91.84% (225/245) |
| `cpp/src/scene/component.cpp` | 100.00% (10/10) |
| `cpp/src/scene/entity.cpp` | 95.31% (61/64) |
| `cpp/src/scene/mesh_renderer.cpp` | 94.06% (190/202) |
| `cpp/src/scene/player.cpp` | 91.43% (320/350) |
| `cpp/src/scene/scene.cpp` | 91.97% (275/299) |

C++ exceptions:

- `cpp/src/assets/`: glTF parsing/importing is fixture-matrix code covered by focused glTF/model/skinning/player asset tests plus browser/native smoke, rather than the per-file native line gate.
- `cpp/src/game/game.cpp`: device-bound `Game` renderer ownership and command encoding; covered by `npm run build:wasm`, browser smoke, and native smoke rather than native line coverage.
- `cpp/src/web/`: browser-only Emscripten/Embind/WebGPU glue and frame-driver submission; covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke rather than native line coverage.
- `cpp/src/native/`: native Dawn smoke harness; covered by `npm run smoke:render` because the validation value is GPU readback plus PNG/report output.

## TypeScript Line Coverage

| File | Lines |
| --- | ---: |
| `total` | 86.15% (759/881) |
| `src/app/canvasHost.ts` | 97.51% (196/201) |
| `src/app/controlInput.ts` | 96.40% (161/167) |
| `src/app/main.ts` | 0.00% (0/84), documented exception |
| `src/app/wasmRuntime.ts` | 93.70% (402/429) |

TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.
