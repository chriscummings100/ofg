import { equal } from "node:assert/strict";
import { textureFromRgbaPixels } from "./textureLoader.js";

describe("textureLoader", () => {
  it("creates Texture objects from RGBA pixel data", () => {
    const data = new Uint8ClampedArray([
      255, 0, 0, 255,
      0, 255, 0, 255,
      0, 0, 255, 255,
      255, 255, 255, 255
    ]);

    const texture = textureFromRgbaPixels("texture:test", 2, 2, data);

    equal(texture.id, "texture:test");
    equal(texture.width, 2);
    equal(texture.height, 2);
    equal(texture.layers, 1);
    equal(texture.format, "rgba8unorm");
    equal(texture.data?.length, 16);
    equal(texture.data?.[1], 0);
    equal(texture.data?.[4], 0);
  });
});
