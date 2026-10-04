# tinygltf

Vendored unmodified from syoyo/tinygltf v2.9.6, commit
`26422192e2908a562b641175dde18489824e609e`, on 2026-10-04:
https://github.com/syoyo/tinygltf/tree/26422192e2908a562b641175dde18489824e609e

`tiny_gltf.h` and `LICENSE` are MIT licensed. The matching `json.hpp` includes its
nlohmann/json MIT notice. OFG compiles the loader implementation once, disables
filesystem/image defaults, and supplies prepared bytes and its existing stb decoder.
No upstream examples, writer application or graphics dependency is built.
