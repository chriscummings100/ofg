// Builds the default C++/WASM browser module through CMake and Emscripten.
//
// The script keeps the generated Emscripten output shape deterministic:
// assets/wasm/ofg_cpp/ofg_cpp.js plus ofg_cpp.wasm. It deliberately uses the
// pinned local toolchain when available so the migration is not tied to PATH.
import { existsSync } from "node:fs";
import { mkdir, readFile, rm } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { fileURLToPath } from "node:url";

const rootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const emsdkVersion = (await readFile(path.join(rootDir, "emscripten-version.txt"), "utf8")).trim();
const emsdkDir = process.env.EMSDK || path.join(rootDir, "artifacts", "toolchains", "emsdk");
const ninjaDir = path.join(rootDir, "artifacts", "toolchains", "ninja");
const buildDir = path.join(rootDir, "artifacts", "build", "cpp-wasm");
const outputDir = path.join(rootDir, "assets", "wasm", "ofg_cpp");
const emcmakePath = findEmscriptenCommand("emcmake");
const ninjaPath = findNinja();

await mkdir(outputDir, { recursive: true });
await rm(buildDir, { recursive: true, force: true });
await mkdir(buildDir, { recursive: true });

run(
  emcmakePath,
  [
    "cmake",
    "-S",
    path.join(rootDir, "cpp"),
    "-B",
    buildDir,
    "-G",
    "Ninja",
    `-DCMAKE_MAKE_PROGRAM=${cmakePath(ninjaPath)}`,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOFG_BUILD_TESTS=OFF",
    "-DOFG_BUILD_WASM=ON"
  ],
  emscriptenEnv()
);

run("cmake", ["--build", buildDir, "--target", "ofg_cpp_wasm"], emscriptenEnv());

const jsPath = path.join(outputDir, "ofg_cpp.js");
const wasmPath = path.join(outputDir, "ofg_cpp.wasm");
if (!existsSync(jsPath) || !existsSync(wasmPath)) {
  throw new Error(
    `Expected Emscripten outputs were not created: ${jsPath} and ${wasmPath}`
  );
}

const generatedModule = await import(`${pathToFileURL(jsPath).href}?t=${Date.now()}`);
if (typeof generatedModule.default !== "function") {
  throw new Error("Generated ofg_cpp.js did not expose a default module factory.");
}

console.log(`Generated ${path.relative(rootDir, jsPath)}`);
console.log(`Generated ${path.relative(rootDir, wasmPath)}`);

// Finds an Emscripten helper command from the pinned SDK or PATH.
function findEmscriptenCommand(name) {
  const platformSuffixes = process.platform === "win32" ? [".exe", ".bat", ".cmd", ".py", ""] : [""];
  const commandNames = platformSuffixes.map((suffix) => `${name}${suffix}`);
  const candidates = [
    ...commandNames.map((commandName) =>
      path.join(emsdkDir, "upstream", "emscripten", commandName)
    ),
    ...commandNames
  ];

  const found = candidates.find((candidate) => {
    if (!path.isAbsolute(candidate)) {
      return commandExists(candidate);
    }
    return existsSync(candidate);
  });

  if (found) {
    return found;
  }

  throw new Error(
    [
      `Could not find ${name} for pinned Emscripten ${emsdkVersion}.`,
      `Run npm run setup:emscripten, or set EMSDK to an activated emsdk checkout.`
    ].join(" ")
  );
}

// Checks whether a command name resolves through the platform command lookup.
function commandExists(command) {
  const checkCommand = process.platform === "win32" ? "where" : "which";
  const checkArgs = [command];
  const result = spawnSync(checkCommand, checkArgs, {
    stdio: "ignore"
  });
  return result.status === 0;
}

// Finds Ninja from the pinned toolchain or PATH.
function findNinja() {
  const command = process.platform === "win32" ? "ninja.exe" : "ninja";
  const candidates = [
    path.join(ninjaDir, command),
    command
  ];

  const found = candidates.find((candidate) => {
    if (!path.isAbsolute(candidate)) {
      return commandExists(candidate);
    }
    return existsSync(candidate);
  });

  if (found) {
    return found;
  }

  throw new Error("Could not find Ninja. Run npm run setup:ninja, or place ninja on PATH.");
}

// Builds the environment expected by emcmake/em++.
function emscriptenEnv() {
  const delimiter = path.delimiter;
  const pathEntries = [
    path.dirname(ninjaPath),
    path.join(emsdkDir, "upstream", "emscripten"),
    path.join(emsdkDir, "upstream", "bin"),
    process.env.PATH ?? ""
  ];
  return {
    ...process.env,
    EMSDK: emsdkDir,
    EM_CONFIG: path.join(emsdkDir, ".emscripten"),
    PATH: pathEntries.filter(Boolean).join(delimiter)
  };
}

// Normalizes paths for CMake command-line definitions.
function cmakePath(item) {
  return item.replaceAll("\\", "/");
}

// Runs a command with inherited stdio so build output remains visible.
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
