# Environment filter provenance

`filter.slang` ports the Lambertian/GGX/Charlie sampling equations used by `tools/bake-pbr.py` to raster passes.
Those equations adapt Khronos glTF-Sample-Renderer `source/shaders/ibl_filtering.frag` at
cc27919cacbb235d2f58a0c0203387efce9375f8. Copyright The Khronos Group Inc., Apache-2.0;
see [preserved license](../pbr/LICENSE-Khronos.txt) and [original adaptation notice](../pbr/NOTICE.md).
The Hammersley radical inverse derives from Holger Dammertz, CC BY 3.0:
http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html .

OFG samples its generated disk-free sky capture, uses 128 samples and writes each filter/face/mip through a raster
pass. Face mapping, generation publication and RHI binding code are OFG code. This pass omits the reference's
source-image mip selection because the small sky capture has one mip.
