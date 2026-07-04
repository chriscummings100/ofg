# Latest Coverage

Date: 2026-07-04

Commands: `npm run coverage`.

Result: passed. The C++ coverage wrapper reported that checked animation/core/debug registry/gpu/runtime/math/render/resource/scene files met the 90% per-file line coverage gate. Newly covered debug registry files are `cpp/src/debug/debug_menu.cpp` at 97.92% and `cpp/src/debug/debug_scalars.cpp` at 92.31%; `cpp/src/debug/debug_ui.cpp` is a documented smoke-tested exception because it depends on Dear ImGui and WebGPU command encoding. The TypeScript coverage wrapper reported that checked TypeScript files met the 90% per-file line coverage gate.

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
| `cpp/src/debug/debug_menu.cpp` | 97.92% (377/385) |
| `cpp/src/debug/debug_scalars.cpp` | 92.31% (108/117) |
| `cpp/src/game/render_target.cpp` | 100.00% (18/18) |
| `cpp/src/gpu/common.cpp` | 92.31% (96/104) |
| `cpp/src/math/mat.cpp` | 100.00% (48/48) |
| `cpp/src/math/quat.cpp` | 100.00% (95/95) |
| `cpp/src/math/transform.cpp` | 100.00% (122/122) |
| `cpp/src/render/bloom_pass.cpp` | 90.04% (461/524, 12 defensive lines excluded) |
| `cpp/src/render/bloom_settings.cpp` | 100.00% (97/97) |
| `cpp/src/render/bootstrap_scene.cpp` | 100.00% (3/3) |
| `cpp/src/render/bounds.cpp` | 100.00% (74/74) |
| `cpp/src/render/camera_properties.cpp` | 95.74% (45/47) |
| `cpp/src/render/demo_scene.cpp` | 92.96% (396/426) |
| `cpp/src/render/depth_target.cpp` | 96.30% (78/81) |
| `cpp/src/render/draw_list.cpp` | 96.77% (60/62) |
| `cpp/src/render/frustum.cpp` | 100.00% (66/66) |
| `cpp/src/render/lighting.cpp` | 100.00% (26/26) |
| `cpp/src/render/opaque_pass.cpp` | 92.98% (424/456) |
| `cpp/src/render/opaque_pbr_shader.cpp` | 100.00% (17/17) |
| `cpp/src/render/pipeline_cache.cpp` | 96.72% (118/122) |
| `cpp/src/render/render_object.cpp` | 96.36% (53/55) |
| `cpp/src/render/renderer_counters.cpp` | 100.00% (9/9) |
| `cpp/src/render/renderer.cpp` | 92.61% (376/406) |
| `cpp/src/render/scene_color_target.cpp` | 94.17% (113/126, 6 defensive lines excluded) |
| `cpp/src/render/shadow_cascade.cpp` | 93.01% (213/229) |
| `cpp/src/render/shadow_caster_pass.cpp` | 97.00% (356/395, 28 defensive lines excluded) |
| `cpp/src/render/shadow_debug_pass.cpp` | 97.18% (207/238, 25 defensive lines excluded) |
| `cpp/src/render/shadow_frame_state.cpp` | 95.83% (69/72) |
| `cpp/src/render/shadow_map_target.cpp` | 94.82% (183/206, 13 defensive lines excluded) |
| `cpp/src/render/shadow_settings.cpp` | 94.81% (73/77) |
| `cpp/src/render/sky_pass.cpp` | 92.54% (248/284, 16 defensive lines excluded) |
| `cpp/src/render/temp_buffer.cpp` | 93.78% (377/402) |
| `cpp/src/render/tone_map_pass.cpp` | 92.29% (323/364, 14 defensive lines excluded) |
| `cpp/src/resources/material.cpp` | 90.71% (205/226) |
| `cpp/src/resources/mesh.cpp` | 93.93% (201/214) |
| `cpp/src/resources/property_bag.cpp` | 94.56% (139/147) |
| `cpp/src/resources/resource_error.cpp` | 93.75% (15/16) |
| `cpp/src/resources/resource.cpp` | 96.67% (58/60) |
| `cpp/src/resources/resources.cpp` | 91.19% (435/477) |
| `cpp/src/resources/shader.cpp` | 94.12% (128/136) |
| `cpp/src/resources/texture.cpp` | 93.00% (226/243) |
| `cpp/src/runtime/runtime_debug_status.cpp` | 100.00% (180/180) |
| `cpp/src/scene/animation_player.cpp` | 91.26% (282/309) |
| `cpp/src/scene/camera.cpp` | 91.84% (225/245) |
| `cpp/src/scene/component.cpp` | 100.00% (10/10) |
| `cpp/src/scene/entity.cpp` | 91.43% (64/70) |
| `cpp/src/scene/environment.cpp` | 94.89% (223/235) |
| `cpp/src/scene/light.cpp` | 100.00% (32/32) |
| `cpp/src/scene/mesh_renderer.cpp` | 94.06% (190/202) |
| `cpp/src/scene/player.cpp` | 90.30% (326/361) |
| `cpp/src/scene/scene.cpp` | 90.73% (274/302) |

C++ exceptions:

- `cpp/src/assets/`: glTF parsing/importing is fixture-matrix code covered by focused glTF/model/skinning/player asset tests plus browser/native smoke, rather than the per-file native line gate.
- `cpp/src/debug/debug_ui.cpp`: Dear ImGui/WebGPU bridge code; covered by `npm run smoke:browser`, `npm run smoke:browser:cpp`, and `npm run smoke:render` because its useful behavior requires a live ImGui context, WebGPU backend, command encoder, and final render target.
- `cpp/src/game/game.cpp`: device-bound `Game` renderer ownership and command encoding; covered by `npm run build:wasm`, browser smoke, and native smoke rather than native line coverage.
- `cpp/src/web/`: browser-only Emscripten/Embind/WebGPU glue and frame-driver submission; covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke rather than native line coverage.
- `cpp/src/native/`: native Dawn smoke harness; covered by `npm run smoke:render` because the validation value is GPU readback plus PNG/report output.
- `cpp/src/render/bloom_pass.cpp`, `cpp/src/render/scene_color_target.cpp`, `cpp/src/render/shadow_caster_pass.cpp`, `cpp/src/render/shadow_debug_pass.cpp`, `cpp/src/render/shadow_map_target.cpp`, `cpp/src/render/sky_pass.cpp`, and `cpp/src/render/tone_map_pass.cpp`: only narrow defensive WebGPU null-return, impossible tangent overflow, and partial-creation cleanup lines are excluded by `tools/cpp-coverage.mjs`; normal behavior remains gated and smoke-tested.

## TypeScript Line Coverage

| File | Lines |
| --- | ---: |
| `total` | 89.07% (1386/1556) |
| `src/app/canvasHost.ts` | 97.51% (196/201) |
| `src/app/controlInput.ts` | 96.80% (303/313) |
| `src/app/main.ts` | 0.00% (0/88, documented exception) |
| `src/app/wasmRuntime.ts` | 92.97% (887/954) |

TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.
