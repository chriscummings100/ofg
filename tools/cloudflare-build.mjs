// Cloudflare Pages build wrapper for the C++/WASM browser app.
//
// Pages builds need the pinned Emscripten and Ninja tools before CMake can
// produce the browser module. The actual app build and package steps stay behind
// npm scripts so local and deployment behavior remain aligned.

import { existsSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const wasmPath = resolve(root, "assets/wasm/ofg_cpp/ofg_cpp.wasm");

runNpmScript("setup:emscripten");
runNpmScript("setup:ninja");
runNpmScript("build");
runNpmScript("package:site:from-build");
printWasmSize();

// Prints the generated C++ WASM size for deploy logs and regression tracking.
function printWasmSize() {
  if (!existsSync(wasmPath)) {
    throw new Error(`Expected generated WASM before deploy packaging: ${wasmPath}`);
  }

  const bytes = statSync(wasmPath).size;
  console.log(`Generated WASM size: ${bytes} bytes (${(bytes / 1024).toFixed(1)} KiB)`);
}

// Runs an npm script in a platform-compatible way.
function runNpmScript(scriptName) {
  if (process.platform === "win32") {
    run(process.env.ComSpec ?? "cmd.exe", ["/d", "/s", "/c", `npm run ${scriptName}`]);
    return;
  }
  run("npm", ["run", scriptName]);
}

// Runs a deployment command and exits with the same failing status.
function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: process.env,
    stdio: "inherit"
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
