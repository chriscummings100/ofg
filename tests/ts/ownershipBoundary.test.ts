import assert from "node:assert/strict";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { extname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("../../..", import.meta.url)));
const appDir = resolve(root, "src/app");
const allowedGeneratedWasmImporters = new Set([
  normalizePath("src/app/wasmRuntime.ts")
]);
const generatedWasmPatterns = [
  /assets\/wasm\/ofg_web/,
  /assets\\wasm\\ofg_web/,
  /ofg_web_bg\.wasm/,
  /ofg_web\.js/
];
const forbiddenRenderOwnershipPatterns = [
  /\bnavigator\.gpu\b/,
  /\brequestAdapter\b/,
  /\bGPUDevice\b/,
  /\bGPUCanvasContext\b/,
  /\bGPUQueue\b/,
  /\bGPURenderPassEncoder\b/,
  /\bcreateBuffer\b/,
  /\bcreateCommandEncoder\b/,
  /\bcreateRenderPipeline\b/,
  /\bcreateShaderModule\b/,
  /\bcreateTexture\b/,
  /\bbeginRenderPass\b/,
  /\bgetCurrentTexture\b/,
  /\brequestDevice\b/,
  /\bsetPipeline\b/,
  /\bdrawIndexed\s*\(/,
  /\bgetContext\(["']webgpu["']\)/,
  /\bqueue\.submit\b/,
  /\.configure\s*\(/,
  /\bdraw\s*\(/
];

describe("TypeScript ownership boundary", () => {
  it("loads generated WASM internals only through wasmRuntime.ts", () => {
    for (const file of appSourceFiles()) {
      const source = readFileSync(file, "utf8");
      const relativePath = normalizePath(relative(root, file));
      const importsGeneratedWasm = generatedWasmPatterns.some((pattern) =>
        pattern.test(source)
      );

      assert.equal(
        importsGeneratedWasm && !allowedGeneratedWasmImporters.has(relativePath),
        false,
        `${file} imports generated WASM internals outside wasmRuntime.ts`
      );
    }
  });

  it("keeps WebGPU draw ownership out of TypeScript app code", () => {
    for (const file of appSourceFiles()) {
      const source = readFileSync(file, "utf8");
      for (const pattern of forbiddenRenderOwnershipPatterns) {
        assert.equal(
          pattern.test(source),
          false,
          `${file} appears to own WebGPU renderer behavior via ${pattern}`
        );
      }
    }
  });

  it("recognizes representative TypeScript WebGPU ownership snippets", () => {
    const snippets = [
      "await navigator.gpu.requestAdapter()",
      "device.createCommandEncoder()",
      "encoder.beginRenderPass({})",
      "context.getCurrentTexture()",
      "device.queue.submit([])",
      "pass.setPipeline(pipeline)",
      "pass.drawIndexed(3)",
      "canvas.getContext(\"webgpu\")",
      "context.configure({ device })"
    ];

    for (const snippet of snippets) {
      assert.equal(
        forbiddenRenderOwnershipPatterns.some((pattern) => pattern.test(snippet)),
        true,
        `denylist did not catch ${snippet}`
      );
    }
  });
});

function appSourceFiles(): string[] {
  return collectTypeScriptFiles(appDir);
}

function collectTypeScriptFiles(dir: string): string[] {
  const files: string[] = [];
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    const stats = statSync(path);
    if (stats.isDirectory()) {
      files.push(...collectTypeScriptFiles(path));
    } else if (stats.isFile() && extname(entry) === ".ts") {
      files.push(path);
    }
  }
  return files;
}

function normalizePath(path: string): string {
  return path.replaceAll("\\", "/");
}
