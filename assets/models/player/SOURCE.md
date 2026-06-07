Quaternius player test assets
=============================

Source: https://quaternius.com/packs/universalbasecharacters.html
Source: https://quaternius.com/packs/universalanimationlibrary.html
Source: https://quaternius.com/packs/universalanimationlibrary2.html
Downloaded from the Quaternius itch.io standard free packs on 2026-06-06
and 2026-06-07.

Files:

- `quaternius-superhero-male.glb`: converted from `Universal Base Characters[Standard]/Base Characters/Godot - UE/Superhero_Male_FullBody.gltf` plus its external `.bin`. Texture image URIs remain in the source JSON, but the current OFG importer uses material factors and embedded mesh buffers only.
- `quaternius-superhero-female.glb`: converted from `Universal Base Characters[Standard]/Base Characters/Godot - UE/Superhero_Female_FullBody.gltf` plus its external `.bin`. Texture image URIs remain in the source JSON, but the current OFG importer uses material factors and embedded mesh buffers only.
- `quaternius-ual1-standard.glb`: copied from `Universal Animation Library[Standard]/Unreal-Godot/UAL1_Standard.glb` for the shared humanoid skeleton and base locomotion clips such as `Idle_Loop`, `Walk_Loop`, and `Sprint_Loop`.
- `quaternius-ual2-standard.glb`: copied from `Universal Animation Library 2[Standard]/Unreal-Godot/UAL2_Standard.glb` for the shared humanoid skeleton, mannequin mesh, and earlier idle/walk animation tests.

Note: the requested Regular male/female full-body models are not present in the
free Standard base-character zip currently downloaded under
`artifacts/quaternius-downloads/`. The checked-in male/female Superhero bodies
are temporary same-rig placeholders until Regular GLBs are available.

License: CC0 1.0 Universal (Public Domain Dedication).
Original assets by Quaternius.
