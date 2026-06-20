// Pins the local Dawn source checkout used by the native C++ render smoke.
//
// Dawn itself is too large to vendor into the repository, so this script keeps a
// generated checkout under artifacts/toolchains/dawn/src and checks out the exact
// revision from dawn-version.txt. Dependency fetching remains Dawn/CMake-owned.
import { existsSync } from "node:fs";
import { mkdir, readFile, rm } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dawnRevision = (await readFile(path.join(rootDir, "dawn-version.txt"), "utf8")).trim();
const toolDir = path.join(rootDir, "artifacts", "toolchains", "dawn");
const sourceDir = path.join(toolDir, "src");

if (existsSync(path.join(sourceDir, ".git"))) {
  const current = runCapture("git", ["-C", sourceDir, "rev-parse", "HEAD"]).stdout.trim();
  if (current === dawnRevision) {
    console.log(`Dawn ${dawnRevision} is already ready at ${sourceDir}`);
    process.exit(0);
  }
  run("git", ["-C", sourceDir, "fetch", "origin", dawnRevision]);
  run("git", ["-C", sourceDir, "checkout", "--detach", dawnRevision]);
  console.log(`Dawn ${dawnRevision} is ready at ${sourceDir}`);
  process.exit(0);
}

await rm(toolDir, { recursive: true, force: true });
await mkdir(toolDir, { recursive: true });
run("git", ["clone", "https://dawn.googlesource.com/dawn", sourceDir]);
run("git", ["-C", sourceDir, "checkout", "--detach", dawnRevision]);
console.log(`Dawn ${dawnRevision} is ready at ${sourceDir}`);

// Runs a setup command and streams progress to the caller.
function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    stdio: "inherit"
  });

  if (result.status !== 0) {
    const printable = [command, ...args].join(" ");
    throw new Error(`${printable} failed with exit code ${result.status}`);
  }
}

// Runs a command whose stdout is needed for checkout-state inspection.
function runCapture(command, args) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    encoding: "utf8"
  });

  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    const printable = [command, ...args].join(" ");
    throw new Error(`${printable} failed with exit code ${result.status}\n${result.stderr}`);
  }
  return result;
}
