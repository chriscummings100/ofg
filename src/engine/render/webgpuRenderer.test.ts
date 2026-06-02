import { equal } from "node:assert/strict";
import type { EngineWebGpuBridge } from "../web/engineWebWasm.js";
import { WebGpuRenderer } from "./webgpuRenderer.js";

type FakeGpuMesh = {
  readonly vertexBuffer: { destroy: () => void };
  readonly indexBuffer: { destroy: () => void };
  readonly indexCount: number;
  readonly rustHandle?: bigint;
};

describe("WebGpuRenderer", () => {
  it("prunes object uniforms for render items that disappeared and reports Rust bridge destruction", () => {
    const destroyedObjects: bigint[] = [];
    const renderer = new WebGpuRenderer({} as HTMLCanvasElement, fakeBridge({
      destroyObject: (handle) => {
        destroyedObjects.push(handle);
        return true;
      }
    })) as unknown as {
      objectUniforms: Map<string, { uniformBuffer: { destroy: () => void }; rustHandle?: bigint }>;
      pruneObjectUniforms(seenItemIds: Set<string>): void;
    };
    let destroyed = 0;
    renderer.objectUniforms.set("kept", { uniformBuffer: { destroy: () => { destroyed += 1; } }, rustHandle: 7n });
    renderer.objectUniforms.set("gone", { uniformBuffer: { destroy: () => { destroyed += 1; } }, rustHandle: 9n });

    renderer.pruneObjectUniforms(new Set(["kept"]));

    equal(renderer.objectUniforms.has("kept"), true);
    equal(renderer.objectUniforms.has("gone"), false);
    equal(destroyed, 1);
    equal(destroyedObjects.join(","), "9");
  });

  it("destroys cached GPU mesh buffers for meshes that disappeared and reports Rust bridge destruction", () => {
    const destroyedMeshes: bigint[] = [];
    const renderer = new WebGpuRenderer({} as HTMLCanvasElement, fakeBridge({
      destroyMesh: (handle) => {
        destroyedMeshes.push(handle);
        return true;
      }
    })) as unknown as {
      meshCache: Map<object, FakeGpuMesh>;
      pruneGpuMeshes(seenMeshes: Set<object>): void;
    };
    const keptMesh = {};
    const goneMesh = {};
    const destroyedBuffers: string[] = [];

    renderer.meshCache.set(keptMesh, createFakeGpuMesh("kept", destroyedBuffers, 10n));
    renderer.meshCache.set(goneMesh, createFakeGpuMesh("gone", destroyedBuffers, 11n));

    renderer.pruneGpuMeshes(new Set([keptMesh]));

    equal(renderer.meshCache.has(keptMesh), true);
    equal(renderer.meshCache.has(goneMesh), false);
    equal(destroyedBuffers.join(","), "gone:vertex,gone:index");
    equal(destroyedMeshes.join(","), "11");
  });
});

function createFakeGpuMesh(
  label: string,
  destroyedBuffers: string[],
  rustHandle?: bigint
): FakeGpuMesh {
  return {
    vertexBuffer: {
      destroy: () => destroyedBuffers.push(`${label}:vertex`)
    },
    indexBuffer: {
      destroy: () => destroyedBuffers.push(`${label}:index`)
    },
    indexCount: 3,
    rustHandle
  };
}

function fakeBridge(
  overrides: Partial<Pick<EngineWebGpuBridge, "destroyMesh" | "destroyObject">>
): EngineWebGpuBridge {
  return {
    destroyMesh: () => true,
    destroyObject: () => true,
    ...overrides
  } as unknown as EngineWebGpuBridge;
}
