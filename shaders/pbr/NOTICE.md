# PBR shader provenance

`brdf.slang` and `iridescence.slang` closely translate the corresponding Khronos glTF-Sample-Renderer files at cc27919cacbb235d2f58a0c0203387efce9375f8. Material/layer assembly, IBL and neutral tone mapping adapt `pbr.frag`, `material_info.glsl`, `ibl.glsl`, `punctual.glsl` and `tonemapping.glsl` from that revision. Copyright The Khronos Group Inc. Licensed under Apache-2.0; see `LICENSE-Khronos.txt`.

GLSL constructors and mix become Slang types/lerp; the iridescence matrix is explicitly transposed. OFG bounds zero roughness evaluation, guards degenerate vectors, and uses exact sRGB output. Geometry, host bindings, output composition and procedural fixtures are OFG code.

`tools/bake-pbr.py` adapts the importance sampling and lookup equations in `source/shaders/ibl_filtering.frag`. Hammersley radical inverse derives from Holger Dammertz, CC BY 3.0: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html . The adaptation evaluates an analytic studio environment rather than sampling an input image mip chain. Its generated fixture contains no third-party image content.
