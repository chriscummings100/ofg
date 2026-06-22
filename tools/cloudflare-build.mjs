// Cloudflare Pages build wrapper for the C++/WASM browser app.
//
// Cloudflare should receive prebuilt static files. This command builds the app,
// packages `.deploy`, and reports the WASM size without installing any compiler
// toolchain or Dawn checkout.
import { existsSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const wasmPath = resolve(root, "assets/wasm/ofg_cpp/ofg_cpp.wasm");

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
  const npmExecPath = process.env.npm_execpath;
  if (npmExecPath && existsSync(npmExecPath)) {
    run(process.execPath, [npmExecPath, "run", scriptName]);
    return;
  }

  const npmCommand = process.platform === "win32" ? "npm.cmd" : "npm";
  run(npmCommand, ["run", scriptName], { shell: process.platform === "win32" });
}

// Runs a build command and exits with the same failing status.
function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: process.env,
    stdio: "inherit",
    ...options
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
