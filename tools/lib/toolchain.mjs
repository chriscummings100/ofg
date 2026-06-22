// Shared discovery helpers for OFG build wrappers.
//
// These helpers find already-installed tools from PATH, explicit environment
// variables, and common Windows installation locations. They deliberately reject
// repository-local toolchain folders.
import { existsSync, readdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";

// Normalizes paths for CMake command-line definitions.
export function cmakePath(item) {
  return item.replaceAll("\\", "/");
}

// Runs a command with inherited stdio so compiler/test output remains visible.
export function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    stdio: "inherit",
    ...options
  });

  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    const printable = [command, ...args].join(" ");
    throw new Error(`${printable} failed with exit code ${result.status}`);
  }
}

// Runs a command whose stdout is needed by a wrapper.
export function runCapture(command, args, options = {}) {
  const result = spawnSync(command, args, {
    encoding: "utf8",
    ...options
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

// Finds the CMake executable from PATH or common Visual Studio install roots.
export function findCmake(rootDir) {
  return requiredCommand("cmake", {
    rootDir,
    candidates: visualStudioToolCandidates(["Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", exe("cmake")]),
    installHint: "Install CMake and add it to PATH."
  });
}

// Finds Ninja from PATH or common Visual Studio install roots.
export function findNinja(rootDir) {
  return requiredCommand("ninja", {
    rootDir,
    candidates: [
      process.env.NINJA,
      ...visualStudioToolCandidates(["Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "Ninja", exe("ninja")]),
      "C:/tools/ninja/ninja.exe"
    ],
    installHint: "Install Ninja or use Visual Studio's bundled Ninja."
  });
}

// Finds Emscripten helper commands from an activated SDK or EMSDK.
export function findEmscriptenCommand(name, rootDir) {
  const commandNames = executableNames(name);
  const emsdkCandidates = process.env.EMSDK
    ? commandNames.map((commandName) =>
        path.join(process.env.EMSDK, "upstream", "emscripten", commandName)
      )
    : [];
  return requiredCommand(name, {
    rootDir,
    candidates: emsdkCandidates,
    installHint: "Activate the installed Emscripten SDK, or set EMSDK to its install root."
  });
}

// Builds an environment for Emscripten commands without repository fallbacks.
export function emscriptenEnv({ emcmake, ninja }) {
  const inferredEmsdk = process.env.EMSDK || inferEmsdkRoot(emcmake);
  const pathEntries = [
    path.dirname(ninja),
    inferredEmsdk ? path.join(inferredEmsdk, "upstream", "emscripten") : undefined,
    inferredEmsdk ? path.join(inferredEmsdk, "upstream", "bin") : undefined,
    process.env.PATH ?? ""
  ];
  return {
    ...process.env,
    ...(inferredEmsdk ? { EMSDK: inferredEmsdk } : {}),
    ...(inferredEmsdk ? { EM_CONFIG: path.join(inferredEmsdk, ".emscripten") } : {}),
    PATH: pathEntries.filter(Boolean).join(path.delimiter)
  };
}

// Fails before Emscripten can fetch a missing port during a normal build.
export function requireEmscriptenPort({ emcmake, portName }) {
  const emsdkRoot = process.env.EMSDK || inferEmsdkRoot(emcmake);
  if (!emsdkRoot) {
    throw new Error("Could not infer EMSDK root while checking Emscripten ports.");
  }
  const portDir = path.join(emsdkRoot, "upstream", "emscripten", "cache", "ports", portName);
  if (!existsSync(portDir)) {
    throw new Error(
      `Emscripten port ${portName} is not present in the installed SDK cache at ${portDir}. Preload the port in the system SDK before running OFG builds.`
    );
  }
}

// Finds native LLVM/Clang tools, preferring Visual Studio LLVM on Windows.
export function findNativeClangTools(rootDir, { frontend = "gnu" } = {}) {
  const useClangCl = process.platform === "win32" && frontend === "msvc";
  const compilerName = useClangCl ? "clang-cl" : "clang++";
  const cCompilerName = useClangCl ? "clang-cl" : "clang";

  return {
    clang: requiredCommand(cCompilerName, {
      rootDir,
      candidates: nativeLlvmCandidates(exe(cCompilerName)),
      installHint: "Install desktop LLVM/Clang or Visual Studio C++ LLVM tools."
    }),
    clangxx: requiredCommand(compilerName, {
      rootDir,
      candidates: nativeLlvmCandidates(exe(compilerName)),
      installHint: "Install desktop LLVM/Clang or Visual Studio C++ LLVM tools."
    }),
    llvmCov: requiredCommand("llvm-cov", {
      rootDir,
      candidates: nativeLlvmCandidates(exe("llvm-cov")),
      installHint: "Install desktop LLVM/Clang with llvm-cov."
    }),
    llvmProfdata: requiredCommand("llvm-profdata", {
      rootDir,
      candidates: nativeLlvmCandidates(exe("llvm-profdata")),
      installHint: "Install desktop LLVM/Clang with llvm-profdata."
    })
  };
}

// Finds Windows SDK resource helpers used by CMake.
export function findWindowsSdkTool(toolName) {
  if (process.platform !== "win32") {
    return undefined;
  }
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

// Finds the installed Clang profiling runtime without copying it anywhere.
export function clangProfileRuntimePath(clangxx) {
  const resourceDirResult = runCapture(clangxx, ["-print-resource-dir"]);
  const resourceDir = resourceDirResult.stdout.trim();
  const direct = path.join(resourceDir, "lib", "windows", "clang_rt.profile-x86_64.lib");
  if (existsSync(direct)) {
    return direct;
  }

  const matches = [];
  collectMatches(path.dirname(resourceDir), "clang_rt.profile-x86_64.lib", matches);
  matches.sort((left, right) =>
    right.localeCompare(left, undefined, { numeric: true, sensitivity: "base" })
  );
  if (matches[0]) {
    return matches[0];
  }
  throw new Error("Could not find clang_rt.profile-x86_64.lib in the installed Clang resource tree.");
}

// Resolves the installed Dawn checkout used by native WebGPU C++ builds.
export function resolveDawnSourceDir({ rootDir } = {}) {
  const configured = process.env.OFG_DAWN_SOURCE_DIR;
  const fallback = process.platform === "win32" ? "C:\\tools\\dawn" : undefined;
  const candidates = [configured, fallback].filter(Boolean);

  for (const candidate of candidates) {
    const resolved = path.resolve(candidate);
    if (existsSync(path.join(resolved, "CMakeLists.txt"))) {
      if (rootDir) {
        rejectRepoToolchain(rootDir, resolved);
      }
      if (!configured && candidate === fallback) {
        console.warn(
          "OFG_DAWN_SOURCE_DIR is not set; using C:\\tools\\dawn. Set OFG_DAWN_SOURCE_DIR to use a different installed Dawn checkout."
        );
      }
      return resolved;
    }
  }

  if (configured) {
    throw new Error(`OFG_DAWN_SOURCE_DIR does not look like a Dawn checkout: ${path.resolve(configured)}`);
  }
  throw new Error(
    "OFG_DAWN_SOURCE_DIR must point to an installed Dawn checkout. Example: $env:OFG_DAWN_SOURCE_DIR='C:\\tools\\dawn'"
  );
}

// Verifies the Dawn checkout shape and reports source-controlled revision drift.
export function validateDawnSource({ sourceDir, expectedRevision, rootDir }) {
  if (!existsSync(path.join(sourceDir, "CMakeLists.txt"))) {
    throw new Error(`OFG_DAWN_SOURCE_DIR does not look like a Dawn checkout: ${sourceDir}`);
  }
  if (!existsSync(path.join(sourceDir, ".git"))) {
    console.warn(`Dawn checkout is not a Git checkout, so revision drift cannot be reported: ${sourceDir}`);
    return;
  }
  const current = runCapture("git", ["-C", sourceDir, "rev-parse", "HEAD"], {
    cwd: rootDir
  }).stdout.trim();
  if (expectedRevision && current !== expectedRevision) {
    console.warn(
      `Dawn checkout revision ${current} differs from dawn-version.txt ${expectedRevision}; using installed Dawn checkout.`
    );
  }
}

// Resolves a required command and rejects repository-local toolchain paths.
export function requiredCommand(command, { rootDir, candidates = [], installHint }) {
  const candidateList = [
    ...lookupCommand(command),
    ...candidates.filter(Boolean)
  ];

  for (const candidate of candidateList) {
    const resolved = resolveExistingCommand(candidate);
    if (resolved) {
      rejectRepoToolchain(rootDir, resolved);
      return resolved;
    }
  }

  throw new Error(`Could not find ${command}. ${installHint}`);
}

// Adds unique directory paths ahead of the existing PATH.
export function pathWithTools(...tools) {
  const directories = [];
  for (const tool of tools.filter(Boolean)) {
    const directory = path.dirname(tool);
    if (!directories.some((item) => item.toLowerCase() === directory.toLowerCase())) {
      directories.push(directory);
    }
  }
  directories.push(process.env.PATH ?? "");
  return directories.join(path.delimiter);
}

// Converts a command basename to its platform executable name.
function exe(name) {
  return process.platform === "win32" ? `${name}.exe` : name;
}

// Returns platform command names for Emscripten helpers.
function executableNames(name) {
  if (process.platform !== "win32") {
    return [name];
  }
  return [".exe", ".cmd", ".bat", ".py", ""].map((suffix) => `${name}${suffix}`);
}

// Uses the platform command lookup to enumerate PATH matches.
function lookupCommand(command) {
  const result = spawnSync(process.platform === "win32" ? "where" : "which", [command], {
    encoding: "utf8"
  });
  if (result.status !== 0) {
    return [];
  }
  return result.stdout.split(/\r?\n/).filter(Boolean);
}

// Resolves an executable path or PATH command candidate.
function resolveExistingCommand(candidate) {
  if (!candidate) {
    return undefined;
  }
  if (path.isAbsolute(candidate)) {
    return existsSync(candidate) ? path.resolve(candidate) : undefined;
  }
  const matches = lookupCommand(candidate);
  return matches[0] ? path.resolve(matches[0]) : undefined;
}

// Refuses to use old repository-owned toolchain directories.
function rejectRepoToolchain(rootDir, candidate) {
  const toolchainRoot = path.join(rootDir, "artifacts", "toolchains");
  const relative = path.relative(toolchainRoot, candidate);
  if (relative === "" || (!relative.startsWith("..") && !path.isAbsolute(relative))) {
    throw new Error(`Refusing to use repository-local toolchain path: ${candidate}`);
  }
}

// Produces candidate paths under common Visual Studio roots.
function visualStudioToolCandidates(parts) {
  return visualStudioRoots().map((root) => path.join(root, ...parts));
}

// Produces LLVM candidate paths from environment and Windows install roots.
function nativeLlvmCandidates(fileName) {
  const roots = [
    process.env.LLVM_ROOT,
    process.env.LLVM_HOME,
    process.env.LLVM_DIR,
    "C:\\Program Files\\LLVM",
    ...visualStudioRoots().map((root) => path.join(root, "VC", "Tools", "Llvm", "x64"))
  ].filter(Boolean);
  return roots.map((root) => path.join(root, "bin", fileName));
}

// Finds Visual Studio installation roots without installing anything.
function visualStudioRoots() {
  if (process.platform !== "win32") {
    return [];
  }
  const roots = [];
  const vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
  if (existsSync(vswhere)) {
    const result = spawnSync(vswhere, ["-products", "*", "-property", "installationPath"], {
      encoding: "utf8"
    });
    if (result.status === 0) {
      roots.push(...result.stdout.split(/\r?\n/).filter(Boolean));
    }
  }
  roots.push(
    "C:\\Program Files\\Microsoft Visual Studio\\18\\Community",
    "C:\\Program Files\\Microsoft Visual Studio\\18\\BuildTools",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools"
  );
  return [...new Set(roots)].filter((root) => existsSync(root));
}

// Infers EMSDK from a helper path inside upstream/emscripten.
function inferEmsdkRoot(emcmake) {
  const emscriptenDir = path.dirname(emcmake);
  const upstreamDir = path.dirname(emscriptenDir);
  const emsdkRoot = path.dirname(upstreamDir);
  return path.basename(emscriptenDir).toLowerCase() === "emscripten" &&
    path.basename(upstreamDir).toLowerCase() === "upstream"
    ? emsdkRoot
    : undefined;
}

// Recursively collects matching files under a small installed-tool subtree.
function collectMatches(directory, fileName, matches) {
  if (!existsSync(directory)) {
    return;
  }
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const fullPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      collectMatches(fullPath, fileName, matches);
    } else if (entry.name === fileName) {
      matches.push(fullPath);
    }
  }
}
