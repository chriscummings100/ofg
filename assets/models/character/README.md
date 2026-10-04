# Quaternius superhero and UAL1

Copied byte-for-byte on 2026-10-04 from `C:/dev/ofg-old2/assets/models/player`. The historical directory was left untouched. Original assets by Quaternius, licensed CC0 1.0 Universal (Public Domain Dedication).

The original provenance notes are preserved unchanged in [ORIGINAL-SOURCE.md](ORIGINAL-SOURCE.md). That historical note also describes female/UAL2 files which are not included here. The male GLB was converted in the old demo from the Standard Universal Base Characters Godot/UE glTF, embedding its buffer and seven PNG images. UAL1 was copied there from the Standard Universal Animation Library Unreal/Godot GLB.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| quaternius-superhero-male.glb | 15479612 | 23c373996e3ac6609492c3a9e1e5870e73b38f6d94afc246afe86cd6e13d2a66 |
| quaternius-ual1-standard.glb | 8114364 | d867292451e432b735e2a910c2db6640fbea97b205d85a2e8ffed26da87972cf |

The UAL1 file contains 45 named clips, including Idle_Loop, Walk_Loop and Sprint_Loop. Its animation channels contain only translation, rotation and scale. The superhero has 69 nodes and one skin; UAL1 has 67 nodes and one skin. These counts were inspected from their GLB JSON chunks; they do not establish runtime import or animation correctness.

The character fixture maps by joint name and validates hierarchy. These rigs differ in rest transforms and proportions.
Direct mapping reproduces the old demo; it is not proper retargeting. See the [character guide](../../../docs/animation.md)
for launch modes and controls, and the [animation plan](../../../docs/archived/gltf-animation.md) for validation evidence.
