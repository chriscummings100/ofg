// Runs Clang source-based coverage for native-checkable C++ runtime code.
//
// Browser-only WebGPU code is validated by build and smoke gates; this script
// focuses line coverage on portable C++ core/runtime/scene files that doctest
// can execute natively.
import { existsSync, readdirSync } from "node:fs";
import { copyFile, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const llvmVersion = (await readFile(path.join(rootDir, "llvm-version.txt"), "utf8")).trim();
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-coverage");
const coverageDir = path.join(rootDir, "artifacts", "coverage", "cpp");
const llvmBinDir = localLlvmBinDir();
const ninjaDir = path.join(rootDir, "artifacts", "toolchains", "ninja");
const clang = findCommand("clang", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang-cl.exe" : "clang"),
  path.join(llvmBinDir, process.platform === "win32" ? "clang.exe" : "clang")
]);
const clangxx = findCommand("clang++", [
  path.join(llvmBinDir, process.platform === "win32" ? "clang-cl.exe" : "clang++"),
  path.join(llvmBinDir, process.platform === "win32" ? "clang++.exe" : "clang++")
]);
const llvmProfdata = findCommand("llvm-profdata", [
  path.join(llvmBinDir, process.platform === "win32" ? "llvm-profdata.exe" : "llvm-profdata")
]);
const llvmCov = findCommand("llvm-cov", [
  path.join(llvmBinDir, process.platform === "win32" ? "llvm-cov.exe" : "llvm-cov")
]);
const ninja = findCommand("ninja", [
  path.join(ninjaDir, process.platform === "win32" ? "ninja.exe" : "ninja")
]);
const rc = process.platform === "win32" ? findOptionalWindowsSdkTool("rc.exe") : undefined;
const mt = process.platform === "win32" ? findOptionalWindowsSdkTool("mt.exe") : undefined;

await rm(buildDir, { recursive: true, force: true });
await rm(coverageDir, { recursive: true, force: true });
await mkdir(buildDir, { recursive: true });
await mkdir(coverageDir, { recursive: true });
await ensureWindowsLldLinkAlias(path.dirname(clangxx));
await ensureWindowsProfileRuntime();

const env = {
  ...process.env,
  CC: clang,
  CXX: clangxx,
  LLVM_PROFILE_FILE: path.join(coverageDir, "ofg-cpp-%p.profraw"),
  ...(rc ? { RC: rc } : {}),
  PATH: [
    path.dirname(ninja),
    ...(mt ? [path.dirname(mt)] : []),
    ...(rc ? [path.dirname(rc)] : []),
    path.dirname(clangxx),
    path.dirname(llvmProfdata),
    path.dirname(llvmCov),
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
  "-DOFG_BUILD_WASM=OFF",
  "-DOFG_ENABLE_COVERAGE=ON",
  ...(process.platform === "win32" ? [`-DOFG_CLANG_PROFILE_LIB_DIR=${cmakePath(clangProfileLibDir())}`] : [])
], env);
run("cmake", ["--build", buildDir, "--target", "ofg_cpp_tests"], env);
run("ctest", ["--test-dir", buildDir, "--output-on-failure"], env);

const profrawFiles = readdirSync(coverageDir)
  .filter((file) => file.endsWith(".profraw"))
  .map((file) => path.join(coverageDir, file));
if (profrawFiles.length === 0) {
  throw new Error("C++ coverage did not produce any .profraw files.");
}

const profdataPath = path.join(coverageDir, "cpp.profdata");
const summaryPath = path.join(coverageDir, "cpp-summary.json");
const testBinary = path.join(buildDir, process.platform === "win32" ? "ofg_cpp_tests.exe" : "ofg_cpp_tests");

run(llvmProfdata, ["merge", "-sparse", ...profrawFiles, "-o", profdataPath], env);
const exportResult = runCapture(llvmCov, [
  "export",
  testBinary,
  `-instr-profile=${profdataPath}`,
  "-format=text"
], env);
await writeFile(summaryPath, exportResult.stdout);

