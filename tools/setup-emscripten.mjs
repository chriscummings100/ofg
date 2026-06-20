// Installs the pinned Emscripten SDK used by C++/WASM builds.
//
// The checkout lives under artifacts/toolchains so it remains generated local
// state, while emscripten-version.txt keeps the project-level tool pin explicit.
import { existsSync } from "node:fs";
import { mkdir, readFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const emsdkVersion = (await readFile(path.join(rootDir, "emscripten-version.txt"), "utf8")).trim();
const toolchainDir = path.join(rootDir, "artifacts", "toolchains");
const emsdkDir = path.join(toolchainDir, "emsdk");
const emsdkCommand = process.platform === "win32" ? "emsdk.bat" : "./emsdk";
const emsdkPath = path.join(emsdkDir, emsdkCommand);

await mkdir(toolchainDir, { recursive: true });

run(
  "git",
  existsSync(emsdkDir)
    ? ["-C", emsdkDir, "fetch", "--tags", "origin"]
    : ["clone", "https://github.com/emscripten-core/emsdk.git", emsdkDir]
);
run("git", ["-C", emsdkDir, "checkout", emsdkVersion]);
run(emsdkPath, ["install", emsdkVersion], { cwd: emsdkDir });
run(emsdkPath, ["activate", emsdkVersion], { cwd: emsdkDir });

console.log(`Emscripten ${emsdkVersion} is ready at ${emsdkDir}`);

// Runs a setup command and streams progress to the caller.
function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    stdio: "inherit",
    shell: process.platform === "win32",
    ...options
  });

  if (result.status !== 0) {
    const printable = [command, ...args].join(" ");
    throw new Error(`${printable} failed with exit code ${result.status}`);
  }
}
