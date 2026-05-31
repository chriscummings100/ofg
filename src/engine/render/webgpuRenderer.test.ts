import { equal } from "node:assert/strict";
import { WebGpuRenderer } from "./webgpuRenderer.js";

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
});
