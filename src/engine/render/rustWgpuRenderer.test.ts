import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import type { EngineWebWgpuRenderer } from "../web/engineWebWasm.js";
import { Material } from "./Material.js";
import type { RenderMeshPacket } from "./RenderPackets.js";
import { RustWgpuRendererAdapter } from "./rustWgpuRenderer.js";
import { Texture } from "./Texture.js";

describe("RustWgpuRendererAdapter", () => {
  it("packs direct render item packets into coarse Rust/wgpu render arrays", () => {
    const fake = fakeRenderer();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake);
    const mesh = fakeMeshPacket("mesh:test");
    const markerMesh = fakeMeshPacket("mesh:marker");
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
      ],
      {
        id: "player.marker",
        mesh: markerMesh,
        material
      }
    ));

    equal(fake.registeredMeshes, 2);
    equal(fake.registeredTextures, 1);
    equal(fake.registeredObjects, 2);
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
    equal(fake.lastRender?.meshHandles[0], 10);
    equal(fake.lastRender?.objectHandles[0], 30);
    equal(fake.lastRender?.albedoTextureHandles[0], 20);
    equal(fake.lastRender?.normalTextureHandles[0], 101);
    equal(fake.lastRender?.materialTextureHandles[0], 102);
    almostEqual(fake.lastRender?.worldMatrices[0], 1);
    almostEqual(fake.lastRender?.materialPackets[0], 0.8);
    almostEqual(fake.lastRender?.materialPackets[7], 0.4);
    equal(fake.lastRender?.playerMarkerMeshHandle, 11);
    equal(fake.lastRender?.playerMarkerObjectHandle, 31);
    almostEqual(fake.lastRender?.playerMarkerMaterialPacket[0], 0.8);
  });

  it("prunes object and mesh handles that disappeared from the render packets", () => {
    const fake = fakeRenderer();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake) as unknown as {
      objectHandles: Map<string, { handle: number }>;
      meshCache: Map<RenderMeshPacket, { handle: number }>;
      pruneObjectHandles(seenItemIds: Set<string>): void;
      pruneGpuMeshes(seenMeshes: Set<RenderMeshPacket>): void;
    };
    const keptMesh = fakeMeshPacket("mesh:kept");
    const goneMesh = fakeMeshPacket("mesh:gone");

    adapter.objectHandles.set("kept", { handle: 7 });
    adapter.objectHandles.set("gone", { handle: 8 });
    adapter.meshCache.set(keptMesh, { handle: 9 });
    adapter.meshCache.set(goneMesh, { handle: 10 });

    adapter.pruneObjectHandles(new Set(["kept"]));
    adapter.pruneGpuMeshes(new Set([keptMesh]));

    equal(adapter.objectHandles.has("kept"), true);
    equal(adapter.objectHandles.has("gone"), false);
    equal(adapter.meshCache.has(keptMesh), true);
    equal(adapter.meshCache.has(goneMesh), false);
    equal(fake.destroyedObjects.join(","), "8");
    equal(fake.destroyedMeshes.join(","), "10");
  });
});

type FakeRenderer = EngineWebWgpuRenderer & {
  registeredMeshes: number;
  registeredTextures: number;
  registeredObjects: number;
  destroyedMeshes: number[];
  destroyedObjects: number[];
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
    readonly meshHandles: Float64Array;
    readonly objectHandles: Float64Array;
    readonly albedoTextureHandles: Float64Array;
    readonly normalTextureHandles: Float64Array;
    readonly materialTextureHandles: Float64Array;
    readonly worldMatrices: Float32Array;
    readonly materialPackets: Float32Array;
    readonly playerMarkerMeshHandle: number;
    readonly playerMarkerObjectHandle: number;
    readonly playerMarkerMaterialPacket: Float32Array;
  };
};

function fakeRenderer(): FakeRenderer {
  return {
    registeredMeshes: 0,
    registeredTextures: 0,
    registeredObjects: 0,
    destroyedMeshes: [],
    destroyedObjects: [],
    resize() {},
    registerMesh() {
      this.registeredMeshes += 1;
      return 9 + this.registeredMeshes;
    },
    destroyMesh(handle) {
      this.destroyedMeshes.push(handle);
    },
    registerTexture() {
      this.registeredTextures += 1;
      return 19 + this.registeredTextures;
    },
    destroyTexture() {},
    registerObject() {
      this.registeredObjects += 1;
      return 29 + this.registeredObjects;
    },
    destroyObject(handle) {
      this.destroyedObjects.push(handle);
    },
    render() {},
    renderEngineFrame(
      engineSnapshot,
      aspect,
      meshHandles,
      objectHandles,
      albedoTextureHandles,
      normalTextureHandles,
      materialTextureHandles,
      worldMatrices,
      materialPackets,
      playerMarkerMeshHandle,
      playerMarkerObjectHandle,
      _playerMarkerAlbedoTextureHandle,
      _playerMarkerNormalTextureHandle,
      _playerMarkerMaterialTextureHandle,
      playerMarkerMaterialPacket
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect,
        meshHandles: new Float64Array(meshHandles),
        objectHandles: new Float64Array(objectHandles),
        albedoTextureHandles: new Float64Array(albedoTextureHandles),
        normalTextureHandles: new Float64Array(normalTextureHandles),
        materialTextureHandles: new Float64Array(materialTextureHandles),
        worldMatrices: new Float32Array(worldMatrices),
        materialPackets: new Float32Array(materialPackets),
        playerMarkerMeshHandle,
        playerMarkerObjectHandle,
        playerMarkerMaterialPacket: new Float32Array(playerMarkerMaterialPacket)
      };
    },
    fallbackAlbedoTextureHandle() {
      return 100;
    },
    fallbackNormalTextureHandle() {
      return 101;
    },
    fallbackMaterialTextureHandle() {
      return 102;
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
