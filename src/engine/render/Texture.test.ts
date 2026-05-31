import { equal } from "node:assert/strict";
import { Texture } from "./Texture.js";

describe("Texture", () => {
  it("stores texture metadata", () => {
    const texture = new Texture("texture:test", 64, 32, "rgba8unorm");

    equal(texture.id, "texture:test");
    equal(texture.width, 64);
    equal(texture.height, 32);
    equal(texture.format, "rgba8unorm");
  });
});
