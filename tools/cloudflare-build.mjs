// Cloudflare Pages build wrapper for the OFG static browser app.

import { existsSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { delimiter, resolve } from "node:path";
import { homedir } from "node:os";
import { fileURLToPath } from "node:url";
import { wasmBindgenVersion } from "./wasm-bindgen-version.mjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const wasmPath = resolve(root, "assets/wasm/ofg_web/ofg_web_bg.wasm");

let buildEnv = withCargoPath(process.env);

ensureRustToolchain();
ensureWasmBindgen();
runNpmScript("build");
runNpmScript("package:site:from-build");
printWasmSize();

function ensureRustToolchain() {
  const hasCargo = commandSucceeds("cargo", ["--version"]);
  const hasRustup = commandSucceeds("rustup", ["--version"]);

  if (!hasCargo || !hasRustup) {
    if (process.platform !== "linux") {
      throw new Error(
        "Rust and rustup are required for OFG builds. Install rustup from https://rustup.rs/. Automatic rustup installation is only used on Linux build images."
      );
    }

    run("bash", [
      "-lc",
      "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal"
    ]);
    buildEnv = withCargoPath(buildEnv);
  }

  run("rustup", ["target", "add", "wasm32-unknown-unknown"]);
}

function ensureWasmBindgen() {
  const version = wasmBindgenCliVersion();
  if (version === wasmBindgenVersion) {
    return;
  }

  const installArgs = [
    "install",
    "wasm-bindgen-cli",
    "--version",
    wasmBindgenVersion,
    "--locked"
  ];
  if (version !== null) {
    installArgs.push("--force");
  }
  run("cargo", installArgs);
}

function wasmBindgenCliVersion() {
  const output = commandOutput("wasm-bindgen", ["--version"]);
  const match = output.trim().match(/^wasm-bindgen\s+(\d+\.\d+\.\d+)$/);
  return match?.[1] ?? null;
}

function printWasmSize() {
  if (!existsSync(wasmPath)) {
    throw new Error(`Expected generated WASM before deploy packaging: ${wasmPath}`);
  }

  const bytes = statSync(wasmPath).size;
  console.log(`Generated WASM size: ${bytes} bytes (${(bytes / 1024).toFixed(1)} KiB)`);
}

function commandSucceeds(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: buildEnv,
    stdio: "ignore"
  });
  return result.status === 0;
}

function commandOutput(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: buildEnv,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"]
  });
  if (result.status !== 0) {
    return "";
  }
  return `${result.stdout ?? ""}${result.stderr ?? ""}`;
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    env: buildEnv,
    stdio: "inherit"
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function runNpmScript(scriptName) {
  if (process.platform === "win32") {
    run(process.env.ComSpec ?? "cmd.exe", ["/d", "/s", "/c", `npm run ${scriptName}`]);
    return;
  }
  run("npm", ["run", scriptName]);
}

function withCargoPath(env) {
  const cargoBin = resolve(env.CARGO_HOME ?? resolve(homedir(), ".cargo"), "bin");
  const pathKey = Object.keys(env).find((key) => key.toLowerCase() === "path") ?? "PATH";
  return {
    ...env,
    [pathKey]: `${cargoBin}${delimiter}${env[pathKey] ?? ""}`
  };
}
