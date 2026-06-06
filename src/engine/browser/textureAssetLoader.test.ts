import { deepEqual, equal } from "node:assert/strict";
import {
  createBrowserTextureAssetLoader,
  textureFromRgbaPixels
} from "./textureAssetLoader.js";

describe("textureAssetLoader", () => {
  it("creates plain RGBA texture arrays from pixel data", () => {
    const data = new Uint8ClampedArray([
      255, 0, 0, 255,
      0, 255, 0, 255,
      0, 0, 255, 255,
      255, 255, 255, 255
    ]);

    const texture = textureFromRgbaPixels("texture:test", 2, 2, data);

    equal(texture.width, 2);
    equal(texture.height, 2);
    equal(texture.layers, 1);
    equal(texture.data.length, 16);
    equal(texture.data[1], 0);
    equal(texture.data[4], 0);
  });

  it("loads generic texture-array requests without terrain semantics", async () => {
    const labels: string[] = [];
    const urlsByLabel: Record<string, readonly string[]> = {};
    const loader = createBrowserTextureAssetLoader(async (label, urls) => {
      labels.push(label);
      urlsByLabel[label] = urls;
      return {
        width: 1,
        height: 1,
        layers: urls.length,
        data: new Uint8Array(urls.length * 4)
      };
    });

    const assets = await loader.loadTextureArrays([
      { id: "array:a", urls: ["/a.png", "/b.png"] },
      { id: "array:b", urls: ["/c.png"] }
    ]);

    deepEqual(labels, ["texture-array:array:a", "texture-array:array:b"]);
    deepEqual(urlsByLabel["texture-array:array:a"], ["/a.png", "/b.png"]);
    equal(assets[0]?.id, "array:a");
    equal(assets[0]?.layers, 2);
    equal(assets[1]?.id, "array:b");
    equal(assets[1]?.layers, 1);
  });
});
