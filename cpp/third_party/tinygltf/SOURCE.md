# tinygltf source snapshot

This directory vendors the tinygltf glTF 2.0 loader so OFG can build its C++
GLTF importer without a runtime package manager dependency.

- Upstream: https://github.com/syoyo/tinygltf
- Snapshot commit: `a434ee02066c2d9b62a3504876aed38e6e399fe0`
- Snapshot date: 2026-07-02
- License: MIT; see `LICENSE`.

Copied files:

- `tiny_gltf.h`
- `tiny_gltf.cc`
- `json.hpp`
- `stb_image.h`
- `stb_image_write.h`
- `README.md`
- `LICENSE`

The importer implementation should compile tinygltf from one OFG source file
or the copied `tiny_gltf.cc`, so `TINYGLTF_IMPLEMENTATION`,
`STB_IMAGE_IMPLEMENTATION`, and `STB_IMAGE_WRITE_IMPLEMENTATION` are defined in
exactly one translation unit.
