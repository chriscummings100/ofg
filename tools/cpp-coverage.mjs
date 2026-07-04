// Runs Clang source-based coverage for native-checkable C++ runtime code.
//
// Browser-only WebGPU code is validated by build and smoke gates; this script
// focuses line coverage on portable C++ core/runtime/resource/render files that
// doctest can execute natively. It uses installed LLVM tools and does not mutate
// the compiler installation.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  clangProfileRuntimePath,
  cmakePath,
  configureCmakeIfNeeded,
  findCmake,
  findNativeClangTools,
  findNinja,
  findWindowsSdkTool,
  pathWithTools,
  resolveDawnSourceDir,
  run,
  runCapture,
  validateDawnSource
} from "./lib/toolchain.mjs";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-coverage");
const coverageDir = path.join(rootDir, "artifacts", "coverage", "cpp");
const lineCoverageExclusions = new Map([
  [
    "cpp/src/render/scene_color_target.cpp",
    new Set([15, 18, 32, 40, 55, 116])
  ],
  [
    "cpp/src/render/sky_pass.cpp",
    new Set([35, 57, 71, 117, 131, 152, 167, 168, 194, 298, 299, 302, 305, 308, 311, 314])
  ],
  [
    "cpp/src/render/tone_map_pass.cpp",
    new Set([29, 60, 74, 111, 125, 149, 157, 215, 216, 219, 222, 225, 228, 283])
  ]
]);
const dawnRevision = readTrimmed("dawn-version.txt");
const dawnSourceDir = resolveDawnSourceDir({ rootDir });
const cmake = findCmake(rootDir);
const ctest = siblingTool(cmake, "ctest");
const ninja = findNinja(rootDir);
const clangTools = findNativeClangTools(rootDir, { frontend: "msvc" });
const rc = findWindowsSdkTool("rc.exe");
const mt = findWindowsSdkTool("mt.exe");
const profileRuntimePath =
  process.platform === "win32" ? clangProfileRuntimePath(clangTools.clangxx) : undefined;
const freshBuild = process.argv.slice(2).some((arg) => arg === "--fresh" || arg === "--clean");

validateDawnSource({ sourceDir: dawnSourceDir, expectedRevision: dawnRevision, rootDir });

await rm(coverageDir, { recursive: true, force: true });
await mkdir(coverageDir, { recursive: true });

const env = {
  ...process.env,
  CC: clangTools.clang,
  CXX: clangTools.clangxx,
  LLVM_PROFILE_FILE: path.join(coverageDir, "ofg-cpp-%p.profraw"),
  ...(rc ? { RC: rc } : {}),
  PATH: pathWithTools(
    ninja,
    mt,
    rc,
    clangTools.clangxx,
    clangTools.llvmProfdata,
    clangTools.llvmCov
  )
};

configureCmakeIfNeeded(cmake,
  [
    "-S",
    path.join(rootDir, "cpp"),
    "-B",
    buildDir,
    "-G",
    "Ninja",
    `-DCMAKE_MAKE_PROGRAM=${cmakePath(ninja)}`,
    `-DCMAKE_CXX_COMPILER=${cmakePath(clangTools.clangxx)}`,
    ...(rc ? [`-DCMAKE_RC_COMPILER=${cmakePath(rc)}`] : []),
    ...(mt ? [`-DCMAKE_MT=${cmakePath(mt)}`] : []),
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DOFG_BUILD_TESTS=ON",
    "-DOFG_BUILD_WASM=OFF",
    "-DOFG_ENABLE_COVERAGE=ON",
    `-DOFG_DAWN_SOURCE_DIR=${cmakePath(dawnSourceDir)}`,
    ...(profileRuntimePath
      ? [`-DOFG_CLANG_PROFILE_LIB_PATH=${cmakePath(profileRuntimePath)}`]
      : [])
  ],
  { buildDir, cwd: rootDir, env, fresh: freshBuild });
