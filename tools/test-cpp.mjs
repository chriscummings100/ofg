// Runs native C++ doctest coverage-neutral tests through CMake/CTest.
//
// The command uses the pinned LLVM/Clang and Ninja tools when available, adds
// Windows SDK helper tools for CMake resource/link steps, and registers tests
// through CTest so future C++ test executables can join the same gate.
import { existsSync, readdirSync } from "node:fs";
import { copyFile, mkdir, readFile, rm } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const llvmVersion = (await readFile(path.join(rootDir, "llvm-version.txt"), "utf8")).trim();
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-native");
const emsdkBinDir = path.join(rootDir, "artifacts", "toolchains", "emsdk", "upstream", "bin");
const llvmBinDir = localLlvmBinDir();
const ninjaDir = path.join(rootDir, "artifacts", "toolchains", "ninja");
const clang = findCommand("clang", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang.exe" : "clang"),
  path.join(emsdkBinDir, process.platform === "win32" ? "clang.exe" : "clang")
]);
const clangxx = findCommand("clang++", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang++.exe" : "clang++"),
  path.join(emsdkBinDir, process.platform === "win32" ? "clang++.exe" : "clang++")
]);
const ninja = findCommand("ninja", [
  path.join(ninjaDir, process.platform === "win32" ? "ninja.exe" : "ninja")
]);
const rc = process.platform === "win32" ? findOptionalWindowsSdkTool("rc.exe") : undefined;
const mt = process.platform === "win32" ? findOptionalWindowsSdkTool("mt.exe") : undefined;

await rm(buildDir, { recursive: true, force: true });
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
  "-DCMAKE_BUILD_TYPE=Debug",
  "-DOFG_BUILD_TESTS=ON",
  "-DOFG_BUILD_WASM=OFF"
], env);
run("cmake", ["--build", buildDir, "--target", "ofg_cpp_tests"], env);
run("ctest", ["--test-dir", buildDir, "--output-on-failure"], env);

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
    `Could not find ${command}. Run npm run setup:emscripten and npm run setup:ninja, or place ${command} on PATH.`
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

// Runs a command with inherited stdio so compiler/test output remains visible.
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
