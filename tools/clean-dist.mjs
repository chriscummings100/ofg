import { rmSync } from "node:fs";
import { isAbsolute, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const generatedPaths = ["dist", "dist-test"];

for (const generatedPath of generatedPaths) {
  const absolutePath = resolve(root, generatedPath);
  assertWorkspaceChild(absolutePath, generatedPath);
  rmSync(absolutePath, { recursive: true, force: true });
}

function assertWorkspaceChild(path, label) {
  const relativePath = relative(root, path);
  if (
    relativePath === "" ||
    isAbsolute(relativePath) ||
    relativePath === ".." ||
    relativePath.startsWith(`..${sep}`)
  ) {
    throw new Error(`Refusing to remove ${label} outside the repository: ${path}`);
  }
}
