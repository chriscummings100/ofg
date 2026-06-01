import { equal } from "node:assert/strict";
import { WebGpuRenderer } from "./webgpuRenderer.js";

type FakeGpuMesh = {
  readonly vertexBuffer: { destroy: () => void };
  readonly indexBuffer: { destroy: () => void };
  readonly indexCount: number;
};

describe("WebGpuRenderer", () => {
  it("prunes object uniforms for render items that disappeared", () => {
    const renderer = new WebGpuRenderer({} as HTMLCanvasElement) as unknown as {
      objectUniforms: Map<string, { uniformBuffer: { destroy: () => void } }>;
      pruneObjectUniforms(seenItemIds: Set<string>): void;
    };
    let destroyed = 0;
    renderer.objectUniforms.set("kept", { uniformBuffer: { destroy: () => { destroyed += 1; } } });
    renderer.objectUniforms.set("gone", { uniformBuffer: { destroy: () => { destroyed += 1; } } });

    renderer.pruneObjectUniforms(new Set(["kept"]));

    equal(renderer.objectUniforms.has("kept"), true);
    equal(renderer.objectUniforms.has("gone"), false);
    equal(destroyed, 1);
  });

  it("destroys cached GPU mesh buffers for meshes that disappeared", () => {
    const renderer = new WebGpuRenderer({} as HTMLCanvasElement) as unknown as {
      meshCache: Map<object, FakeGpuMesh>;
      pruneGpuMeshes(seenMeshes: Set<object>): void;
    };
    const keptMesh = {};
    const goneMesh = {};
    const destroyedBuffers: string[] = [];

    renderer.meshCache.set(keptMesh, createFakeGpuMesh("kept", destroyedBuffers));
    renderer.meshCache.set(goneMesh, createFakeGpuMesh("gone", destroyedBuffers));

    renderer.pruneGpuMeshes(new Set([keptMesh]));

    equal(renderer.meshCache.has(keptMesh), true);
    equal(renderer.meshCache.has(goneMesh), false);
    equal(destroyedBuffers.join(","), "gone:vertex,gone:index");
  });
});

function createFakeGpuMesh(label: string, destroyedBuffers: string[]): FakeGpuMesh {
  return {
    vertexBuffer: {
      destroy: () => destroyedBuffers.push(`${label}:vertex`)
    },
    indexBuffer: {
      destroy: () => destroyedBuffers.push(`${label}:index`)
    },
    indexCount: 3
  };
}
