import { equal, ok } from "node:assert/strict";
import type { RenderMeshPacket } from "../render/RenderPackets.js";
import type { TerrainRenderSource } from "../render/TerrainCoreRenderPackets.js";
import { Texture } from "../render/Texture.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads mesh and texture bytes then renders item IDs through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const mesh = fakeMeshPacket("mesh:test");
    const albedoTexture = fakeTexture("texture:albedo");
    const normalTexture = fakeTexture("texture:normal");
    const materialTexture = fakeTexture("texture:material");
    const snapshot = sampleEngineRenderSnapshot();

    withFakeWindow(() => adapter.renderEngineFrame(
      snapshot,
      fakeTerrainRenderSource(mesh, albedoTexture, normalTexture, materialTexture)
    ));

    equal(fake.upsertedMeshes.length, 1);
    equal(fake.upsertedMeshes[0]?.id, "mesh:test");
    equal(fake.upsertedMeshes[0]?.floatsPerVertex, 19);
    equal(fake.upsertedTextures.length, 3);
    equal(fake.upsertedTextures[0]?.id, "texture:albedo");
    equal(fake.upsertedTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainMaterials.length, 1);
    equal(fake.upsertedTerrainMaterials[0]?.albedoTextureId, "texture:albedo");
    equal(fake.upsertedTerrainMaterials[0]?.normalTextureId, "texture:normal");
    equal(fake.upsertedTerrainMaterials[0]?.materialTextureId, "texture:material");
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
    equal(fake.lastRender?.itemIds[0], "terrain:test:0,0,0");
    equal(fake.lastRender?.meshIds[0], "mesh:test");
    almostEqual(fake.lastRender?.worldMatrices[0], 1);
  });

  it("destroys uploaded meshes that disappear from render packets", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake) as unknown as {
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
  upsertedTerrainMaterials: {
    readonly albedoTextureId: string;
    readonly normalTextureId: string;
    readonly materialTextureId: string;
  }[];
  destroyedMeshes: string[];
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
    readonly itemIds: string[];
    readonly meshIds: string[];
    readonly worldMatrices: Float32Array;
  };
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedMeshes: [],
    upsertedTextures: [],
    upsertedTerrainMaterials: [],
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
    upsertTerrainMaterial(albedoTextureId, normalTextureId, materialTextureId) {
      this.upsertedTerrainMaterials.push({
        albedoTextureId,
        normalTextureId,
        materialTextureId
      });
    },
    renderEngineFrame(
      engineSnapshot,
      aspect,
      itemIds,
      meshIds,
      worldMatrices
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect,
        itemIds: [...itemIds],
        meshIds: [...meshIds],
        worldMatrices: new Float32Array(worldMatrices)
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

function fakeTerrainRenderSource(
  mesh: RenderMeshPacket,
  albedoTexture: Texture,
  normalTexture: Texture,
  materialTexture: Texture
): TerrainRenderSource {
  return {
    itemIdPrefix: "terrain:test",
    albedoTexture,
    normalTexture,
    materialTexture,
    chunks: [
      {
        key: "0,0,0",
        mesh
      }
    ]
  };
}

function fakeTexture(id: string): Texture {
  return new Texture(
    id,
    1,
    1,
    "rgba8unorm",
    { data: new Uint8Array([255, 0, 0, 255]) }
  );
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
