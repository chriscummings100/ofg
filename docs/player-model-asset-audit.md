# Player Model Asset Audit

This document records the Milestone 1A audit for the Quaternius player assets under `C:\dev\ofg\assets\models\player`. The native tests in `C:\dev\ofg\cpp\tests\player_asset_audit_test.cpp` lock down the key facts used by later importer, animation, and browser-loading work.

## Selected Assets

Default player mesh: `quaternius-superhero-male.glb`.

Default animation library: `quaternius-ual1-standard.glb`.

Rationale: the male superhero GLB has the intended skinned character mesh and PBR texture set, while UAL1 has the locomotion clips needed for the first player controller: `Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, and `Sprint_Loop`. `quaternius-ual2-standard.glb` has a compatible skeleton but does not contain `Sprint_Loop`, so it is useful as a future secondary library rather than the first locomotion source.

## Mesh And Material Requirements

`quaternius-superhero-male.glb` has 69 nodes, 3 meshes/primitives, 3 materials, 7 images/textures, 1 skin, 65 joints, and no embedded animations. All mesh primitives are triangle lists and include `POSITION`, `NORMAL`, `TEXCOORD_0`, `JOINTS_0`, and `WEIGHTS_0`.

The mesh does not include `TANGENT`. Because all three materials use normal textures, Milestone 3 must generate tangents from positions, normals, and UVs before normal mapping is considered correct.

Material texture slots found in the selected player mesh: 3 base-color textures, 3 normal textures, and 1 metallic-roughness texture. The importer should treat base color as sRGB and normal/metallic-roughness as linear.

## Skeleton And Animation Compatibility

The selected player mesh and both UAL animation libraries have the same 65 joint names in the same skin order. `skin.skeleton` is absent (`-1`) in these files, so later `SkinBinding` work should keep the skeleton root optional and derive a practical skeleton pivot/root from the imported node tree when needed.

UAL1 has 45 animations. All audited animation samplers use `LINEAR` interpolation. Channels target `translation`, `rotation`, and `scale`; UAL1 has 2925 channels for each path.

UAL2 has 43 animations and the same skeleton, but lacks `Sprint_Loop`.

## Memory Notes

The selected mesh and animation GLBs together are 23,593,976 bytes on disk. Their decoded glTF buffer bytes plus decoded player image bytes add roughly 105,986,224 more bytes, for a source-plus-decoded-data estimate of 129,580,200 bytes before importer conversion, renderer resources, GPU upload staging, or GPU texture memory.

The current browser build uses `INITIAL_MEMORY=33554432` and `ALLOW_MEMORY_GROWTH=0`, so Milestone 8 must deliberately raise the WASM memory budget and/or enable controlled growth before loading the player assets in the browser.
