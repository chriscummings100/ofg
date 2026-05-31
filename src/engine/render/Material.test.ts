import { equal } from "node:assert/strict";
import { vec4 } from "../math/vec4.js";
import { Material } from "./Material.js";

describe("Material", () => {
  it("stores base color and flags", () => {
    const color = vec4(0.1, 0.2, 0.3, 1);
    const material = new Material("material:test", color, 7);

    equal(material.id, "material:test");
    equal(material.baseColor, color);
    equal(material.flags, 7);
  });

  it("allows texture assignment to change", () => {
    const material = new Material("material:test", vec4(1, 1, 1, 1), 0, "texture:first");

    material.texture = "texture:second";

    equal(material.texture, "texture:second");
  });
});
