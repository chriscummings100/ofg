// Runs native C++ doctest coverage-neutral tests through CMake/CTest.
//
// The command uses installed Clang-family tools and Ninja. It may discover
// Visual Studio's LLVM/Ninja installation on Windows, but it never falls back to
// repository-local toolchain directories.
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  cmakePath,
  configureCmakeIfNeeded,
  findCmake,
  findNativeClangTools,
  findNinja,
  findWindowsSdkTool,
  pathWithTools,
  resolveDawnSourceDir,
  run,
  validateDawnSource
} from "./lib/toolchain.mjs";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-native");
const dawnRevision = readTrimmed("dawn-version.txt");
const dawnSourceDir = resolveDawnSourceDir({ rootDir });
const cmake = findCmake(rootDir);
const ctest = siblingTool(cmake, "ctest");
const ninja = findNinja(rootDir);
const clangTools = findNativeClangTools(rootDir, { frontend: "msvc" });
const rc = findWindowsSdkTool("rc.exe");
const mt = findWindowsSdkTool("mt.exe");
const freshBuild = process.argv.slice(2).some((arg) => arg === "--fresh" || arg === "--clean");

validateDawnSource({ sourceDir: dawnSourceDir, expectedRevision: dawnRevision, rootDir });

const env = {
  ...process.env,
  CC: clangTools.clang,
  CXX: clangTools.clangxx,
  ...(rc ? { RC: rc } : {}),
  PATH: pathWithTools(ninja, mt, rc, clangTools.clangxx)
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
    `-DOFG_DAWN_SOURCE_DIR=${cmakePath(dawnSourceDir)}`
  ],
  { buildDir, cwd: rootDir, env, fresh: freshBuild });
run(cmake, ["--build", buildDir, "--target", "ofg_cpp_tests"], { cwd: rootDir, env });
run(ctest, ["--test-dir", buildDir, "-R", "^ofg_cpp_tests$", "--output-on-failure"], {
  cwd: rootDir,
  env
});

// Uses ctest from the same installed CMake directory when available.
function siblingTool(commandPath, toolName) {
  const candidate = path.join(path.dirname(commandPath), process.platform === "win32" ? `${toolName}.exe` : toolName);
  return existsSync(candidate) ? candidate : toolName;
}

// Reads a source-controlled tool pin.
function readTrimmed(relativePath) {
  return readFileSync(path.join(rootDir, relativePath), "utf8").trim();
}
