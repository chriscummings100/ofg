import { spawnSync } from "node:child_process";
import { delimiter, resolve } from "node:path";
import { homedir } from "node:os";
import { fileURLToPath } from "node:url";

// Bootstraps Cloudflare's Linux build image before running the normal OFG build.
const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const wasmBindgenVersion = "0.2.100";

let buildEnv = withCargoPath(process.env);

ensureRustToolchain();
ensureWasmBindgen();
runNode("tools/clean-dist.mjs");
runNode("tools/build-shaders.mjs");
runNode("tools/build-terrain-wasm.mjs");
runNode("tools/build-engine-web-wasm.mjs");
runNode("node_modules/typescript/bin/tsc", ["-p", "tsconfig.json"]);
runNode("tools/package-site.mjs");

function ensureRustToolchain() {
  const hasCargo = commandSucceeds("cargo", ["--version"]);
  const hasRustup = commandSucceeds("rustup", ["--version"]);

  if (!hasCargo || !hasRustup) {
    if (process.platform === "win32") {
      throw new Error(
        "Rust is required for OFG builds. Install rustup locally from https://rustup.rs/."
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
  const version = commandOutput("wasm-bindgen", ["--version"]);
  if (version.includes(wasmBindgenVersion)) {
    return;
  }

  const installArgs = [
    "install",
    "wasm-bindgen-cli",
    "--version",
    wasmBindgenVersion,
    "--locked"
  ];
  if (version.length > 0) {
    installArgs.push("--force");
  }

  run("cargo", installArgs);
}

function commandSucceeds(command, args) {
  const result = spawnSync(executableFor(command), args, {
    cwd: root,
    env: buildEnv,
    stdio: "ignore"
  });
  return result.status === 0;
}

function commandOutput(command, args) {
  const result = spawnSync(executableFor(command), args, {
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
  const result = spawnSync(executableFor(command), args, {
    cwd: root,
    env: buildEnv,
    stdio: "inherit"
  });

  if (result.error !== undefined) {
    console.error(`Failed to run ${command}: ${result.error.message}`);
    process.exitCode = 1;
    process.exit();
  }

  if (result.status !== 0) {
    process.exitCode = result.status ?? 1;
    process.exit();
  }
}

function runNode(script, args = []) {
  run(process.execPath, [script, ...args]);
}

function executableFor(command) {
  return command;
}

function withCargoPath(env) {
  const cargoBin = resolve(env.CARGO_HOME ?? resolve(homedir(), ".cargo"), "bin");
  const pathKey = Object.keys(env).find((key) => key.toLowerCase() === "path") ?? "PATH";
  return {
    ...env,
    [pathKey]: `${cargoBin}${delimiter}${env[pathKey] ?? ""}`
  };
}
