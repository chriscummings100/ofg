// Installs the pinned Ninja build tool used by CMake commands.
//
// Ninja is a generator/build executor, not another compiler toolchain; pinning
// it keeps CMake behavior consistent across developer machines.
import { createWriteStream, existsSync } from "node:fs";
import { chmod, mkdir, readFile, rm } from "node:fs/promises";
import https from "node:https";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const ninjaVersion = (await readFile(path.join(rootDir, "ninja-version.txt"), "utf8")).trim();
const toolDir = path.join(rootDir, "artifacts", "toolchains", "ninja");
const binaryName = process.platform === "win32" ? "ninja.exe" : "ninja";
const binaryPath = path.join(toolDir, binaryName);

if (existsSync(binaryPath)) {
  console.log(`Ninja ${ninjaVersion} is already ready at ${binaryPath}`);
  process.exit(0);
}

const archiveName = archiveNameForPlatform();
const archivePath = path.join(toolDir, archiveName);
const url = `https://github.com/ninja-build/ninja/releases/download/v${ninjaVersion}/${archiveName}`;

await rm(toolDir, { recursive: true, force: true });
await mkdir(toolDir, { recursive: true });
await download(url, archivePath);
run("tar", ["-xf", archivePath, "-C", toolDir]);

if (process.platform !== "win32") {
  await chmod(binaryPath, 0o755);
}

if (!existsSync(binaryPath)) {
  throw new Error(`Expected Ninja binary was not extracted to ${binaryPath}`);
}

console.log(`Ninja ${ninjaVersion} is ready at ${binaryPath}`);

// Selects the Ninja archive name for the current platform.
function archiveNameForPlatform() {
  switch (process.platform) {
    case "win32":
      return "ninja-win.zip";
    case "linux":
      return "ninja-linux.zip";
    case "darwin":
      return "ninja-mac.zip";
    default:
      throw new Error(`No pinned Ninja archive configured for ${process.platform}.`);
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
