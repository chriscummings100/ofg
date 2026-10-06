# Transvoxel tables

`Transvoxel.cpp` is copied unchanged from Eric Lengyel's MIT-licensed tables at
revision `51a494f03c5b024cd153b596bcc7152eb3cc93a6`:
https://github.com/EricLengyel/Transvoxel/blob/51a494f03c5b024cd153b596bcc7152eb3cc93a6/Transvoxel.cpp

The tables are included privately inside the OFG mesher's namespace. Do not format the vendored file.
The algorithm specification is https://transvoxel.org/.

The face-space orientation and fine-side transition convention were also checked against Zylann's
MIT-licensed Godot Voxel implementation, revision `045ff9326a32fd10ac6fdac6357b5a098aaf2093`,
`meshers/transvoxel/transvoxel.cpp`; its notice is retained in REFERENCE-LICENSE.md.
No Godot runtime, build system, material system or vertex-cache implementation is imported.

OFG emits transition geometry in the shared face plane. It does not apply the optional tangent displacement
used to soften transition strips: node bounds remain exact, and abrupt LOD shape changes are permitted in
this laboratory. Surface topology and all face/edge/corner joins must still be verified.
