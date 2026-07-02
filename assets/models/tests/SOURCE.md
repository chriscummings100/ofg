# GLTF Test Fixtures

These fixtures are small Khronos glTF Sample Assets used to test OFG's Rust
GLTF/GLB importer. They are intentionally feature-focused, not production art.

Source repository:
https://github.com/KhronosGroup/glTF-Sample-Assets

Sample asset browser:
https://github.khronos.org/glTF-Sample-Assets/

Downloaded files:

- `static-box.glb`
  - Source: `Models/Box/glTF-Binary/Box.glb`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Box/glTF-Binary/Box.glb
  - Purpose: small binary GLB static mesh fixture.
  - Asset license: CC-BY-4.0.

- `animated-cube.gltf`
  - Source: `Models/AnimatedCube/glTF/AnimatedCube.gltf`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/AnimatedCube/glTF/AnimatedCube.gltf
  - Purpose: small node-animation fixture.
  - Asset license: CC0-1.0.

- `animated-cube.bin`
  - Source: `Models/AnimatedCube/glTF/AnimatedCube.bin`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/AnimatedCube/glTF/AnimatedCube.bin
  - Purpose: external buffer for `animated-cube.gltf`.
  - Asset license: CC0-1.0.

- `box-animated.glb`
  - Source: `Models/BoxAnimated/glTF-Binary/BoxAnimated.glb`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/BoxAnimated/glTF-Binary/BoxAnimated.glb
  - Purpose: compact binary node-animation fixture with translation and rotation channels.
  - Asset license: CC-BY-4.0.

- `simple-skin.gltf`
  - Source: `Models/SimpleSkin/glTF-Embedded/SimpleSkin.gltf`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/SimpleSkin/glTF-Embedded/SimpleSkin.gltf
  - Purpose: tiny embedded skinning fixture for later skeleton tests.
  - Asset license: CC0-1.0.

- `rigged-simple.glb`
  - Source: `Models/RiggedSimple/glTF-Binary/RiggedSimple.glb`
  - URL: https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/RiggedSimple/glTF-Binary/RiggedSimple.glb
  - Purpose: compact binary rigged/skinned GLB fixture.
  - Asset license: CC-BY-4.0.

- `material-specular-glossiness-13.glb`
  - Source: glTF Asset Generator positive
    `Material_SpecularGlossiness_13.gltf`.
  - URL: https://github.khronos.org/glTF-Asset-Generator/Output/Positive/Material_SpecularGlossiness/Material_SpecularGlossiness_13.gltf
  - Purpose: compact render fixture for required
    `KHR_materials_pbrSpecularGlossiness` with diffuse and
    specular-glossiness textures.
  - Conversion: embedded the external `.bin`, `BaseColor_X.png`,
    `Diffuse_Plane.png`, and `SpecularGlossiness_Plane.png` resources into one
    GLB, and transformed the quad from X/Y into OFG's X/Z ground plane for
    browser-smoke visibility.
  - Asset license: generated Khronos glTF Asset Generator output; repository
    metadata and README are CC-BY-4.0.

License notes were copied from each model's `LICENSE.md` in the Khronos sample
asset repository on 2026-06-06. Metadata files in the source repository are
licensed CC-BY-4.0.
