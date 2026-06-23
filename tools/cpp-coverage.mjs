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

validateDawnSource({ sourceDir: dawnSourceDir, expectedRevision: dawnRevision, rootDir });

await rm(buildDir, { recursive: true, force: true });
await rm(coverageDir, { recursive: true, force: true });
await mkdir(buildDir, { recursive: true });
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

run(cmake, [
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
], { cwd: rootDir, env });
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
      isUnder(file.path, path.join(rootDir, "cpp", "src", "math")) ||
      isUnder(file.path, path.join(rootDir, "cpp", "src", "resources")) ||
      file.path === path.join(rootDir, "cpp", "src", "game", "game_runtime.cpp") ||
      file.path === path.join(rootDir, "cpp", "src", "game", "render_target.cpp") ||
      file.path === path.join(rootDir, "cpp", "src", "runtime", "runtime_debug_status.cpp") ||
      [
        "bootstrap_scene.cpp",
        "camera.cpp",
        "demo_scene.cpp",
        "draw_list.cpp",
        "opaque_pass.cpp",
        "pipeline_cache.cpp",
        "renderer.cpp"
      ].includes(path.basename(file.path)) && isUnder(file.path, path.join(rootDir, "cpp", "src", "render"))
    );
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
