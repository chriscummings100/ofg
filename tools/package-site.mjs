// Packages the C++/WASM browser app into .deploy for Cloudflare Pages.

import {
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync
} from "node:fs";
import { dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const deployDir = resolve(root, ".deploy");
const runtimeFiles = [
  "index.html",
  "dist/app/main.js",
  "dist/app/canvasHost.js",
  "dist/app/wasmRuntime.js",
  "src/app/styles.css",
  "assets/wasm/ofg_cpp/ofg_cpp.js",
  "assets/wasm/ofg_cpp/ofg_cpp.wasm"
];
const expectedOutputPaths = ["_headers", ...runtimeFiles].sort();
const headers = [
  "/*",
  "  Cross-Origin-Embedder-Policy: require-corp",
  "  Cross-Origin-Opener-Policy: same-origin",
  "  Cross-Origin-Resource-Policy: same-origin",
  "",
  "/",
  "  Cache-Control: no-store",
  "",
  "/index.html",
  "  Cache-Control: no-store",
  "",
  "/dist/*",
  "  Cache-Control: no-cache",
  "",
  "/assets/wasm/*",
  "  Cache-Control: no-cache",
  ""
].join("\n");

assertWorkspaceChild(deployDir, ".deploy");
rmSync(deployDir, { recursive: true, force: true });
mkdirSync(deployDir, { recursive: true });

for (const runtimeFile of runtimeFiles) {
  copyRuntimeFile(runtimeFile);
}
writeFileSync(resolve(deployDir, "_headers"), headers);
verifyRequiredOutputs();
console.log(`Packaged Cloudflare Pages site at ${deployDir}`);

// Copies one required runtime file into the deploy directory.
function copyRuntimeFile(runtimeFile) {
  const source = resolve(root, runtimeFile);
  const destination = resolve(deployDir, runtimeFile);
  assertWorkspaceChild(source, runtimeFile);
  assertWorkspaceChild(destination, runtimeFile);

  if (!existsSync(source)) {
    throw new Error(`Cannot package missing runtime file: ${runtimeFile}`);
  }
  if (!statSync(source).isFile()) {
    throw new Error(`Runtime package entry must be a file: ${runtimeFile}`);
  }

  mkdirSync(dirname(destination), { recursive: true });
  writeFileSync(destination, readFileSync(source));
}

// Fails if packaging omitted or added files outside the deployment contract.
function verifyRequiredOutputs() {
  const actualOutputPaths = listFiles(deployDir)
    .map((file) => relative(deployDir, file).replaceAll("\\", "/"))
    .sort();
  const missing = expectedOutputPaths.filter(
    (outputPath) => !actualOutputPaths.includes(outputPath)
  );
  const unexpected = actualOutputPaths.filter(
    (outputPath) => !expectedOutputPaths.includes(outputPath)
  );

  if (missing.length > 0) {
    throw new Error(`Package is missing required outputs: ${missing.join(", ")}`);
  }
  if (unexpected.length > 0) {
    throw new Error(
      `Package contains unexpected runtime files: ${unexpected.join(", ")}`
    );
  }
}

// Recursively lists files under a directory for deploy contract verification.
function listFiles(dir) {
  const files = [];
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    const stats = statSync(path);
    if (stats.isDirectory()) {
      files.push(...listFiles(path));
    } else if (stats.isFile()) {
      files.push(path);
    }
  }
  return files;
}

// Refuses file operations that resolve outside the repository root.
function assertWorkspaceChild(path, label) {
  const relativePath = relative(root, path);
  if (
    relativePath === "" ||
    isAbsolute(relativePath) ||
    relativePath === ".." ||
    relativePath.startsWith(`..${sep}`)
  ) {
    throw new Error(`Refusing to operate on ${label} outside the repository: ${path}`);
  }
}
