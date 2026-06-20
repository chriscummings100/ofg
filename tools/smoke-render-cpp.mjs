// Builds and runs the native C++ Dawn render smoke.
//
// The wrapper keeps CMake/Dawn setup behind the npm command, reads the shared
// smoke contract with Node's JSON parser, forwards those values to the native
// executable, and leaves the executable responsible for rendering, PNG writing,
// report writing, and threshold failures.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { copyFile, mkdir } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const llvmVersion = readTrimmed("llvm-version.txt");
const dawnRevision = readTrimmed("dawn-version.txt");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-render-smoke");
const dawnSourceDir = path.join(rootDir, "artifacts", "toolchains", "dawn", "src");
const llvmBinDir = localLlvmBinDir();
const ninjaDir = path.join(rootDir, "artifacts", "toolchains", "ninja");
const clang = findCommand("clang", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang.exe" : "clang")
]);
const clangxx = findCommand("clang++", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang++.exe" : "clang++")
]);
const ninja = findCommand("ninja", [
  path.join(ninjaDir, process.platform === "win32" ? "ninja.exe" : "ninja")
]);
const rc = process.platform === "win32" ? findOptionalWindowsSdkTool("rc.exe") : undefined;
const mt = process.platform === "win32" ? findOptionalWindowsSdkTool("mt.exe") : undefined;

await ensureDawnSource();
await mkdir(buildDir, { recursive: true });
await ensureWindowsLldLinkAlias(path.dirname(clangxx));

const env = {
  ...process.env,
  CC: clang,
  CXX: clangxx,
  ...(rc ? { RC: rc } : {}),
  PATH: [
    path.dirname(ninja),
    ...(mt ? [path.dirname(mt)] : []),
    ...(rc ? [path.dirname(rc)] : []),
    path.dirname(clangxx),
    ...(process.platform === "win32" ? ["C:\\Windows\\System32"] : []),
    process.env.PATH ?? ""
  ].join(path.delimiter)
};

run("cmake", [
  "-S",
  path.join(rootDir, "cpp"),
  "-B",
  buildDir,
  "-G",
  "Ninja",
  `-DCMAKE_MAKE_PROGRAM=${cmakePath(ninja)}`,
  ...(rc ? [`-DCMAKE_RC_COMPILER=${cmakePath(rc)}`] : []),
  ...(mt ? [`-DCMAKE_MT=${cmakePath(mt)}`] : []),
  `-DCMAKE_C_COMPILER=${cmakePath(clang)}`,
  `-DCMAKE_CXX_COMPILER=${cmakePath(clangxx)}`,
  "-DCMAKE_BUILD_TYPE=Release",
  "-DOFG_BUILD_TESTS=OFF",
  "-DOFG_BUILD_WASM=OFF",
  "-DOFG_BUILD_NATIVE_SMOKE=ON",
  `-DOFG_DAWN_SOURCE_DIR=${cmakePath(dawnSourceDir)}`
], env);
run("cmake", ["--build", buildDir, "--target", "ofg_render_smoke_cpp", "--parallel", "8"], env);

const smokeContract = JSON.parse(
  readFileSync(path.join(rootDir, "tools", "smoke-contract.json"), "utf8")
);
const executable = path.join(
  buildDir,
  process.platform === "win32" ? "ofg_render_smoke_cpp.exe" : "ofg_render_smoke_cpp"
);
if (!existsSync(executable)) {
  throw new Error(`Expected native smoke executable was not built: ${executable}`);
}

run(executable, [
  "--out",
  path.join(rootDir, "artifacts", "render-smoke"),
  "--width",
  String(smokeContract.width),
  "--height",
  String(smokeContract.height),
  "--clear-color-rgba8",
  smokeContract.clearColorRgba8.join(","),
  "--sample-step",
  String(smokeContract.sampleStep),
  "--color-distance-tolerance",
  String(smokeContract.colorDistanceTolerance),
  "--bucket-divisor",
  String(smokeContract.bucketDivisor),
  "--min-triangle-ratio",
  String(smokeContract.minTriangleRatio),
  "--min-background-ratio",
  String(smokeContract.minBackgroundRatio),
  "--min-non-background-color-buckets",
  String(smokeContract.minNonBackgroundColorBuckets)
], env);

