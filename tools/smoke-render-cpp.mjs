// Builds and runs the native C++ Dawn render smoke.
//
// The wrapper reads the shared smoke contract with Node's JSON parser, validates
// an explicitly installed Dawn checkout, and leaves the native executable
// responsible for rendering, PNG writing, report writing, and threshold failures.
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
const dawnRevision = readTrimmed("dawn-version.txt");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-render-smoke");
const dawnSourceDir = resolveDawnSourceDir({ rootDir });
const cmake = findCmake(rootDir);
const ninja = findNinja(rootDir);
const clangTools = findNativeClangTools(rootDir, { frontend: "gnu" });
const rc = findWindowsSdkTool("rc.exe");
const mt = findWindowsSdkTool("mt.exe");
const freshBuild = process.argv.slice(2).some((arg) => arg === "--fresh" || arg === "--clean");

validateDawnSource({ sourceDir: dawnSourceDir, expectedRevision: dawnRevision, rootDir });

const env = {
  ...process.env,
  CC: clangTools.clang,
  CXX: clangTools.clangxx,
  ...(rc ? { RC: rc } : {}),
  PATH: pathWithTools(
    ninja,
    mt,
    rc,
    clangTools.clangxx,
    process.platform === "win32" ? "C:\\Windows\\System32\\cmd.exe" : undefined
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
    ...(rc ? [`-DCMAKE_RC_COMPILER=${cmakePath(rc)}`] : []),
    ...(mt ? [`-DCMAKE_MT=${cmakePath(mt)}`] : []),
    `-DCMAKE_C_COMPILER=${cmakePath(clangTools.clang)}`,
    `-DCMAKE_CXX_COMPILER=${cmakePath(clangTools.clangxx)}`,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOFG_BUILD_TESTS=OFF",
    "-DOFG_BUILD_WASM=OFF",
    "-DOFG_BUILD_NATIVE_SMOKE=ON",
    `-DOFG_DAWN_SOURCE_DIR=${cmakePath(dawnSourceDir)}`
  ],
  { buildDir, cwd: rootDir, env, fresh: freshBuild });
run(cmake, ["--build", buildDir, "--target", "ofg_render_smoke_cpp", "--parallel", "8"], {
  cwd: rootDir,
  env
});

const smokeContract = JSON.parse(
  readFileSync(path.join(rootDir, "tools", "smoke-contract.json"), "utf8")
);
const backgroundReferenceRgba8 =
  smokeContract.backgroundReferenceRgba8 ?? smokeContract.clearColorRgba8;
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
  "--background-reference-rgba8",
  backgroundReferenceRgba8.join(","),
  "--sample-step",
  String(smokeContract.sampleStep),
  "--color-distance-tolerance",
  String(smokeContract.colorDistanceTolerance),
  "--bucket-divisor",
  String(smokeContract.bucketDivisor),
  "--min-scene-ratio",
  String(smokeContract.minSceneRatio),
  "--min-background-ratio",
  String(smokeContract.minBackgroundRatio),
  "--min-ground-ratio",
  String(smokeContract.minGroundRatio),
  "--min-colored-ratio",
  String(smokeContract.minColoredRatio),
  "--min-lower-half-scene-ratio",
  String(smokeContract.minLowerHalfSceneRatio),
  "--min-non-background-color-buckets",
  String(smokeContract.minNonBackgroundColorBuckets)
], { cwd: rootDir, env });

// Reads a source-controlled tool pin.
function readTrimmed(relativePath) {
  return readFileSync(path.join(rootDir, relativePath), "utf8").trim();
}