run(cmake, ["--build", buildDir, "--target", "ofg_cpp_tests"], { cwd: rootDir, env });
run(ctest, ["--test-dir", buildDir, "-R", "^ofg_cpp_tests$", "--output-on-failure"], {
  cwd: rootDir,
  env
});

const profrawFiles = readdirSync(coverageDir)
  .filter((file) => file.endsWith(".profraw"))
  .map((file) => path.join(coverageDir, file));
if (profrawFiles.length === 0) {
  throw new Error("C++ coverage did not produce any .profraw files.");
}

const profdataPath = path.join(coverageDir, "cpp.profdata");
const summaryPath = path.join(coverageDir, "cpp-summary.json");
const testBinary = path.join(buildDir, process.platform === "win32" ? "ofg_cpp_tests.exe" : "ofg_cpp_tests");

run(clangTools.llvmProfdata, ["merge", "-sparse", ...profrawFiles, "-o", profdataPath], {
  cwd: rootDir,
  env
});
const exportResult = runCapture(clangTools.llvmCov, [
  "export",
  testBinary,
  `-instr-profile=${profdataPath}`,
  "-format=text"
], {
  cwd: rootDir,
  env,
  maxBuffer: 64 * 1024 * 1024
});
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
  const exceptionNote =
    file.excludedLineCount > 0 ? ` (${file.excludedLineCount} defensive lines excluded)` : "";
  console.log(`${path.relative(rootDir, file.path)} line coverage ${file.percent.toFixed(2)}%${exceptionNote}`);
}
console.log(`C++ coverage summary written to ${path.relative(rootDir, summaryPath)}`);

// Extracts per-file line coverage for source files covered by this gate.
//
// glTF parser/importer files under cpp/src/assets are intentionally outside
// this per-file gate for now because their useful contract coverage comes from
// fixture-driven importer tests, player asset audits, and browser/native smoke.
// COVERAGE.md and the active glTF plan record that exception.
function collectCheckedFiles(report) {
  const files = report.data?.[0]?.files ?? [];
  return files
    .map((file) => {
      const resolvedPath = path.resolve(file.filename);
      const relativePath = normalizePath(path.relative(rootDir, resolvedPath));
      const lines = file.summary?.lines ?? {};
      const lineCount = Number(lines.count ?? 0);
      const coveredLineCount = Number(lines.covered ?? 0);
      const excludedLineCount = lineCoverageExclusions.get(relativePath)?.size ?? 0;
      const checkedLineCount = lineCount - excludedLineCount;
      return {
        path: resolvedPath,
        percent: checkedLineCount <= 0 ? 0 : (coveredLineCount / checkedLineCount) * 100,
        excludedLineCount
      };
    })
    .filter((file) =>
      isUnder(file.path, path.join(rootDir, "cpp", "src", "animation")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "core")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "gpu")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "math")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "resources")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "scene")) ||
      file.path === path.join(rootDir, "cpp", "src", "game", "game_runtime.cpp") ||
      file.path === path.join(rootDir, "cpp", "src", "game", "render_target.cpp") ||
      file.path === path.join(rootDir, "cpp", "src", "runtime", "runtime_debug_status.cpp") ||
      (file.path.endsWith(".cpp") && isUnder(file.path, path.join(rootDir, "cpp", "src", "render")))
    );
}

// Normalizes paths for stable exception keys across Windows and POSIX hosts.
function normalizePath(value) {
  return value.replaceAll("\\", "/");
}

// Reports whether a file path is inside a parent directory.
function isUnder(candidate, parent) {
  const relative = path.relative(parent, candidate);
  return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
}

// Uses ctest from the same installed CMake directory when available.
function siblingTool(commandPath, toolName) {
  const candidate = path.join(path.dirname(commandPath), process.platform === "win32" ? `${toolName}.exe` : toolName);
  return existsSync(candidate) ? candidate : toolName;
}

// Reads a source-controlled tool pin.
function readTrimmed(relativePath) {
  return readFileSync(path.join(rootDir, relativePath), "utf8").trim();
}
