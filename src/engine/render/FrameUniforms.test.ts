import { equal, ok } from "node:assert/strict";
import { identityMat4 } from "../math/mat4.js";
import { vec3 } from "../math/vec3.js";
import { createDirectionalLight } from "./Lighting.js";
import { FRAME_UNIFORM_BYTES, FRAME_UNIFORM_FLOATS, buildFrameUniformValues } from "./FrameUniforms.js";

describe("FrameUniforms", () => {
  it("packs camera matrices, eye position, and main light", () => {
    const viewProjection = identityMat4();
    const inverseViewProjection = identityMat4();
    viewProjection[0] = 2;
    inverseViewProjection[5] = 3;
    const light = createDirectionalLight({
      direction: vec3(1, 0, 0),
      color: vec3(0.9, 0.8, 0.7),
      intensity: 1.5,
      ambient: 0.25
    });

    const values = buildFrameUniformValues({
      eye: vec3(4, 5, 6),
      target: vec3(0, 0, 1),
      viewProjection,
      inverseViewProjection
    }, light);

    equal(FRAME_UNIFORM_FLOATS, 44);
    equal(FRAME_UNIFORM_BYTES, 176);
    equal(values[0], 2);
    equal(values[21], 3);
    equal(values[32], 4);
    equal(values[33], 5);
    equal(values[34], 6);
    equal(values[36], 1);
    equal(values[39], 1.5);
    ok(Math.abs(values[40] - 0.9) < 1e-6);
    equal(values[43], 0.25);
  });

  it("can write into a reusable target buffer", () => {
    const target = new Float32Array(FRAME_UNIFORM_FLOATS);
    const light = createDirectionalLight();

    const values = buildFrameUniformValues({
      eye: vec3(1, 2, 3),
      target: vec3(0, 0, 1),
      viewProjection: identityMat4(),
      inverseViewProjection: identityMat4()
    }, light, target);

    equal(values, target);
    equal(target[32], 1);
  });
});
