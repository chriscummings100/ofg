import { deepEqual, equal, ok } from "node:assert/strict";
import {
  createBrowserAssetLoader,
  createBrowserTextureAssetLoader,
  loadByteAssetFromUrl,
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

  it("loads generic byte requests without model semantics", async () => {
    const fetchedUrls: string[] = [];
    const loader = createBrowserAssetLoader(
      async () => {
        throw new Error("Texture decoding should not run for byte requests.");
      },
      async (url) => {
        fetchedUrls.push(url);
        return new Uint8Array([fetchedUrls.length, 7]);
      }
    );

    const assets = await loader.loadBytes([
      { id: "model:box", url: "/assets/models/test-fixtures/static-box.glb" },
      { id: "model:walk", url: "/assets/models/player/walk.glb" }
    ]);

    deepEqual(fetchedUrls, [
      "/assets/models/test-fixtures/static-box.glb",
      "/assets/models/player/walk.glb"
    ]);
    equal(assets[0]?.id, "model:box");
    deepEqual(Array.from(assets[0]?.data ?? []), [1, 7]);
    equal(assets[1]?.id, "model:walk");
    deepEqual(Array.from(assets[1]?.data ?? []), [2, 7]);
  });

  it("fetches raw asset bytes from URLs", async () => {
    const originalFetch = globalThis.fetch;
    const fetchMock: typeof fetch = async (input) => {
      equal(input, "/assets/models/test-fixtures/static-box.glb");
      return new Response(new Uint8Array([1, 2, 3, 4]));
    };
    globalThis.fetch = fetchMock;

    try {
      const bytes = await loadByteAssetFromUrl("/assets/models/test-fixtures/static-box.glb");

      deepEqual(Array.from(bytes), [1, 2, 3, 4]);
    } finally {
      globalThis.fetch = originalFetch;
    }
  });

  it("rejects failed raw byte fetches with the URL and status", async () => {
    const originalFetch = globalThis.fetch;
    const fetchMock: typeof fetch = async () => new Response("", {
      status: 404,
      statusText: "Not Found"
    });
    globalThis.fetch = fetchMock;

    try {
      let caughtError: unknown;
      try {
        await loadByteAssetFromUrl("/missing.glb");
      } catch (error) {
        caughtError = error;
      }

      if (!(caughtError instanceof Error)) {
        throw new Error("Expected loadByteAssetFromUrl to throw an Error.");
      }
      ok(/Failed to load asset bytes '\/missing\.glb': 404 Not Found/.test(caughtError.message));
    } finally {
      globalThis.fetch = originalFetch;
    }
  });
});
