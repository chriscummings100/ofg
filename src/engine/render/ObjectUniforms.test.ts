import { equal, ok } from "node:assert/strict";
import { identityMat4 } from "../math/mat4.js";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import { Material } from "./Material.js";
import { OBJECT_UNIFORM_BYTES, OBJECT_UNIFORM_FLOATS, buildObjectUniformValues } from "./ObjectUniforms.js";

describe("ObjectUniforms", () => {
  it("packs world matrix and default material values", () => {
    const world = identityMat4();
    world[12] = 3;

    const values = buildObjectUniformValues(world);

    equal(OBJECT_UNIFORM_FLOATS, 44);
    equal(OBJECT_UNIFORM_BYTES, 176);
    equal(values[12], 3);
    equal(values[16], 1);
    equal(values[21], 1);
    equal(values[26], 1);
    equal(values[32], 1);
    equal(values[33], 1);
    equal(values[34], 1);
    equal(values[35], 1);
    equal(values[36], 1);
    equal(values[37], 1);
    equal(values[38], 1);
    ok(Math.abs(values[39] - 0.18) < 1e-6);
    equal(values[40], 0);
    equal(values[41], 1);
    equal(values[42], 0);
    equal(values[43], 0);
  });

  it("packs material albedo and specular values", () => {
    const material = new Material("material:test", {
      albedoFactor: vec4(0.25, 0.5, 0.75, 1),
      specular: vec3(0.9, 0.8, 0.7),
      specularFactor: 0.35,
      flags: 3,
      textureScale: 0.125
    });

    const values = buildObjectUniformValues(identityMat4(), material);

    equal(values[32], 0.25);
    equal(values[33], 0.5);
    equal(values[34], 0.75);
    equal(values[35], 1);
    ok(Math.abs(values[36] - 0.9) < 1e-6);
    ok(Math.abs(values[37] - 0.8) < 1e-6);
    ok(Math.abs(values[38] - 0.7) < 1e-6);
    ok(Math.abs(values[39] - 0.35) < 1e-6);
    equal(values[40], 3);
    equal(values[41], 0.125);
  });

  it("packs an inverse-transpose normal matrix", () => {
    const world = identityMat4();
    world[0] = 2;
    world[5] = 4;
    world[10] = 8;

    const values = buildObjectUniformValues(world);

    equal(values[16], 0.5);
    equal(values[21], 0.25);
    equal(values[26], 0.125);
  });

  it("can write into a reusable target buffer", () => {
    const target = new Float32Array(OBJECT_UNIFORM_FLOATS);

    const values = buildObjectUniformValues(identityMat4(), undefined, target);

    equal(values, target);
  });
});
