// Builds the Rust browser crate and regenerates the wasm-bindgen web package.
// The generated assets under assets/wasm/ofg_web are build output, not source.

import { existsSync, mkdirSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { wasmBindgenVersion } from "./wasm-bindgen-version.mjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const packageName = "ofg_web";
const target = "wasm32-unknown-unknown";
const outDir = resolve(root, "assets/wasm/ofg_web");
const wasmPath = resolve(root, `target/${target}/release/${packageName}.wasm`);

ensureWasmBindgenVersion();
run("cargo", ["build", "-p", packageName, "--target", target, "--release"]);

if (!existsSync(wasmPath)) {
  throw new Error(`Expected WASM artifact was not produced: ${wasmPath}`);
}

rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
run("wasm-bindgen", [
  wasmPath,
  "--target",
  "web",
  "--out-dir",
  outDir,
  "--out-name",
  packageName,
  "--typescript"
]);

function ensureWasmBindgenVersion() {
  const result = spawnSync("wasm-bindgen", ["--version"], {
    cwd: root,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"]
  });
  const output = `${result.stdout ?? ""}${result.stderr ?? ""}`;
  const match = output.trim().match(/^wasm-bindgen\s+(\d+\.\d+\.\d+)$/);
  const actualVersion = match?.[1] ?? null;
  if (result.status !== 0 || actualVersion !== wasmBindgenVersion) {
    throw new Error(
      `wasm-bindgen ${wasmBindgenVersion} is required. Found: ${output.trim() || "not installed"}\n` +
        `Install it with: cargo install wasm-bindgen-cli --version ${wasmBindgenVersion} --locked --force`
    );
  }
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: "inherit"
  });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
