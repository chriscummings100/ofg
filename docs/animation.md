# Animated character laboratory

Run `build/native/ofg.exe --character`, or open `?demo=character` after the web build. Both load the preserved CC0
Quaternius superhero male and UAL1 library asynchronously, instantiate only the superhero, and explicitly play
`Idle_Loop`. See [asset provenance](../assets/models/character/README.md) and [build commands](../DEVELOPING.md).
The PBR default and generic `--model` / `?demo=model` modes remain available. Generic model instantiation never autoplays.

Open the **Animation** tab beside Render Settings. Choose an animator and a clip, then use Play/Pause, Stop, Looping,
Speed and Time. Every clip label includes its source index, including unnamed or duplicate names. Scrubbing pauses
playback. Stop samples time zero; selecting a different clip rewinds while preserving play/pause state. Zero speed
freezes advancement. Non-looping clips hold their final pose, and Play restarts them.

The fixture uses the existing fly camera and PBR lighting. Right mouse plus WASDQE flies; Shift accelerates. R or F
restores the character view at (0,1,-4). `--character-pair` or `?demo=character&instances=2` creates two placements
sharing assets but with independent animators and private compute outputs. The second starts half a clip later at
0.6 speed. Use animator selection to pause/scrub either instance independently.

UAL1 node indices map directly to superhero entities by unique name and corresponding parent. All 45 immutable clips
are reused without copying keys. **This is direct mapping, not retargeting.** The rigs differ in rest transforms and
proportions, so contact, limb alignment and deformation may be imperfect. No crossfade, IK, character controller,
follow camera or root-motion extraction is present. Authored root motion stays ordinary node animation under the
placement root. Clips with morph weight tracks are rejected explicitly.

Local pose sampling and entity application are separate GPU-independent functions. Every sample begins with the
captured target rest pose, preventing clip-property leakage. State updates precede animation, then world transforms,
palette extraction, compute deformation and draw. This local-pose boundary permits future blending/IK without changing
the clip resources or renderer. Hidden scene viewports still advance animation.

The native GPU suite checks packed output numerically and renders actual clips. The browser smoke drives canvas UI,
records diagnostics and saves screenshots/video under `artifacts/animation/browser`; native pose sequences and window
captures are under `artifacts/animation/native`. Completed acceptance and evidence are recorded in the
[archived plan](archived/gltf-animation.md). No performance improvement is claimed.
