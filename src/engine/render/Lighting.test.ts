import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { createDirectionalLight } from "./Lighting.js";

describe("Lighting", () => {
  it("creates a normalized default main light", () => {
    const light = createDirectionalLight();
    const length = Math.hypot(light.direction.x, light.direction.y, light.direction.z);

    ok(Math.abs(length - 1) < 1e-6);
    ok(light.direction.y > 0);
    equal(light.intensity, 1);
    equal(light.ambient, 0.34);
  });

  it("normalizes custom light direction and stores color", () => {
    const light = createDirectionalLight({
      direction: vec3(10, 0, 0),
      color: vec3(0.8, 0.7, 0.6),
      intensity: 2,
      ambient: 0.1
    });

    equal(light.direction.x, 1);
    equal(light.direction.y, 0);
    equal(light.direction.z, 0);
    equal(light.color.x, 0.8);
    equal(light.intensity, 2);
    equal(light.ambient, 0.1);
  });
});
