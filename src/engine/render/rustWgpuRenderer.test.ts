import { equal, ok } from "node:assert/strict";
import { identityMat4 } from "../math/mat4.js";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import type { EngineWebWgpuRenderer } from "../web/engineWebWasm.js";
import { Material } from "./Material.js";
import { Mesh } from "./Mesh.js";
import type { RenderWorld } from "./RenderWorld.js";
import { RustWgpuRendererAdapter } from "./rustWgpuRenderer.js";
import { Texture } from "./Texture.js";

describe("RustWgpuRendererAdapter", () => {
  it("packs render items into coarse Rust/wgpu render arrays", () => {
    const fake = fakeRenderer();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake);
    const mesh = new Mesh(
      "mesh:test",
      new Float32Array(19 * 3),
      new Uint32Array([0, 1, 2]),
      { floatsPerVertex: 19, attributes: [] }
    );
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
    const renderWorld: RenderWorld = {
      camera: {
        viewProjection: identityMat4(),
        inverseViewProjection: identityMat4(),
        eye: vec3(1, 2, 3),
        target: vec3(0, 0, 0)
      },
      mainLight: {
        direction: vec3(0, 1, 0),
        color: vec3(1, 1, 1),
        intensity: 2,
        ambient: 0.2
      },
      items: [
        {
          id: "item:test",
          mesh,
          material,
          albedoTexture: texture,
          worldMatrix: identityMat4()
        }
      ]
    };

    withFakeWindow(() => adapter.render(renderWorld));

    equal(fake.registeredMeshes, 1);
    equal(fake.registeredTextures, 1);
    equal(fake.registeredObjects, 1);
    equal(fake.lastRender?.meshHandles[0], 10);
    equal(fake.lastRender?.objectHandles[0], 30);
    equal(fake.lastRender?.albedoTextureHandles[0], 20);
    equal(fake.lastRender?.normalTextureHandles[0], 101);
    equal(fake.lastRender?.materialTextureHandles[0], 102);
    almostEqual(fake.lastRender?.framePacket[32], 1);
    almostEqual(fake.lastRender?.framePacket[41], 2);
    almostEqual(fake.lastRender?.worldMatrices[0], 1);
    almostEqual(fake.lastRender?.materialPackets[0], 0.8);
    almostEqual(fake.lastRender?.materialPackets[7], 0.4);
  });

  it("prunes object and mesh handles that disappeared from the render world", () => {
    const fake = fakeRenderer();
    const adapter = new RustWgpuRendererAdapter(fakeCanvas(), fake) as unknown as {
      objectHandles: Map<string, { handle: number }>;
      meshCache: Map<object, { handle: number }>;
      pruneObjectHandles(seenItemIds: Set<string>): void;
      pruneGpuMeshes(seenMeshes: Set<object>): void;
    };
    const keptMesh = {};
    const goneMesh = {};

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
    readonly meshHandles: Float64Array;
    readonly objectHandles: Float64Array;
    readonly albedoTextureHandles: Float64Array;
    readonly normalTextureHandles: Float64Array;
    readonly materialTextureHandles: Float64Array;
    readonly framePacket: Float32Array;
    readonly worldMatrices: Float32Array;
    readonly materialPackets: Float32Array;
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
      return 10;
    },
    destroyMesh(handle) {
      this.destroyedMeshes.push(handle);
    },
    registerTexture() {
      this.registeredTextures += 1;
      return 20;
    },
    destroyTexture() {},
    registerObject() {
      this.registeredObjects += 1;
      return 30;
    },
    destroyObject(handle) {
      this.destroyedObjects.push(handle);
    },
    render(
      framePacket,
      meshHandles,
      objectHandles,
      albedoTextureHandles,
      normalTextureHandles,
      materialTextureHandles,
      worldMatrices,
      materialPackets
    ) {
      this.lastRender = {
        framePacket: new Float32Array(framePacket),
        meshHandles: new Float64Array(meshHandles),
        objectHandles: new Float64Array(objectHandles),
        albedoTextureHandles: new Float64Array(albedoTextureHandles),
        normalTextureHandles: new Float64Array(normalTextureHandles),
        materialTextureHandles: new Float64Array(materialTextureHandles),
        worldMatrices: new Float32Array(worldMatrices),
        materialPackets: new Float32Array(materialPackets)
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
