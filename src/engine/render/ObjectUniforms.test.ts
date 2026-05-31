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

    equal(OBJECT_UNIFORM_FLOATS, 24);
    equal(OBJECT_UNIFORM_BYTES, 96);
    equal(values[12], 3);
    equal(values[16], 1);
    equal(values[17], 1);
    equal(values[18], 1);
    equal(values[19], 1);
    equal(values[20], 1);
    equal(values[21], 1);
    equal(values[22], 1);
    ok(Math.abs(values[23] - 0.18) < 1e-6);
  });

  it("packs material albedo and specular values", () => {
    const material = new Material("material:test", {
      albedoFactor: vec4(0.25, 0.5, 0.75, 1),
      specular: vec3(0.9, 0.8, 0.7),
      specularFactor: 0.35
    });

    const values = buildObjectUniformValues(identityMat4(), material);

    equal(values[16], 0.25);
    equal(values[17], 0.5);
    equal(values[18], 0.75);
    equal(values[19], 1);
    ok(Math.abs(values[20] - 0.9) < 1e-6);
    ok(Math.abs(values[21] - 0.8) < 1e-6);
    ok(Math.abs(values[22] - 0.7) < 1e-6);
    ok(Math.abs(values[23] - 0.35) < 1e-6);
  });
});
