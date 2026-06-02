import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import {
  createEngineWebRenderer,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  loadEngineWebWasmModule,
  patchLegacyWgpuRequiredLimits,
  type EngineWebWasmModule,
  type EngineWebWgpuRenderer
} from "./engineWebWasm.js";

describe("engine web WASM", () => {
  it("exposes deterministic engine web wasm-bindgen artifact metadata", () => {
    equal(ENGINE_WEB_WASM_METADATA.id, "engine_web");
    equal(ENGINE_WEB_WASM_METADATA.sourceCrate, "crates/engine_web");
    equal(ENGINE_WEB_WASM_METADATA.modulePath, "assets/wasm/engine_web/engine_web.js");
    equal(ENGINE_WEB_WASM_METADATA.wasmPath, "assets/wasm/engine_web/engine_web_bg.wasm");
    equal(ENGINE_WEB_WASM_METADATA.dtsPath, "assets/wasm/engine_web/engine_web.d.ts");
    equal(ENGINE_WEB_WASM_METADATA.target, "wasm32-unknown-unknown");
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.wasmHash));
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.moduleHash));
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.dtsHash));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("RustWgpuRenderer"));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("RustWgpuRendererStatus"));
  });

  it("emits wasm-bindgen glue for the Rust/wgpu renderer facade", () => {
    const moduleText = readFileSync(ENGINE_WEB_WASM_METADATA.modulePath, "utf8");
    const dtsText = readFileSync(ENGINE_WEB_WASM_METADATA.dtsPath, "utf8");

    ok(moduleText.includes("export class RustWgpuRenderer"));
    ok(moduleText.includes("export class RustWgpuRendererStatus"));
    ok(dtsText.includes("static create(canvas: HTMLCanvasElement): Promise<RustWgpuRenderer>"));
    ok(dtsText.includes("render("));
    ok(dtsText.includes("fallbackAlbedoTextureHandle"));
    ok(dtsText.includes("maxTextureArrayLayers"));
  });

  it("loads and initializes the wasm-bindgen module through a dynamic import hook", async () => {
    const calls: string[] = [];
    const module = fakeModule(fakeRenderer());
    const loaded = await loadEngineWebWasmModule(async (specifier) => {
      calls.push(specifier);
      return module;
    });

    equal(loaded, module);
    equal(module.initialized, true);
    equal(calls.length, 1);
    ok(calls[0].endsWith("/assets/wasm/engine_web/engine_web.js"));
  });

  it("creates the Rust/wgpu renderer from the loaded module", async () => {
    const renderer = fakeRenderer();
    const created = await createEngineWebRenderer({} as HTMLCanvasElement, async () =>
      fakeModule(renderer)
    );

    equal(created, renderer);
    equal(ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM, 1);
  });

  it("patches the legacy wgpu limit name before browser device requests", async () => {
    let requestedLimits: Record<string, number | undefined> | undefined;
    const globalObject = {
      GPUAdapter: {
        prototype: {
          async requestDevice(descriptor?: { requiredLimits?: Record<string, number> }) {
            requestedLimits = descriptor?.requiredLimits;
            return {};
          }
        }
      }
    };

    equal(patchLegacyWgpuRequiredLimits(globalObject as unknown as typeof globalThis), true);
    await globalObject.GPUAdapter.prototype.requestDevice({
      requiredLimits: {
        maxTextureArrayLayers: 16,
        maxInterStageShaderComponents: 60
      }
    });

    equal(requestedLimits?.maxTextureArrayLayers, 16);
    equal(requestedLimits?.maxInterStageShaderVariables, undefined);
    equal(requestedLimits?.maxInterStageShaderComponents, undefined);
    equal(patchLegacyWgpuRequiredLimits(globalObject as unknown as typeof globalThis), false);
  });
});

function fakeModule(renderer: EngineWebWgpuRenderer): EngineWebWasmModule & { initialized: boolean } {
  return {
    initialized: false,
    async default() {
      this.initialized = true;
    },
    RustWgpuRenderer: {
      async create() {
        return renderer;
      }
    }
  };
}

function fakeRenderer(): EngineWebWgpuRenderer {
  return {
    resize() {},
    registerMesh() {
      return 1;
    },
    destroyMesh() {},
    registerTexture() {
      return 2;
    },
    destroyTexture() {},
    registerObject() {
      return 3;
    },
    destroyObject() {},
    render() {},
    renderEngineFrame() {},
    fallbackAlbedoTextureHandle() {
      return 4;
    },
    fallbackNormalTextureHandle() {
      return 5;
    },
    fallbackMaterialTextureHandle() {
      return 6;
    },
    status() {
      return {
        version: 1,
        runtime: "rust-wgpu",
        configured: true,
        canvasWidth: 1,
        canvasHeight: 1,
        maxTextureArrayLayers: 16,
        requiredTextureArrayLayers: 16,
        meshCount: 0,
        textureCount: 3,
        objectCount: 0,
        frameIndex: 0,
        frameDrawCount: 0
      };
    }
  };
}