const report = JSON.parse(await readFile(summaryPath, "utf8"));
const checkedFiles = collectCheckedFiles(report);
const failures = checkedFiles.filter((file) => file.percent < 90);
if (failures.length > 0) {
  const details = failures
    .map((file) => `${path.relative(rootDir, file.path)}: ${file.percent.toFixed(2)}%`)
    .join(", ");
  throw new Error(`C++ coverage failed for checked files: ${details}`);
}

for (const file of checkedFiles) {
  console.log(`${path.relative(rootDir, file.path)} line coverage ${file.percent.toFixed(2)}%`);
}
console.log(`C++ coverage summary written to ${path.relative(rootDir, summaryPath)}`);

// Extracts per-file line coverage for source files covered by this gate.
function collectCheckedFiles(report) {
  const files = report.data?.[0]?.files ?? [];
  return files
    .map((file) => ({
      path: path.resolve(file.filename),
      percent: Number(file.summary?.lines?.percent ?? 0)
    }))
    .filter((file) =>
      isUnder(file.path, path.join(rootDir, "cpp", "src", "core")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "runtime")) ||
      file.path === path.join(rootDir, "cpp", "src", "render", "bootstrap_scene.cpp")
    );
}

// Reports whether a file path is inside a parent directory.
function isUnder(candidate, parent) {
  const relative = path.relative(parent, candidate);
  return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
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

// Returns the Clang resource subdirectory expected by CMake coverage flags.
function clangProfileLibDir() {
  const resourceDirResult = spawnSync(clangxx, ["-print-resource-dir"], {
    encoding: "utf8"
  });
  if (resourceDirResult.status !== 0) {
    throw new Error("Could not discover Clang resource directory for C++ coverage.");
  }
  return path.join(resourceDirResult.stdout.trim(), "lib", "x86_64-pc-windows-msvc");
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

// Ensures Windows Clang coverage can find clang_rt.profile.lib.
async function ensureWindowsProfileRuntime() {
  if (process.platform !== "win32") {
    return;
  }

  const resourceDirResult = spawnSync(clangxx, ["-print-resource-dir"], {
    encoding: "utf8"
  });
  if (resourceDirResult.status !== 0) {
    throw new Error("Could not discover Clang resource directory for C++ coverage.");
  }
  const resourceDir = resourceDirResult.stdout.trim();
  const destinationDir = path.join(resourceDir, "lib", "x86_64-pc-windows-msvc");
  const destination = path.join(destinationDir, "clang_rt.profile.lib");
  const bundledRuntime = path.join(resourceDir, "lib", "windows", "clang_rt.profile-x86_64.lib");
  if (existsSync(bundledRuntime)) {
    await mkdir(destinationDir, { recursive: true });
    await copyFile(bundledRuntime, destination);
    return;
  }

  const source = findWindowsProfileRuntime();
  if (!source) {
    throw new Error(
      "Could not find clang_rt.profile-x86_64.lib. Install desktop LLVM/Clang or Visual Studio BuildTools with Clang profiling runtime."
    );
  }
  await mkdir(destinationDir, { recursive: true });
  await copyFile(source, destination);
}

// Searches common Visual Studio roots for a compatible Clang profile runtime.
function findWindowsProfileRuntime() {
  const baseDirs = [
    "C:\\Program Files (x86)\\Microsoft Visual Studio",
    "C:\\Program Files\\Microsoft Visual Studio"
  ];
  const matches = [];
  for (const baseDir of baseDirs) {
    collectProfileRuntimeMatches(baseDir, matches);
  }
  matches.sort((left, right) =>
    right.localeCompare(left, undefined, { numeric: true, sensitivity: "base" })
  );
  return matches[0];
}

// Recursively collects candidate Clang profile runtime libraries.
function collectProfileRuntimeMatches(directory, matches) {
  if (!existsSync(directory)) {
    return;
  }

  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const fullPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      collectProfileRuntimeMatches(fullPath, matches);
    } else if (entry.name === "clang_rt.profile-x86_64.lib") {
      matches.push(fullPath);
    }
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

// Runs a command whose stdout is needed for coverage export.
function runCapture(command, args, env) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    env,
    encoding: "utf8",
    maxBuffer: 64 * 1024 * 1024
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
