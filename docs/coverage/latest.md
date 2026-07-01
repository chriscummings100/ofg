# Latest Coverage

Date: 2026-06-30

Commands: `npm run coverage`.

Result: passed. The C++ coverage wrapper reported that checked C++ core/gpu/runtime/math/render/resource/scene files met the 90% per-file line coverage gate, and the TypeScript coverage wrapper reported that checked TypeScript files met the 90% per-file line coverage gate.

Generated local artifacts:

- `artifacts/coverage/cpp/cpp-summary.json`
- `artifacts/coverage/ts/coverage-summary.json`

Committed summary copies:

- `docs/coverage/cpp-summary.json`
- `docs/coverage/ts-coverage-summary.json`

## C++ Line Coverage

| File | Lines |
| --- | ---: |
| `cpp/src/core/frame_state.cpp` | 100.00% (10/10) |
| `cpp/src/game/debug_camera_controller.cpp` | 91.45% (107/117) |
| `cpp/src/game/render_target.cpp` | 100.00% (18/18) |
| `cpp/src/gpu/common.cpp` | 96.00% (96/100) |
| `cpp/src/math/mat.cpp` | 100.00% (48/48) |
| `cpp/src/math/quat.cpp` | 100.00% (104/104) |
| `cpp/src/math/transform.cpp` | 100.00% (71/71) |
| `cpp/src/render/bootstrap_scene.cpp` | 100.00% (3/3) |
| `cpp/src/render/camera_properties.cpp` | 95.74% (45/47) |
| `cpp/src/render/demo_scene.cpp` | 94.78% (218/230) |
| `cpp/src/render/draw_list.cpp` | 96.77% (60/62) |
| `cpp/src/render/opaque_pass.cpp` | 91.24% (250/274) |
| `cpp/src/render/pipeline_cache.cpp` | 96.58% (113/117) |
| `cpp/src/render/renderer.cpp` | 90.27% (167/185) |
| `cpp/src/resources/material.cpp` | 91.57% (228/249) |
| `cpp/src/resources/mesh.cpp` | 95.00% (171/180) |
| `cpp/src/resources/property_bag.cpp` | 94.56% (139/147) |
| `cpp/src/resources/resource_error.cpp` | 93.75% (15/16) |
| `cpp/src/resources/resources.cpp` | 93.49% (158/169) |
| `cpp/src/resources/shader.cpp` | 94.84% (147/155) |
| `cpp/src/resources/texture.cpp` | 93.73% (254/271) |
| `cpp/src/runtime/runtime_debug_status.cpp` | 100.00% (73/73) |
| `cpp/src/scene/camera.cpp` | 100.00% (58/58) |
| `cpp/src/scene/component.cpp` | 100.00% (10/10) |
| `cpp/src/scene/entity.cpp` | 94.23% (49/52) |
| `cpp/src/scene/mesh_renderer.cpp` | 100.00% (28/28) |
| `cpp/src/scene/scene.cpp` | 95.72% (179/187) |

C++ exceptions:

- `cpp/src/game/game.cpp`: device-bound `Game` renderer ownership and command encoding; covered by `npm run build:wasm`, browser smoke, and native smoke rather than native line coverage.
- `cpp/src/web/`: browser-only Emscripten/Embind/WebGPU glue and frame-driver submission; covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke rather than native line coverage.
- `cpp/src/native/`: native Dawn smoke harness; covered by `npm run smoke:render` because the validation value is GPU readback plus PNG/report output.

## TypeScript Line Coverage

| File | Lines |
| --- | ---: |
| `total` | 85.41% (621/727) |
| `src/app/canvasHost.ts` | 97.51% (196/201) |
| `src/app/debugInput.ts` | 96.20% (152/158) |
| `src/app/main.ts` | 0.00% (0/83), documented exception |
| `src/app/wasmRuntime.ts` | 95.78% (273/285) |

TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.