// Reads a source-controlled tool pin.
function readTrimmed(relativePath) {
  return readFileSync(path.join(rootDir, relativePath), "utf8").trim();
}

// Ensures the generated Dawn checkout exists at the pinned revision.
async function ensureDawnSource() {
  if (existsSync(path.join(dawnSourceDir, ".git"))) {
    const current = runCapture("git", ["-C", dawnSourceDir, "rev-parse", "HEAD"]).stdout.trim();
    if (current === dawnRevision) {
      return;
    }
  }
  run(process.execPath, [path.join(rootDir, "tools", "setup-dawn.mjs")], process.env);
}

// Finds a required command from preferred local toolchain paths or PATH.
function findCommand(command, candidates = []) {
  const candidate = candidates.find((item) => existsSync(item));
  if (candidate) {
    return candidate;
  }

  const result = spawnSync(process.platform === "win32" ? "where" : "which", [command], {
    encoding: "utf8"
  });

  if (result.status === 0) {
    const first = result.stdout.split(/\r?\n/).find(Boolean);
    if (first && existsSync(first)) {
      return first;
    }
    return command;
  }

  throw new Error(
    `Could not find ${command}. Run npm run setup:llvm and npm run setup:ninja, or place ${command} on PATH.`
  );
}

// Finds Windows SDK resource tools for CMake when they are installed.
function findOptionalWindowsSdkTool(toolName) {
  const baseDir = "C:\\Program Files (x86)\\Windows Kits\\10\\bin";
  if (!existsSync(baseDir)) {
    return undefined;
  }

  const versions = readdirSync(baseDir, { withFileTypes: true })
    .filter((item) => item.isDirectory())
    .map((item) => item.name)
    .sort((left, right) =>
      right.localeCompare(left, undefined, { numeric: true, sensitivity: "base" })
    );

  for (const version of versions) {
    const candidate = path.join(baseDir, version, "x64", toolName);
    if (existsSync(candidate)) {
      return candidate;
    }
  }

  return undefined;
}

// Returns the platform-specific pinned LLVM binary directory.
function localLlvmBinDir() {
  const archiveBase = archiveBaseNameForPlatform();
  return path.join(rootDir, "artifacts", "toolchains", "llvm", archiveBase, "bin");
}

// Maps the pinned LLVM version to the archive layout extracted by setup:llvm.
function archiveBaseNameForPlatform() {
  switch (process.platform) {
    case "win32":
      return `clang+llvm-${llvmVersion}-x86_64-pc-windows-msvc`;
    case "linux":
      return `LLVM-${llvmVersion}-Linux-X64`;
    case "darwin":
      return process.arch === "arm64"
        ? `LLVM-${llvmVersion}-macOS-ARM64`
        : `LLVM-${llvmVersion}-macOS-X64`;
    default:
      return "";
  }
}

// Normalizes paths for CMake command-line definitions.
function cmakePath(item) {
  return item.replaceAll("\\", "/");
}

// Adds the lld-link name CMake expects when only lld.exe is present.
async function ensureWindowsLldLinkAlias(binDir) {
  if (process.platform !== "win32") {
    return;
  }

  const lld = path.join(binDir, "lld.exe");
  const lldLink = path.join(binDir, "lld-link.exe");
  if (existsSync(lld) && !existsSync(lldLink)) {
    await copyFile(lld, lldLink);
  }
}

// Runs a command with inherited stdio so compiler progress remains visible.
function run(command, args, env) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    env,
    stdio: "inherit"
  });

  if (result.status !== 0) {
    const printable = [command, ...args].join(" ");
    throw new Error(`${printable} failed with exit code ${result.status}`);
  }
}

// Runs a command whose stdout is needed for setup checks.
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
