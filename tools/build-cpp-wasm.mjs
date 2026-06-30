// Builds the default C++/WASM browser module through CMake and Emscripten.
//
// The script keeps the generated Emscripten output shape deterministic:
// assets/wasm/ofg_cpp/ofg_cpp.js plus ofg_cpp.wasm. It uses installed system
// tools only and refuses repository-local toolchain fallbacks.
import { existsSync } from "node:fs";
import { mkdir } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { fileURLToPath } from "node:url";
import {
  cmakePath,
  configureCmakeIfNeeded,
  emscriptenEnv,
  findCmake,
  findEmscriptenCommand,
  findNinja,
  requireEmscriptenPort,
  run
} from "./lib/toolchain.mjs";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-wasm");
const outputDir = path.join(rootDir, "assets", "wasm", "ofg_cpp");
const emcmake = findEmscriptenCommand("emcmake", rootDir);
const cmake = findCmake(rootDir);
const ninja = findNinja(rootDir);
const env = emscriptenEnv({ emcmake, ninja });
const freshBuild = process.argv.slice(2).some((arg) => arg === "--fresh" || arg === "--clean");

requireEmscriptenPort({ emcmake, portName: "emdawnwebgpu" });

await mkdir(outputDir, { recursive: true });

configureCmakeIfNeeded(
  emcmake,
  [
    cmake,
    "-S",
    path.join(rootDir, "cpp"),
    "-B",
    buildDir,
    "-G",
    "Ninja",
    `-DCMAKE_MAKE_PROGRAM=${cmakePath(ninja)}`,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOFG_BUILD_TESTS=OFF",
    "-DOFG_BUILD_WASM=ON"
  ],
  { buildDir, cwd: rootDir, env, fresh: freshBuild }
);

run(cmake, ["--build", buildDir, "--target", "ofg_cpp_wasm"], { cwd: rootDir, env });

const jsPath = path.join(outputDir, "ofg_cpp.js");
const wasmPath = path.join(outputDir, "ofg_cpp.wasm");
if (!existsSync(jsPath) || !existsSync(wasmPath)) {
  throw new Error(
    `Expected Emscripten outputs were not created: ${jsPath} and ${wasmPath}`
  );
}

const generatedModule = await import(`${pathToFileURL(jsPath).href}?t=${Date.now()}`);
if (typeof generatedModule.default !== "function") {
  throw new Error("Generated ofg_cpp.js did not expose a default module factory.");
}

console.log(`Generated ${path.relative(rootDir, jsPath)}`);
console.log(`Generated ${path.relative(rootDir, wasmPath)}`);
