import {
  cpSync,
  existsSync,
  mkdirSync,
  rmSync,
  statSync,
  writeFileSync
} from "node:fs";
import { dirname, isAbsolute, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

// Packages the root-served browser app into a static directory for Cloudflare.
const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const deployDir = resolve(root, ".deploy");

const requiredRuntimePaths = [
  "index.html",
  "dist",
  "assets",
  "src/app/styles.css"
];

const headers = [
  "/*",
  "  Cache-Control: no-store",
  "  Cross-Origin-Embedder-Policy: require-corp",
  "  Cross-Origin-Opener-Policy: same-origin",
  "  Cross-Origin-Resource-Policy: same-origin",
  ""
].join("\n");

assertWorkspaceChild(deployDir, ".deploy");
rmSync(deployDir, { recursive: true, force: true });
mkdirSync(deployDir, { recursive: true });

for (const runtimePath of requiredRuntimePaths) {
  copyRuntimePath(runtimePath);
}

writeFileSync(resolve(deployDir, "_headers"), headers);
console.log(`Packaged static site at ${deployDir}`);

function copyRuntimePath(runtimePath) {
  const source = resolve(root, runtimePath);
  const destination = resolve(deployDir, runtimePath);

  if (!existsSync(source)) {
    throw new Error(`Cannot package missing runtime path: ${runtimePath}`);
  }

  mkdirSync(dirname(destination), { recursive: true });
  cpSync(source, destination, {
    recursive: statSync(source).isDirectory()
  });
}

function assertWorkspaceChild(path, label) {
  const relativePath = relative(root, path);
  if (
    relativePath === "" ||
    isAbsolute(relativePath) ||
    relativePath.startsWith(`..${sep}`) ||
    relativePath === ".."
  ) {
    throw new Error(`Refusing to operate on ${label} outside the repository: ${path}`);
  }
}
