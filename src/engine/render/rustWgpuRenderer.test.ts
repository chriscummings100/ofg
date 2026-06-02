import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import type { EngineWebBrowserGame } from "../web/engineWebWasm.js";
import { Material } from "./Material.js";
import type { RenderMeshPacket } from "./RenderPackets.js";
import { RustWgpuRendererAdapter } from "./rustWgpuRenderer.js";
import { Texture } from "./Texture.js";

describe("RustWgpuRendererAdapter", () => {
  it("uploads mesh and texture bytes then renders item IDs through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake);
    const mesh = fakeMeshPacket("mesh:test");
    const texture = new Texture(
      "texture:test",
      1,
      1,
      "rgba8unorm",
      { data: new Uint8Array([255, 0, 0, 255]) }
    );
    const material = new Material("material:test", {
      albedoFactor: vec4(0.8, 0.7, 0.6, 1),
      specular: vec3(0.1, 0.2, 0.3),
      specularFactor: 0.4
    });
    const snapshot = sampleEngineRenderSnapshot();

    withFakeWindow(() => adapter.renderEngineFrame(
      snapshot,
      [
        {
          id: "item:test",
          mesh,
          material,
          albedoTexture: texture
        }
      ]
    ));

    equal(fake.upsertedMeshes.length, 1);
    equal(fake.upsertedMeshes[0]?.id, "mesh:test");
    equal(fake.upsertedMeshes[0]?.floatsPerVertex, 19);
    equal(fake.upsertedTextures.length, 1);
    equal(fake.upsertedTextures[0]?.id, "texture:test");
    equal(fake.upsertedTextures[0]?.formatCode, 1);
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
    equal(fake.lastRender?.itemIds[0], "item:test");
    equal(fake.lastRender?.meshIds[0], "mesh:test");
    equal(fake.lastRender?.albedoTextureIds[0], "texture:test");
    equal(fake.lastRender?.normalTextureIds[0], "");
    equal(fake.lastRender?.materialTextureIds[0], "");
    almostEqual(fake.lastRender?.worldMatrices[0], 1);
    almostEqual(fake.lastRender?.materialPackets[0], 0.8);
    almostEqual(fake.lastRender?.materialPackets[7], 0.4);
  });

  it("destroys uploaded meshes that disappear from render packets", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake) as unknown as {
      uploadedMeshes: Map<string, RenderMeshPacket>;
      pruneUploadedMeshes(seenMeshes: Set<RenderMeshPacket>): void;
    };
    const keptMesh = fakeMeshPacket("mesh:kept");
    const goneMesh = fakeMeshPacket("mesh:gone");

    adapter.uploadedMeshes.set(keptMesh.id, keptMesh);
    adapter.uploadedMeshes.set(goneMesh.id, goneMesh);

    adapter.pruneUploadedMeshes(new Set([keptMesh]));

    equal(adapter.uploadedMeshes.has(keptMesh.id), true);
    equal(adapter.uploadedMeshes.has(goneMesh.id), false);
    equal(fake.destroyedMeshes.join(","), "mesh:gone");
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  upsertedMeshes: {
    readonly id: string;
    readonly floatsPerVertex: number;
  }[];
  upsertedTextures: {
    readonly id: string;
    readonly formatCode: number;
  }[];
  destroyedMeshes: string[];
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
    readonly itemIds: string[];
    readonly meshIds: string[];
    readonly albedoTextureIds: string[];
    readonly normalTextureIds: string[];
    readonly materialTextureIds: string[];
    readonly worldMatrices: Float32Array;
    readonly materialPackets: Float32Array;
  };
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedMeshes: [],
    upsertedTextures: [],
    destroyedMeshes: [],
    resize() {},
    upsertMesh(id, _vertices, _indices, floatsPerVertex) {
      this.upsertedMeshes.push({ id, floatsPerVertex });
    },
    destroyMesh(id) {
      this.destroyedMeshes.push(id);
    },
    upsertTexture(id, _width, _height, _layers, formatCode) {
      this.upsertedTextures.push({ id, formatCode });
    },
    destroyTexture() {},
    renderEngineFrame(
      engineSnapshot,
      aspect,
      itemIds,
      meshIds,
      albedoTextureIds,
      normalTextureIds,
      materialTextureIds,
      worldMatrices,
      materialPackets
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect,
        itemIds: [...itemIds],
        meshIds: [...meshIds],
        albedoTextureIds: [...albedoTextureIds],
        normalTextureIds: [...normalTextureIds],
        materialTextureIds: [...materialTextureIds],
        worldMatrices: new Float32Array(worldMatrices),
        materialPackets: new Float32Array(materialPackets)
      };
    },
    status() {
      return {
        version: 1,
        runtime: "rust-wgpu",
        configured: true,
        canvasWidth: 640,
        canvasHeight: 480,
        maxTextureArrayLayers: 16,
        requiredTextureArrayLayers: 16,
        meshCount: 1,
        textureCount: 3,
        objectCount: 1,
        frameIndex: 1,
        frameDrawCount: 1
      };
    }
  };
}

function fakeMeshPacket(id: string): RenderMeshPacket {
  return {
    id,
    vertices: new Float32Array(19 * 3),
    indices: new Uint32Array([0, 1, 2]),
    floatsPerVertex: 19
  };
}

function sampleEngineRenderSnapshot(): Float32Array {
  return new Float32Array([
    1, 2, 3,
    1, 2, 2,
    0, 0,
    70 * Math.PI / 180,
    0.05,
    500,
    0, 1, 0,
    1, 1, 1,
    2,
    0.2,
    1,
    1, 2, 3,
    0
  ]);
}

function fakeCanvas(): HTMLCanvasElement {
  return {
    clientWidth: 640,
    clientHeight: 480,
    width: 0,
    height: 0
  } as HTMLCanvasElement;
}

function withFakeWindow(action: () => void): void {
  const globalWithWindow = globalThis as unknown as {
    window?: { devicePixelRatio: number };
  };
  const previousWindow = globalWithWindow.window;
  globalWithWindow.window = { devicePixelRatio: 1 };
  try {
    action();
  } finally {
    globalWithWindow.window = previousWindow;
  }
}

function almostEqual(actual: number | undefined, expected: number): void {
  if (actual === undefined) {
    ok(false, "Expected a numeric value.");
    return;
  }
  ok(Math.abs(actual - expected) < 0.00001, `Expected ${actual} to be close to ${expected}.`);
}
