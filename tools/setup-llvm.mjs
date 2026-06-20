// Installs the pinned desktop LLVM/Clang bundle for native C++ gates.
//
// Browser C++ continues to use Emscripten's Clang; this toolchain is for native
// doctest, coverage, and Dawn smoke work where desktop LLVM tools are required.
import { createWriteStream, existsSync } from "node:fs";
import { chmod, mkdir, readFile, rm } from "node:fs/promises";
import https from "node:https";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const llvmVersion = (await readFile(path.join(rootDir, "llvm-version.txt"), "utf8")).trim();
const toolDir = path.join(rootDir, "artifacts", "toolchains", "llvm");
const archiveName = archiveNameForPlatform();
const extractedDir = path.join(toolDir, archiveName.replace(/\.tar\.xz$/, ""));
const clangName = process.platform === "win32" ? "clang++.exe" : "clang++";
const clangPath = path.join(extractedDir, "bin", clangName);

if (existsSync(clangPath)) {
  console.log(`LLVM ${llvmVersion} is already ready at ${extractedDir}`);
  process.exit(0);
}

const archivePath = path.join(toolDir, archiveName);
const url = `https://github.com/llvm/llvm-project/releases/download/llvmorg-${llvmVersion}/${archiveName}`;

await rm(toolDir, { recursive: true, force: true });
await mkdir(toolDir, { recursive: true });
await download(url, archivePath);
run("tar", ["-xf", archivePath, "-C", toolDir]);

if (process.platform !== "win32") {
  await chmod(clangPath, 0o755);
}

if (!existsSync(clangPath)) {
  throw new Error(`Expected LLVM clang++ binary was not extracted to ${clangPath}`);
}

console.log(`LLVM ${llvmVersion} is ready at ${extractedDir}`);

// Selects the LLVM archive name for the current platform.
function archiveNameForPlatform() {
  switch (process.platform) {
    case "win32":
      return `clang+llvm-${llvmVersion}-x86_64-pc-windows-msvc.tar.xz`;
    case "linux":
      return `LLVM-${llvmVersion}-Linux-X64.tar.xz`;
    case "darwin":
      return process.arch === "arm64"
        ? `LLVM-${llvmVersion}-macOS-ARM64.tar.xz`
        : `LLVM-${llvmVersion}-macOS-X64.tar.xz`;
    default:
      throw new Error(`No pinned LLVM archive configured for ${process.platform}.`);
  }
}

// Downloads an archive while following GitHub release redirects.
function download(url, destination) {
  return new Promise((resolve, reject) => {
    https
      .get(url, (response) => {
        if (response.statusCode && response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
          response.resume();
          download(response.headers.location, destination).then(resolve, reject);
          return;
        }

        if (response.statusCode !== 200) {
          response.resume();
          reject(new Error(`Failed to download ${url}: HTTP ${response.statusCode}`));
          return;
        }

        const file = createWriteStream(destination);
        response.pipe(file);
        file.on("finish", () => {
          file.close(resolve);
        });
        file.on("error", reject);
      })
      .on("error", reject);
  });
}

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
