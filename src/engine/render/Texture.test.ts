import { equal, throws } from "node:assert/strict";
import { Texture } from "./Texture.js";

describe("Texture", () => {
  it("stores texture metadata", () => {
    const texture = new Texture("texture:test", 64, 32, "rgba8unorm");

    equal(texture.id, "texture:test");
    equal(texture.width, 64);
    equal(texture.height, 32);
    equal(texture.format, "rgba8unorm");
  });

  it("stores optional rgba texture data", () => {
    const data = new Uint8Array([255, 0, 0, 255]);

    const texture = new Texture("texture:red", 1, 1, "rgba8unorm", { data });

    equal(texture.data, data);
  });

  it("rejects invalid dimensions", () => {
    throws(() => new Texture("texture:bad", 0, 1, "rgba8unorm"), /positive/);
  });

  it("rejects incorrectly sized rgba data", () => {
    throws(
      () => new Texture("texture:bad", 2, 2, "rgba8unorm", { data: new Uint8Array([255]) }),
      /width \* height \* 4/
    );
  });
});
